# SmartNest: IoT Home Automation

SmartNest is a cloud-connected home automation system. An **ESP32** controls the lights, fans, an automatic gate and the sensors. A web dashboard lets you control and monitor everything from anywhere. The two never talk to each other directly. Both read and write the same **Supabase** database.

**Live site:** https://smartnest-home-automation.onrender.com/

```
 Dashboard (HTML/JS, Render)  <-->  Supabase (Postgres, Auth, Realtime)  <-->  ESP32 (relays, gate, sensors)
```

---

## Features

### Dashboard
- Sign-in screen as the front page; **Features** and **How it works** are separate pages
- Real-time room switches (bathroom, bedroom, dining, kitchen, living) and **All ON / All OFF**
- **Gate control:** Open, Close, Stop, an animated gate, and a **speed slider** (30–100%)
- Live temperature and humidity with a chart, plus an estimated power and cost card
- **Scenes:** Good night, Leaving home, Welcome home
- **Voice control** (browser Web Speech API) and a simulated **SmartNest AI chat** (rule-based, not an LLM)
- **Command palette:** press `Ctrl+K` (or `Cmd+K`) and type, e.g. "open gate"
- **Activity log**, **emergency (panic) button**, night mode, theme picker, dashboard lock
- **Alerts:** warnings when the gate stays open or keeps moving, optional auto-close after 2 minutes, and browser notifications
- **Offline protection:** when the ESP32 heartbeat is older than 60 seconds, controls pause and commands from the switches, voice, chat and palette are blocked (the panic button still works)
- Account chip showing the signed-in email and role
- Installable web app (manifest, icons and a service worker)

### Roles (admin panel)
| Role | What it can do |
|---|---|
| **Admin** | Controls everything and opens the Admin page to manage users |
| **Operator** | Controls every switch and feature, but cannot see the Admin page or change roles |
| **Guest** | View only |
| **Device** | The ESP32's own account |

New sign-ups start as **guests**. Admin roles cannot be changed from the website, only from the Supabase SQL Editor, so nobody can remove an admin.

### Firmware (ESP32)
- Polls Supabase every 3 s for device states and every 1 s for the gate command and speed
- Sends a heartbeat (uptime and free RAM) every 20 s and DHT11 readings every 30 s
- PWM gate motor control; the motor stops the moment a limit switch is hit
- 16x2 LCD with temperature and humidity, and a 1.3" OLED with an analog clock, status screen and screensavers (starfield and robot)

---

## Hardware

| Part | Notes |
|---|---|
| ESP32 Dev Module | Main controller (Arduino core 3.x) |
| ESP8266 NodeMCU | Optional separate status board |
| DHT11 | Temperature and humidity |
| 16x2 I2C LCD (0x27) | Dining room display |
| 1.3" OLED (SH1106, software SPI) | Living room "smart TV" |
| LEDs / 8-channel relay module | 5 room outputs (LEDs now, relays for 230V later) |
| L298N driver + DC gear motor | Automatic gate |
| 2 micro limit switches | Gate open and close ends |
| 12V 2A adapter | Motor power |

### Pin map (ESP32)
| Function | GPIO |
|---|---|
| DHT11 | 27 |
| LCD I2C (SDA / SCL) | 21 / 22 |
| OLED (SPI) | 14, 13, 17, 32, 16 |
| Room outputs: bathroom, bedroom, dining, kitchen, living | 18, 19, 23, 25, 26 |
| Gate motor: IN1, IN2, ENA (PWM) | 12, 4, 5 |
| Limit switches: open, close | 33, 15 |

Limit switches use `INPUT_PULLUP`. Wire the common (C) pin to GND and the normally-open (NO) pin to the GPIO. The pin reads **1 at rest and 0 when pressed**. GPIO 12 and 15 are strapping pins, so do not hold them LOW or HIGH at boot.

---

## Repository layout
```
index.html               the whole dashboard (HTML, CSS, JS)
sw.js                    service worker (app install)
manifest.webmanifest     app manifest
icon-192.png, icon-512.png
firmware/smartnest/
  smartnest.ino          ESP32 sketch (the folder name must match the .ino name)
  secrets.h              your Wi-Fi and Supabase credentials (NOT committed)
  secrets.example.h      template for secrets.h
supabase_extra_setup.sql, supabase_esp8266_setup.sql   original setup
supabase_gate_speed.sql  adds the gate speed column
supabase_roles.sql       roles table and row-level security policies
supabase_operator_role.sql   adds the operator role and locks admin rows
```

---

## Setup

### 1. Supabase
1. Create a project and enable **Email** sign-in under Authentication.
2. Create these tables (RLS enabled): `device_states`, `gate_state` (one row, `id = 1`, columns `state`, `speed`, `updated_at`), `sensor_readings`, `device_heartbeat`.
3. Seed `device_states` with the 8 device IDs: `bathroom_light`, `bathroom_fan`, `bedroom_light`, `bedroom_fan`, `dining_light`, `kitchen_light_1`, `kitchen_light_2`, `living_light`.
4. Run the SQL files in this order in the SQL Editor:
   1. `supabase_extra_setup.sql` and `supabase_esp8266_setup.sql`
   2. `supabase_gate_speed.sql`
   3. `supabase_roles.sql`
   4. `supabase_operator_role.sql`
5. Create a separate login for the ESP32, then set its role to **device** in the Admin page.

### 2. ESP32 firmware
1. Install the ESP32 board package (3.x) and the libraries: ArduinoJson (v7), DHT sensor library, Adafruit Unified Sensor, LiquidCrystal_I2C, Adafruit GFX, Adafruit SH110X.
2. Copy `secrets.example.h` to `secrets.h` and fill in your values.
3. Upload `smartnest.ino`. Open the Serial Monitor at 115200 to watch the gate line (`1` = switch pressed).

### 3. Website (Render)
Create a **Static Site** from this GitHub repository. Leave the build command empty and set the publish directory to `.`. Every commit to `main` redeploys.

Put your Supabase URL and **publishable** key in `index.html`. The publishable key is meant to be public, but only if row-level security stays on.

---

## Security notes
- Never commit `secrets.h`, a Supabase **secret/service-role** key, or a database password. Keep `secrets.h` in `.gitignore`.
- Row-level security is the real protection. Only admin, operator and device accounts can change devices, the gate, sensor readings or the heartbeat.
- The firmware uses `client.setInsecure()`, which skips certificate checks. This is acceptable for a prototype but not for production.
- **230V warning:** when you move from LEDs to AC bulbs, use a closed enclosure, insulate all mains wiring, keep it away from the ESP32, and have it checked by someone who knows mains electrical work.

---

## Troubleshooting
- **Limit switch looks dead:** press the plunger and watch the Serial Monitor. The value should change, and check the joints are soldered, not just twisted.
- **Gate runs at full speed:** remove the ENA jumper on the L298N so PWM on GPIO 5 takes effect, and join the L298N GND to the ESP32 GND.
- **Motor hums but does not turn:** raise the minimum speed above 30%.
- **Commands do nothing:** check the ESP32 pill in the top bar. When the board is offline, commands are blocked on purpose.
- **Site looks unchanged after a deploy:** hard refresh (Ctrl+Shift+R) or clear the site data for the page.

---

## Known limitations and ideas
- The AI chat is a simple rule-based parser. A real LLM could replace it.
- CDN libraries (Chart.js) are not pinned to exact versions.
- Ideas: floor-plan view, chart time ranges, per-room energy, motion or door sensor with a security mode, RFID gate, and Google Home or Alexa integration.
