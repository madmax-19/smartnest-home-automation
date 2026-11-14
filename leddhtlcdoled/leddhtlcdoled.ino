#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <time.h>
#include "secrets.h"

// ---------- OLED LIBRARIES (SH1106 for 1.3") ----------
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> 

// ---------- RELAY / LED PINS ----------
const uint8_t BATHROOM_RELAY_PIN = 18;
const uint8_t BEDROOM_RELAY_PIN  = 19;
const uint8_t DINING_RELAY_PIN   = 23;
const uint8_t KITCHEN_RELAY_PIN  = 25;
const uint8_t LIVING_RELAY_PIN   = 26;
const bool RELAY_ACTIVE_LOW = false;
const char* DEVICE_ID = "smartnest_esp32";

// ---------- DHT11 & LCD ----------
#define DHTPIN   27
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------- OLED SETUP (Software SPI & SH1106) ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_MOSI   14
#define OLED_CLK    13
#define OLED_DC     17
#define OLED_CS     32
#define OLED_RESET  16
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

// ---------- TIMING & VARIABLES ----------
const unsigned long STATE_INTERVAL_MS     = 3000;
const unsigned long HEARTBEAT_INTERVAL_MS = 20000;
const unsigned long SENSOR_INTERVAL_MS    = 30000;
unsigned long lastStateCheck = 0;
unsigned long lastHeartbeat  = 0;
unsigned long lastSensorRead = 0;
String accessToken;
float lastTemp = NAN, lastHum = NAN;
int activeRooms = 0;

// OLED Screen Timers
unsigned long lastInteraction = 0; 
unsigned long lastScreenChange = 0; 
int currentScreen = 0; // 0=Clock, 1=Status, 2=DVD
int16_t dvdX = 10, dvdY = 10, dvdVx = 2, dvdVy = 2;

// ================= LCD HELPERS =================
void lcdLine(uint8_t row, const String& text) {
  String padded = text;
  while (padded.length() < 16) padded += " ";
  if (padded.length() > 16) padded = padded.substring(0, 16);
  lcd.setCursor(0, row);
  lcd.print(padded);
}

void updateLcd() {
  char line1[17], line2[17];
  if (isnan(lastTemp)) snprintf(line1, sizeof(line1), "Temp: --.- C");
  else                 snprintf(line1, sizeof(line1), "Temp: %.1f C", lastTemp);
  if (isnan(lastHum))  snprintf(line2, sizeof(line2), "Hum:  --   %%");
  else                 snprintf(line2, sizeof(line2), "Hum:  %.0f %%", lastHum);

  lcdLine(0, line1);
  lcdLine(1, line2);
}

// ================= OLED ANALOG CLOCK =================
void drawAnalogClock() {
  display.clearDisplay();
  int cx = 64; // Center X
  int cy = 32; // Center Y
  
  // Draw outer circle
  display.drawCircle(cx, cy, 31, SH110X_WHITE);
  
  // Draw the 12 numbers on the clock face
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  for (int i = 1; i <= 12; i++) {
    // Calculate angle for each hour (subtract 90 because 12 o'clock is at the top)
    float angle = (i * 30 - 90) * PI / 180.0;
    
    // Calculate position (radius of 24 pixels keeps the text inside the circle)
    int x_pos = cx + cos(angle) * 24;
    int y_pos = cy + sin(angle) * 24;
    
    // Get string for the number
    String numStr = String(i);
    
    // Center the text exactly on the calculated position
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(numStr, 0, 0, &x1, &y1, &w, &h);
    
    display.setCursor(x_pos - w / 2, y_pos - h / 2);
    display.print(numStr);
  }

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    display.setCursor(20, 30); display.print("Syncing Time...");
    display.display(); return;
  }

  int sec = timeinfo.tm_sec;
  int min = timeinfo.tm_min;
  int hour = timeinfo.tm_hour;

  // Convert time to angles (in radians)
  float secAngle = (sec * 6 - 90) * PI / 180.0;
  float minAngle = (min * 6 - 90) * PI / 180.0;
  float hourAngle = ((hour % 12) * 30 + min * 0.5 - 90) * PI / 180.0;

  // Draw hands (adjusted lengths so they don't overlap the numbers too much)
  // Second hand (Longest)
  display.drawLine(cx, cy, cx + cos(secAngle) * 23, cy + sin(secAngle) * 23, SH110X_WHITE);
  // Minute hand (Medium)
  display.drawLine(cx, cy, cx + cos(minAngle) * 18, cy + sin(minAngle) * 18, SH110X_WHITE);
  // Hour hand (Shortest)
  display.drawLine(cx, cy, cx + cos(hourAngle) * 12, cy + sin(hourAngle) * 12, SH110X_WHITE);

  // Draw center dot so the hands look connected
  display.fillCircle(cx, cy, 1, SH110X_WHITE);

  display.display();
}

