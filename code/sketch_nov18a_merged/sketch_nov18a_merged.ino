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

// ---------- GATE (L298N + limit switches) ----------
const uint8_t GATE_IN1 = 12;          // L298N IN1 (direction)
const uint8_t GATE_IN2 = 4;           // L298N IN2 (direction)
const uint8_t GATE_ENA = 5;           // L298N ENA (PWM speed)
const uint8_t GATE_OPEN_LIMIT = 33;   // Open limit switch (NO -> GND when pressed)
const uint8_t GATE_CLOSE_LIMIT = 15;  // Close limit switch (NO -> GND when pressed)

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
unsigned long lastGateCheck  = 0;
String accessToken;
float lastTemp = NAN, lastHum = NAN;
int activeRooms = 0;

// Gate variables
String gateState = "closed";
int gateSpeed = 150;   // PWM 0-255, updated from the website slider

// OLED Screen Timers
unsigned long lastInteraction = 0; 
unsigned long lastScreenChange = 0; 
int currentScreen = 0; // 0=Clock, 1=Status, 2=Starfield, 3=Robot

// Starfield Variables
#define NUM_STARS 50
float starX[NUM_STARS], starY[NUM_STARS], starZ[NUM_STARS];

// Robot Variables
unsigned long lastRobotUpdate = 0;
int robotState = 0; // 0=Center, 1=Left, 2=Right, 3=Blink, 4=Smile, 5=Talk
int eyeOffset = 0;
bool eyesClosed = false;
int mouthType = 0; 

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
  int cx = 64; int cy = 32;
  display.drawCircle(cx, cy, 31, SH110X_WHITE);
  display.setTextSize(1); display.setTextColor(SH110X_WHITE);
  
  for (int i = 1; i <= 12; i++) {
    float angle = (i * 30 - 90) * PI / 180.0;
    int x_pos = cx + cos(angle) * 24; int y_pos = cy + sin(angle) * 24;
    String numStr = String(i); int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(numStr, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_pos - w / 2, y_pos - h / 2); display.print(numStr);
  }

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){ display.setCursor(20, 30); display.print("Syncing..."); display.display(); return; }
  float sA = (timeinfo.tm_sec * 6 - 90) * PI / 180.0, mA = (timeinfo.tm_min * 6 - 90) * PI / 180.0, hA = ((timeinfo.tm_hour % 12) * 30 + timeinfo.tm_min * 0.5 - 90) * PI / 180.0;
  
  display.drawLine(cx, cy, cx + cos(sA) * 23, cy + sin(sA) * 23, SH110X_WHITE);
  display.drawLine(cx, cy, cx + cos(mA) * 18, cy + sin(mA) * 18, SH110X_WHITE);
  display.drawLine(cx, cy, cx + cos(hA) * 12, cy + sin(hA) * 12, SH110X_WHITE);
  display.fillCircle(cx, cy, 1, SH110X_WHITE);
  display.display();
}

// ================= OLED STATUS =================
void drawStatus() {
  display.clearDisplay();
  display.setTextSize(1); display.setCursor(0, 0); display.print("SYSTEM STATUS");
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);
  display.setCursor(0, 25); display.setTextSize(2); display.print("Rooms: "); display.print(activeRooms); display.print("/5");
  display.setTextSize(1); display.setCursor(0, 50); display.print("WiFi: "); display.print(WiFi.RSSI()); display.print(" dBm");
  display.display();
}

// ================= OLED STARFIELD =================
void initStars() {
  for(int i = 0; i < NUM_STARS; i++) {
    starX[i] = random(-64, 64);
    starY[i] = random(-32, 32);
    starZ[i] = random(1, 128);
  }
}

void drawStarfield() {
  display.clearDisplay();
  for(int i = 0; i < NUM_STARS; i++) {
    starZ[i] -= 2.5; 
    if (starZ[i] < 1) {
      starZ[i] = 128;
      starX[i] = random(-64, 64);
      starY[i] = random(-32, 32);
    }
    int px = (starX[i] / starZ[i]) * 64 + 64;
    int py = (starY[i] / starZ[i]) * 32 + 32;
    if (px >= 0 && px < 128 && py >= 0 && py < 64) {
      display.drawPixel(px, py, SH110X_WHITE);
      if (starZ[i] < 20) {
        int px2 = (starX[i] / (starZ[i] + 2.0)) * 64 + 64;
        int py2 = (starY[i] / (starZ[i] + 2.0)) * 32 + 32;
        display.drawLine(px, py, px2, py2, SH110X_WHITE);
      }
    }
  }
  display.display();
}

