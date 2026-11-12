/*
  SMARTNEST - ESP8266 firmware
  Room controls:
    Bathroom: bathroom_light + bathroom_fan
    Bedroom : bedroom_light + bedroom_fan
    Dining  : dining_light
    Kitchen : kitchen_light_1 + kitchen_light_2
    Living  : living_light

  Install libraries:
    - ArduinoJson (v7)
  Board package: ESP8266 by ESP8266 Community

  IMPORTANT:
    - Set your own Wi-Fi and Supabase device-account credentials below.
    - The credentials from the old uploaded sketch should be changed/rotated.
    - Relay outputs are configured ACTIVE LOW. Change RELAY_ACTIVE_LOW if needed.
    - GPIO mapping below assumes a NodeMCU-style ESP8266 board.
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// ---------- WIFI ----------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---------- SUPABASE ----------
const char* SUPABASE_URL = "https://ojqghjtjotsmrxwmjjkj.supabase.co";
const char* SUPABASE_KEY = "YOUR_SUPABASE_PUBLISHABLE_KEY";
const char* DEVICE_EMAIL = "YOUR_DEVICE_AUTH_EMAIL";
const char* DEVICE_PASSWORD = "YOUR_NEW_DEVICE_AUTH_PASSWORD";

// ---------- RELAY CONFIG ----------
// NodeMCU labels: D1=GPIO5, D2=GPIO4, D5=GPIO14, D6=GPIO12, D7=GPIO13
const uint8_t BATHROOM_RELAY_PIN = 5;   // D1
const uint8_t BEDROOM_RELAY_PIN  = 4;   // D2
const uint8_t DINING_RELAY_PIN   = 14;  // D5
const uint8_t KITCHEN_RELAY_PIN  = 12;  // D6
const uint8_t LIVING_RELAY_PIN   = 13;  // D7

const bool RELAY_ACTIVE_LOW = true;
const char* DEVICE_ID = "smartnest_esp8266";

const unsigned long STATE_INTERVAL_MS = 3000;
const unsigned long HEARTBEAT_INTERVAL_MS = 20000;
unsigned long lastStateCheck = 0;
unsigned long lastHeartbeat = 0;
String accessToken;

void writeRelay(uint8_t pin, bool on) {
  const uint8_t onLevel = RELAY_ACTIVE_LOW ? LOW : HIGH;
  const uint8_t offLevel = RELAY_ACTIVE_LOW ? HIGH : LOW;
  digitalWrite(pin, on ? onLevel : offLevel);
}

void initRelay(uint8_t pin) {
  // Set inactive level before enabling output to reduce startup relay pulses.
  digitalWrite(pin, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(pin, OUTPUT);
  writeRelay(pin, false);
}

bool beginRequest(HTTPClient& http, BearSSL::WiFiClientSecure& client, const String& url) {
  client.setInsecure(); // TLS is encrypted, but certificate identity is NOT verified.
  http.setTimeout(12000);
  return http.begin(client, url);
}

bool loginSupabase() {
  if (WiFi.status() != WL_CONNECTED) return false;

  BearSSL::WiFiClientSecure client;
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/auth/v1/token?grant_type=password";
  if (!beginRequest(http, client, url)) return false;

  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Content-Type", "application/json");

  JsonDocument request;
  request["email"] = DEVICE_EMAIL;
  request["password"] = DEVICE_PASSWORD;
  String body;
  serializeJson(request, body);

  int code = http.POST(body);
  String response = http.getString();
  http.end();

  if (code != 200) {
    Serial.printf("Supabase login failed: HTTP %d\n%s\n", code, response.c_str());
    accessToken = "";
    return false;
  }

  JsonDocument result;
  if (deserializeJson(result, response)) {
    Serial.println("Could not parse login response");
    return false;
  }

  accessToken = result["access_token"].as<String>();
  Serial.println(accessToken.length() ? "Supabase login OK" : "No access token received");
  return accessToken.length() > 0;
}

bool authorizedRequest(HTTPClient& http) {
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", "Bearer " + accessToken);
  return true;
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (accessToken.length() == 0 && !loginSupabase()) return;

  BearSSL::WiFiClientSecure client;
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_heartbeat?on_conflict=device_id";
  if (!beginRequest(http, client, url)) return;

  authorizedRequest(http);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "resolution=merge-duplicates,return=minimal");

  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["last_seen"] = "now()"; // replaced below with an ISO timestamp from the ESP clock only if NTP is configured
  // PostgREST accepts an ISO timestamp; use server-side current time via SQL RPC is not available here.
  // Use the ESP's NTP-synchronised UTC clock.
  time_t now = time(nullptr);
  if (now > 1700000000) {
    char stamp[25];
    struct tm* tmInfo = gmtime(&now);
    strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", tmInfo);
    doc["last_seen"] = stamp;
  } else {
    Serial.println("Clock not synced; heartbeat skipped until NTP is ready");
    http.end();
    return;
  }

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  String response = http.getString();
  http.end();

  if (code == 401) { accessToken = ""; Serial.println("Heartbeat auth expired"); }
  else if (code < 200 || code >= 300) Serial.printf("Heartbeat HTTP %d: %s\n", code, response.c_str());
  else Serial.println("ESP8266 heartbeat sent");
}

void applyDeviceStates(const JsonArray& rows) {
  bool bathroomLight = false, bathroomFan = false, bedroomLight = false, bedroomFan = false;
  bool kitchen1 = false, kitchen2 = false, dining = false, living = false;
  bool seenBathroomLight = false, seenBathroomFan = false, seenBedroomLight = false, seenBedroomFan = false;
  bool seenKitchen1 = false, seenKitchen2 = false;
  for (JsonObject row : rows) {
    String id = row["device_id"] | "";
    bool on = row["is_on"] | false;
    if (id == "bathroom_light") { bathroomLight = on; seenBathroomLight = true; }
    if (id == "bathroom_fan") { bathroomFan = on; seenBathroomFan = true; }
    if (id == "bedroom_light") { bedroomLight = on; seenBedroomLight = true; }
    if (id == "bedroom_fan") { bedroomFan = on; seenBedroomFan = true; }
    if (id == "dining_light") dining = on;
    if (id == "kitchen_light_1") { kitchen1 = on; seenKitchen1 = true; }
    if (id == "kitchen_light_2") { kitchen2 = on; seenKitchen2 = true; }
    if (id == "living_light") living = on;
  }
  // Group relays turn ON only when every member state is ON.
  bool bathroom = seenBathroomLight && seenBathroomFan && bathroomLight && bathroomFan;
  bool bedroom = seenBedroomLight && seenBedroomFan && bedroomLight && bedroomFan;
  bool kitchen = seenKitchen1 && seenKitchen2 && kitchen1 && kitchen2;

  writeRelay(BATHROOM_RELAY_PIN, bathroom);
  writeRelay(BEDROOM_RELAY_PIN, bedroom);
  writeRelay(DINING_RELAY_PIN, dining);
  writeRelay(KITCHEN_RELAY_PIN, kitchen);
  writeRelay(LIVING_RELAY_PIN, living);

  Serial.printf("Rooms | Bathroom:%s Bedroom:%s Dining:%s Kitchen:%s Living:%s\n",
    bathroom ? "ON" : "OFF", bedroom ? "ON" : "OFF", dining ? "ON" : "OFF",
    kitchen ? "ON" : "OFF", living ? "ON" : "OFF");
}

void readDeviceStates() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (accessToken.length() == 0 && !loginSupabase()) return;

  BearSSL::WiFiClientSecure client;
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_states?select=device_id,is_on";
  if (!beginRequest(http, client, url)) return;
  authorizedRequest(http);

  int code = http.GET();
  String response = http.getString();
  http.end();

  if (code == 401) { accessToken = ""; Serial.println("Token expired; will log in again"); return; }
  if (code != 200) { Serial.printf("State read HTTP %d: %s\n", code, response.c_str()); return; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) { Serial.printf("State JSON error: %s\n", err.c_str()); return; }
  applyDeviceStates(doc.as<JsonArray>());
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    delay(400);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else Serial.println("Wi-Fi connection timed out; retrying in loop");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  initRelay(BATHROOM_RELAY_PIN);
  initRelay(BEDROOM_RELAY_PIN);
  initRelay(DINING_RELAY_PIN);
  initRelay(KITCHEN_RELAY_PIN);
  initRelay(LIVING_RELAY_PIN);

  // NTP time is required for accurate heartbeat timestamps.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) loginSupabase();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 5000) {
      lastReconnect = millis();
      WiFi.reconnect();
    }
    delay(10);
    return;
  }

  if (accessToken.length() == 0) loginSupabase();

  if (millis() - lastStateCheck >= STATE_INTERVAL_MS) {
    lastStateCheck = millis();
    readDeviceStates();
  }
  if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }
  delay(5);
}
