# SmartNest — ESP32 Smart Home Dashboard

A responsive static website for your ESP32 smart-home model. It can be hosted free using GitHub Pages.

## Included controls

- Bathroom: LED + exhaust fan
- Bedroom: LED + fan
- Dining room: LED + DHT22 temperature/humidity display
- Kitchen: two LEDs
- Living room: LED + OLED status preview
- Automatic gate: Open, Close and Stop
- All-lights-and-fans-off button
- Demo mode, activity log, connection status and mobile layout

## Publish on GitHub Pages

1. Create a new GitHub repository, for example `smart-home-automation`.
2. Upload `index.html` and `README.md` to the repository's root folder.
3. Open **Settings → Pages**.
4. Under **Build and deployment**, choose **Deploy from a branch**.
5. Select branch **main** and folder **/(root)**, then Save.
6. Wait for GitHub to publish the site. Open the Pages URL shown there.

## Demo mode

The website starts in Demo mode. All switches and gate buttons can be tested without hardware. Sensor values are simulated.

## ESP32 live connection — API contract

The website expects the ESP32 to be reachable on the same Wi-Fi network and provide these HTTP endpoints:

### GET `/api/status`

Return JSON in this format:

```json
{
  "devices": {
    "bathroom_light": false,
    "bathroom_fan": false,
    "bedroom_light": false,
    "bedroom_fan": false,
    "dining_light": false,
    "kitchen_light_1": false,
    "kitchen_light_2": false,
    "living_light": false
  },
  "temperature": 27.4,
  "humidity": 61,
  "gate": "closed"
}
```

Allowed gate status values: `closed`, `open`, `moving_open`, `moving_close`.

### POST `/api/control`

Request JSON:

```json
{"device":"bedroom_light","state":true}
```

The ESP32 should validate the device name, update the correct output, and return a success JSON response.

### POST `/api/gate`

Request JSON:

```json
{"action":"open"}
```

Allowed actions: `open`, `close`, `stop`.

## Important ESP32 / browser notes

- Set the address in the website's ESP32 connection box, e.g. `http://192.168.1.50`.
- The browser and ESP32 must be on the same local Wi-Fi network for this simple local setup.
- The ESP32 server must enable CORS for the website's origin (or allow `*` for a controlled college demo) and answer `OPTIONS` preflight requests.
- If the website is served over HTTPS by GitHub Pages, browsers may block requests to an `http://` ESP32 address as mixed content. A normal GitHub Pages site therefore works immediately in Demo mode, but direct live control may require serving the dashboard locally over HTTP, using a secure HTTPS reverse proxy/tunnel, or using a cloud messaging service such as MQTT with a secure broker.
- Do not expose the ESP32 control API to the public internet without authentication and appropriate security.
- Use suitable driver boards/relays for loads. Never connect fans, motors or other loads directly to ESP32 GPIO pins. Use low-voltage model loads and suitable power supplies.

## Next step

The matching ESP32 firmware must be written for your exact GPIO pin assignments, relay module logic (active HIGH/LOW), motor driver and limit switches. The website is the frontend; this package does not flash firmware to the ESP32.