// ================= OLED CARTOON ROBOT =================
void drawRobot() {
  display.clearDisplay();
  
  // Draw Antenna
  display.drawLine(64, 10, 64, 4, SH110X_WHITE);
  display.fillCircle(64, 4, 2, SH110X_WHITE);

  // Draw Head
  display.drawRoundRect(34, 10, 60, 44, 10, SH110X_WHITE);
  
  // Update Robot Expression every 800ms
  if (millis() - lastRobotUpdate > 800) {
    lastRobotUpdate = millis();
    robotState = random(0, 6); // 0 to 5
    
    if (robotState == 0) { eyeOffset = 0; eyesClosed = false; mouthType = 0; }      // Center
    else if (robotState == 1) { eyeOffset = -3; eyesClosed = false; mouthType = 0; } // Look Left
    else if (robotState == 2) { eyeOffset = 3; eyesClosed = false; mouthType = 0; }  // Look Right
    else if (robotState == 3) { eyesClosed = true; mouthType = 0; }                  // Blink
    else if (robotState == 4) { eyeOffset = 0; eyesClosed = false; mouthType = 1; }  // Smile
    else if (robotState == 5) { eyeOffset = 0; eyesClosed = false; mouthType = 2; }  // Talk
  }

  // Draw Eyes
  if (eyesClosed) {
    display.drawLine(42, 26, 58, 26, SH110X_WHITE);
    display.drawLine(70, 26, 86, 26, SH110X_WHITE);
  } else {
    display.fillCircle(50, 26, 8, SH110X_WHITE);
    display.fillCircle(78, 26, 8, SH110X_WHITE);
    
    // Pupils (Black inside the white eyes)
    display.fillCircle(50 + eyeOffset, 26, 3, SH110X_BLACK);
    display.fillCircle(78 + eyeOffset, 26, 3, SH110X_BLACK);
  }

  // Draw Mouth
  if (mouthType == 0) { // Straight
    display.drawLine(54, 42, 74, 42, SH110X_WHITE);
  } else if (mouthType == 1) { // Smile
    display.drawLine(52, 40, 58, 44, SH110X_WHITE);
    display.drawLine(58, 44, 70, 44, SH110X_WHITE);
    display.drawLine(70, 44, 76, 40, SH110X_WHITE);
  } else if (mouthType == 2) { // Talk
    display.drawRect(56, 40, 16, 6, SH110X_WHITE);
  }
  
  display.display();
}