// ================= OLED STATUS & DVD =================
void drawStatus() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("SYSTEM STATUS");
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);

  display.setCursor(0, 25);
  display.setTextSize(2);
  display.print("Rooms: "); display.print(activeRooms); display.print("/5");

  long rssi = WiFi.RSSI();
  display.setTextSize(1);
  display.setCursor(0, 50); display.print("WiFi: "); display.print(rssi); display.print(" dBm");
  display.display();
}

void drawDVD() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(dvdX, dvdY); display.print("DVD");
  
  dvdX += dvdVx; dvdY += dvdVy;
  if (dvdX <= 0 || dvdX >= SCREEN_WIDTH - 40) dvdVx = -dvdVx;
  if (dvdY <= 0 || dvdY >= SCREEN_HEIGHT - 16) dvdVy = -dvdVy;
  display.display();
}

void updateOLED() {
  // DVD mode after 2 minutes of no interaction
  if (millis() - lastInteraction > 120000) {
    if (currentScreen != 2) { currentScreen = 2; display.clearDisplay(); }
    drawDVD(); return; 
  }
  // Toggle between Clock and Status every 10 seconds
  if (millis() - lastScreenChange > 10000) {
    lastScreenChange = millis();
    currentScreen = (currentScreen == 0) ? 1 : 0; 
  }
  if (currentScreen == 0) drawAnalogClock();
  else drawStatus();
}

// ================= RELAYS & HTTP =================
void writeRelay(uint8_t pin, bool on) {
  digitalWrite(pin, on ? (RELAY_ACTIVE_LOW ? LOW : HIGH) : (RELAY_ACTIVE_LOW ? HIGH : LOW));
}
void initRelay(uint8_t pin) {
  digitalWrite(pin, RELAY_ACTIVE_LOW ? HIGH : LOW);
  pinMode(pin, OUTPUT); writeRelay(pin, false);
}
bool beginRequest(HTTPClient& http, WiFiClientSecure& client, const String& url) {
  client.setInsecure(); http.setTimeout(12000); return http.begin(client, url);
}
void authorizedRequest(HTTPClient& http) {
  http.addHeader("apikey", SUPABASE_KEY); http.addHeader("Authorization", "Bearer " + accessToken);
}
bool loginSupabase() {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client; HTTPClient http;
  String url = String(SUPABASE_URL) + "/auth/v1/token?grant_type=password";
  if (!beginRequest(http, client, url)) return false;
  http.addHeader("apikey", SUPABASE_KEY); http.addHeader("Content-Type", "application/json");
  JsonDocument req; req["email"] = DEVICE_EMAIL; req["password"] = DEVICE_PASSWORD;
  String body; serializeJson(req, body);
  int code = http.POST(body); String response = http.getString(); http.end();
  if (code != 200) { accessToken = ""; return false; }
  JsonDocument res; deserializeJson(res, response);
  accessToken = res["access_token"].as<String>(); return accessToken.length() > 0;
}

void sendHeartbeat() {
  if (accessToken.length() == 0 && !loginSupabase()) return;
  time_t now = time(nullptr); if (now < 1700000000) return;
  WiFiClientSecure client; HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_heartbeat?on_conflict=device_id";
  if (!beginRequest(http, client, url)) return;
  authorizedRequest(http); http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "resolution=merge-duplicates,return=minimal");
  
  char stamp[25]; struct tm* t = gmtime(&now); strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", t);
  
  // --- NEW: Calculate ESP Health ---
  unsigned long uptimeSeconds = millis() / 1000;
  unsigned long freeHeap = ESP.getFreeHeap();

  JsonDocument doc; 
  doc["device_id"] = DEVICE_ID; 
  doc["last_seen"] = stamp;
  // --- NEW: Add to JSON ---
  doc["uptime"] = uptimeSeconds;
  doc["free_ram"] = freeHeap;

  String body; serializeJson(doc, body);
  int code = http.POST(body); http.end();
  if (code == 401) accessToken = "";
}

