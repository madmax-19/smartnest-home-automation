# SmartNest ESP8266 update

## What changed
- Website now has one switch for each room group: Bathroom, Bedroom, Dining, Kitchen and Living.
- Bathroom switch updates both `bathroom_light` and `bathroom_fan`.
- Bedroom switch updates both `bedroom_light` and `bedroom_fan`.
- Kitchen switch updates both `kitchen_light_1` and `kitchen_light_2`.
- ESP8266 status indicator uses `device_heartbeat.last_seen`; status is shown disconnected if no heartbeat is received for 60 seconds.
- Added `homeautomation_ESP8266.ino` and `supabase_esp8266_setup.sql`.

## Setup
1. Back up your existing website and firmware.
2. Rotate the Wi-Fi password and Supabase device-account password that were present in the previously uploaded sketch. Do not commit real credentials to GitHub.
3. Run `supabase_esp8266_setup.sql` in Supabase SQL Editor.
4. Open `homeautomation_ESP8266.ino` in Arduino IDE.
5. Install/select the ESP8266 board package and install ArduinoJson version 7.
6. Fill in Wi-Fi, Supabase publishable key, and the newly-created device-account email/password.
7. Check the relay pin mapping and trigger polarity before connecting relay modules.

## Default relay mapping
For a NodeMCU-style ESP8266 board:
- Bathroom relay: D1 / GPIO5
- Bedroom relay: D2 / GPIO4
- Dining relay: D5 / GPIO14
- Kitchen relay: D6 / GPIO12
- Living relay: D7 / GPIO13

The sketch defaults to active-LOW relay inputs. If your relay board is active-HIGH, set `RELAY_ACTIVE_LOW` to `false`.

**Important:** `ESP8266MOD` is the module marking, not enough to identify the carrier board. The pin labels above assume a NodeMCU-style board. If yours is a bare ESP-12 module, do not wire it using D1/D2 labels; verify its actual GPIO breakout and power circuitry first. ESP8266 GPIO is 3.3V logic and is not 5V tolerant. Use a suitable regulated supply and common ground for relay modules. Do not connect mains loads during initial testing.

## Heartbeat security note
The sketch uses `BearSSL::WiFiClientSecure::setInsecure()` to make the HTTPS connection without validating the server certificate. Traffic is encrypted, but server identity is not verified. For a production installation, replace this with certificate validation / a maintained trust anchor.