// ================= OLED UPDATE LOGIC =================
void updateOLED() {
  // Screensaver mode after 2 minutes of no interaction
  if (millis() - lastInteraction > 120000) {
    if (currentScreen < 2 || currentScreen > 3) { 
        currentScreen = 2; 
        display.clearDisplay(); 
        initStars();
        lastScreenChange = millis();
    }
    
    // Alternate between Starfield and Robot every 10 seconds
    if (millis() - lastScreenChange > 10000) {
        lastScreenChange = millis();
        currentScreen = (currentScreen == 2) ? 3 : 2; 
        display.clearDisplay();
    }

    if (currentScreen == 2) drawStarfield();
    else drawRobot();
    return; 
  }
  
  // Normal mode: Toggle between Clock and Status every 10 seconds
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
  client.setInsecure(); http.setTimeout(5000); return http.begin(client, url);
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
  
  unsigned long uptimeSeconds = millis() / 1000;
  unsigned long freeHeap = ESP.getFreeHeap();

  JsonDocument doc; 
  doc["device_id"] = DEVICE_ID; 
  doc["last_seen"] = stamp;
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
    if (currentScreen >= 2) { currentScreen = 1; display.clearDisplay(); }
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

// ================= GATE CONTROL =================
void updateGateState(String state) {
  if (accessToken.length() == 0 && !loginSupabase()) return;
  WiFiClientSecure client; HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/gate_state?id=eq.1";
  if (!beginRequest(http, client, url)) return;
  authorizedRequest(http); http.addHeader("Content-Type", "application/json");
  JsonDocument doc; doc["state"] = state;
  String body; serializeJson(doc, body);
  http.PATCH(body); http.end();
  Serial.println("Gate state updated in DB: " + state);
}

void readGateState() {
  if (accessToken.length() == 0 && !loginSupabase()) return;
  WiFiClientSecure client; HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/gate_state?id=eq.1&select=state,speed";
  if (!beginRequest(http, client, url)) return;
  authorizedRequest(http);
  int code = http.GET(); String response = http.getString(); http.end();
  if (code != 200) return;
  JsonDocument doc;
  if (!deserializeJson(doc, response) && doc.is<JsonArray>() && doc.size() > 0) {
    int pct = doc[0]["speed"] | 60; pct = constrain(pct, 30, 100);
    gateSpeed = map(pct, 0, 100, 0, 255);
    String newState = doc[0]["state"].as<String>();
    if (newState != gateState) { gateState = newState; Serial.println("Gate Command: " + gateState); lastInteraction = millis(); }
  }
}

void controlGate() {
  bool isOpenHit  = (digitalRead(GATE_OPEN_LIMIT)  == LOW);
  bool isCloseHit = (digitalRead(GATE_CLOSE_LIMIT) == LOW);

  // Debug: 1 = switch pressed, 0 = not pressed
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.printf("Gate: %-12s | OpenSW: %d | CloseSW: %d\n", gateState.c_str(), isOpenHit, isCloseHit);
  }

  if (gateState == "moving_open") {
    if (isOpenHit) {
      digitalWrite(GATE_IN1, LOW); digitalWrite(GATE_IN2, LOW); ledcWrite(GATE_ENA, 0);
      gateState = "open"; updateGateState("open");
    } else {
      digitalWrite(GATE_IN1, HIGH); digitalWrite(GATE_IN2, LOW); ledcWrite(GATE_ENA, gateSpeed);
    }
  }
  else if (gateState == "moving_close") {
    if (isCloseHit) {
      digitalWrite(GATE_IN1, LOW); digitalWrite(GATE_IN2, LOW); ledcWrite(GATE_ENA, 0);
      gateState = "closed"; updateGateState("closed");
    } else {
      digitalWrite(GATE_IN1, LOW); digitalWrite(GATE_IN2, HIGH); ledcWrite(GATE_ENA, gateSpeed);
    }
  }
  else {
    digitalWrite(GATE_IN1, LOW); digitalWrite(GATE_IN2, LOW); ledcWrite(GATE_ENA, 0);
  }
}

// ================= SETUP & LOOP =================
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0)); 
  
  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  lcdLine(0, "SmartNest");
  lcdLine(1, "Initializing...");

  display.begin(0x02, true);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 25); display.setTextSize(1); display.print("Connecting to WiFi...");
  display.display();

  dht.begin();
  
  initRelay(BATHROOM_RELAY_PIN); initRelay(BEDROOM_RELAY_PIN);
  initRelay(DINING_RELAY_PIN); initRelay(KITCHEN_RELAY_PIN); initRelay(LIVING_RELAY_PIN);

  pinMode(GATE_IN1, OUTPUT); pinMode(GATE_IN2, OUTPUT);
  pinMode(GATE_OPEN_LIMIT, INPUT_PULLUP); pinMode(GATE_CLOSE_LIMIT, INPUT_PULLUP);
  ledcAttach(GATE_ENA, 5000, 8); ledcWrite(GATE_ENA, 0);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov"); 
  loginSupabase();
  
  updateLcd(); 
  lastInteraction = millis(); 
  lastScreenChange = millis();
}

void loop() {
  controlGate();   // check limit switches first, before any slow internet request

  if (WiFi.status() == WL_CONNECTED) {
    if (accessToken.length() == 0) loginSupabase();
    if (millis() - lastStateCheck >= STATE_INTERVAL_MS) { lastStateCheck = millis(); readDeviceStates(); }
    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) { lastHeartbeat = millis(); sendHeartbeat(); }
    if (millis() - lastSensorRead >= SENSOR_INTERVAL_MS) { lastSensorRead = millis(); sendSensorReading(); }
    if (millis() - lastGateCheck >= 1000) { lastGateCheck = millis(); readGateState(); }
  }
  
  updateOLED(); // Draws Clock, Status, Starfield, or Robot
  delay(50); 
}