void sendSensorReading() {
  if (accessToken.length() == 0 && !loginSupabase()) return;
  float t = dht.readTemperature(); float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) return;
  
  lastTemp = t; lastHum = h;
  updateLcd(); // Updates the 16x2 LCD

  WiFiClientSecure client; HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/sensor_readings";
  if (!beginRequest(http, client, url)) return;
  authorizedRequest(http); http.addHeader("Content-Type", "application/json"); http.addHeader("Prefer", "return=minimal");
  JsonDocument doc; doc["temperature"] = serialized(String(t, 1)); doc["humidity"] = serialized(String(h, 0));
  String body; serializeJson(doc, body);
  int code = http.POST(body); http.end();
}

void applyDeviceStates(const JsonArray& rows) {
  bool bL=false, bF=false, bedL=false, bedF=false, k1=false, k2=false, din=false, liv=false;
  bool sBL=false, sBF=false, sBedL=false, sBedF=false, sK1=false, sK2=false;
  bool stateChanged = false;
  for (JsonObject row : rows) {
    String id = row["device_id"] | ""; bool on = row["is_on"] | false;
    if (id == "bathroom_light")  { if(bL != on) stateChanged=true; bL = on; sBL = true; }
    if (id == "bathroom_fan")    { if(bF != on) stateChanged=true; bF = on; sBF = true; }
    if (id == "bedroom_light")   { if(bedL != on) stateChanged=true; bedL = on; sBedL = true; }
    if (id == "bedroom_fan")     { if(bedF != on) stateChanged=true; bedF = on; sBedF = true; }
    if (id == "dining_light")    { if(din != on) stateChanged=true; din = on; }
    if (id == "kitchen_light_1") { if(k1 != on) stateChanged=true; k1 = on; sK1 = true; }
    if (id == "kitchen_light_2") { if(k2 != on) stateChanged=true; k2 = on; sK2 = true; }
    if (id == "living_light")    { if(liv != on) stateChanged=true; liv = on; }
  }
  bool bath = sBL && sBF && bL && bF; bool bed  = sBedL && sBedF && bedL && bedF; bool kit  = sK1 && sK2 && k1 && k2;
  writeRelay(BATHROOM_RELAY_PIN, bath); writeRelay(BEDROOM_RELAY_PIN, bed);
  writeRelay(DINING_RELAY_PIN, din); writeRelay(KITCHEN_RELAY_PIN, kit); writeRelay(LIVING_RELAY_PIN, liv);
  activeRooms = (bath?1:0) + (bed?1:0) + (din?1:0) + (kit?1:0) + (liv?1:0);
  if (stateChanged) {
    lastInteraction = millis();
    if (currentScreen == 2) { currentScreen = 1; display.clearDisplay(); }
  }
}

void readDeviceStates() {
  if (accessToken.length() == 0 && !loginSupabase()) return;
  WiFiClientSecure client; HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_states?select=device_id,is_on";
  if (!beginRequest(http, client, url)) return;
  authorizedRequest(http);
  int code = http.GET(); String response = http.getString(); http.end();
  if (code != 200) return;
  JsonDocument doc; DeserializationError err = deserializeJson(doc, response);
  if (!err) applyDeviceStates(doc.as<JsonArray>());
}

// ================= SETUP & LOOP =================
void setup() {
  Serial.begin(115200);
  
  // LCD Init
  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  lcdLine(0, "SmartNest");
  lcdLine(1, "Initializing...");

  // OLED Init
  display.begin(0x02, true);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 25); display.setTextSize(1); display.print("Connecting to WiFi...");
  display.display();

  // DHT Init
  dht.begin();
  
  // Relays Init
  initRelay(BATHROOM_RELAY_PIN); initRelay(BEDROOM_RELAY_PIN);
  initRelay(DINING_RELAY_PIN); initRelay(KITCHEN_RELAY_PIN); initRelay(LIVING_RELAY_PIN);

  // WiFi & Time
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  // Set Timezone to IST (+5:30)
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov"); 
  
  loginSupabase();
  
  updateLcd(); // Restore LCD to Temp/Humidity display
  lastInteraction = millis(); 
  lastScreenChange = millis();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (accessToken.length() == 0) loginSupabase();
    if (millis() - lastStateCheck >= STATE_INTERVAL_MS) { lastStateCheck = millis(); readDeviceStates(); }
    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) { lastHeartbeat = millis(); sendHeartbeat(); }
    if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) { lastSensorRead = millis(); sendSensorReading(); }
  }
  
  updateOLED(); // Draws the Analog clock, Status, or DVD logo
  delay(50); 
}