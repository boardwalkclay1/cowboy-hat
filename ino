/****************************************************
 *  COWBOY HAT OS – GO TIME EDITION (Speed Fusion)
 *  - ESP8266 + OLED + 4 Vibration Motors
 *  - Phone-driven awareness + radar + IMU gestures
 *  - Modes, sensitivity profiles, danger override
 *  - Walking speed from:
 *      1) Phone GPS speed (preferred)
 *      2) IMU step frequency
 *      3) IMU acceleration integration
 ****************************************************/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================== CONFIG ==================
const char* AP_SSID     = "CowboyHatOS-GoTime";
const char* AP_PASSWORD = "boardwalk";

// Vibration motors
#define VIB_FRONT  5   // D1
#define VIB_BACK   4   // D2
#define VIB_LEFT   0   // D3
#define VIB_RIGHT  2   // D4

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Web server
ESP8266WebServer server(80);

// ================== MODES ==================
enum HatMode {
  MODE_AWARENESS = 0,
  MODE_COMPASS   = 1,
  MODE_NAV       = 2,
  MODE_STEALTH   = 3,
  MODE_SYSTEM    = 4,
  MODE_WEATHER   = 5
};

HatMode currentMode = MODE_AWARENESS;

// ================== SENSITIVITY MODES ==================
enum SensitivityMode {
  SENSE_CROWD = 0,
  SENSE_BALANCED = 1,
  SENSE_HOMEALONE = 2
};

SensitivityMode safetyMode = SENSE_BALANCED;

// ================== STATE ==================
struct IMUState {
  float pitch;
  float roll;
  float yawRate;

  // Speed-related
  float forwardAccel;    // m/s^2 (estimated along direction of travel)
  float stepFrequency;   // steps per second
  bool  stepDetected;    // true when a step event is detected this frame
};

struct PhoneState {
  float headingDeg;
  String direction;
  String navInstruction;

  String objectType;
  String objectDir;
  String objectSpeed;    // "slow", "medium", "fast"
  bool   danger;

  // Weather
  String weatherSummary;
  float  tempC;
  float  windKph;

  // Speed from phone (GPS)
  bool  hasSpeed;
  float speedMps;        // meters per second
};

struct RadarState {
  bool motionFront;
  bool motionBack;
  int  strengthFront;
  int  strengthBack;
};

IMUState imu;
PhoneState phone;
RadarState radar;

// User speed (fused)
float userSpeedMps = 0.0;

// Gesture thresholds
const float NOD_THRESHOLD   = 15.0;
const float TILT_THRESHOLD  = 15.0;
const float SHAKE_THRESHOLD = 80.0;

// Danger vibration timing
unsigned long lastDangerVibe = 0;
const unsigned long DANGER_VIBE_INTERVAL = 400;

// Time tracking for accel integration
unsigned long lastSpeedUpdateMs = 0;

// ================== FORWARD DECLARATIONS ==================
void setupWiFiAP();
void setupWebServer();
void handleRoot();
void handleStateJson();
void handleModeChange();
void handlePhoneUpdate();
void handleSensitivityChange();
void handleNotFound();

void setupOLED();
void drawHUD();

void readIMU(IMUState &s);
void readRadar(RadarState &r);

void processGestures();
void onNod();
void onTiltLeft();
void onTiltRight();
void onShake();

void vibrateMotor(int pin, int ms);
void vibrateDirection(String dir, String speed);
void vibrateDangerLoop();

void computeUserSpeed();
float mapObjectSpeedToMps(const String &speedStr);

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(VIB_FRONT, OUTPUT);
  pinMode(VIB_BACK,  OUTPUT);
  pinMode(VIB_LEFT,  OUTPUT);
  pinMode(VIB_RIGHT, OUTPUT);

  Wire.begin();

  setupOLED();
  setupWiFiAP();
  setupWebServer();

  phone.headingDeg = 0;
  phone.direction = "N";
  phone.objectSpeed = "none";
  phone.danger = false;
  phone.weatherSummary = "Unknown";
  phone.tempC = 0;
  phone.windKph = 0;
  phone.hasSpeed = false;
  phone.speedMps = 0.0;

  lastSpeedUpdateMs = millis();
}

// ================== LOOP ==================
void loop() {
  server.handleClient();

  readIMU(imu);
  readRadar(radar);

  // Compute fused user walking speed
  computeUserSpeed();

  processGestures();

  // Danger override
  if (phone.danger) {
    vibrateDangerLoop();
  } else {
    // Determine object speed in m/s from label
    float objectSpeedMps = mapObjectSpeedToMps(phone.objectSpeed);
    float relativeSpeed = objectSpeedMps - userSpeedMps;

    // Sensitivity logic
    bool shouldAlert = false;

    if (safetyMode == SENSE_CROWD) {
      if (phone.objectSpeed == "fast") shouldAlert = true;
      if (phone.objectSpeed == "medium" && phone.objectDir.indexOf("back") >= 0) shouldAlert = true;
    }

    if (safetyMode == SENSE_BALANCED) {
      if (phone.objectSpeed != "none") shouldAlert = true;
    }

    if (safetyMode == SENSE_HOMEALONE) {
      if (phone.objectSpeed != "none") shouldAlert = true;
    }

    // Relative speed danger: if something is gaining on you fast
    const float RELATIVE_DANGER_THRESHOLD = 2.0; // m/s faster than you
    if (relativeSpeed > RELATIVE_DANGER_THRESHOLD && safetyMode != SENSE_CROWD) {
      phone.danger = true;
    }

    if (shouldAlert && phone.objectType.length() > 0) {
      vibrateDirection(phone.objectDir, phone.objectSpeed);
    }

    if (radar.motionBack && currentMode == MODE_AWARENESS) {
      vibrateMotor(VIB_BACK, 80);
    }
  }

  drawHUD();
}

// ================== WIFI + WEB ==================
void setupWiFiAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/state", HTTP_GET, handleStateJson);
  server.on("/mode", HTTP_POST, handleModeChange);
  server.on("/phone", HTTP_POST, handlePhoneUpdate);
  server.on("/sensitivity", HTTP_POST, handleSensitivityChange);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleRoot() {
  String html = F(
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<style>"
    "body{background:#020617;color:#e5e7eb;font-family:sans-serif;padding:16px;}"
    "h1{color:#38bdf8;margin-bottom:4px;}"
    "h2{color:#facc15;margin-top:0;}"
    "button{padding:8px 12px;margin:4px;border-radius:6px;border:none;background:#facc15;color:#111827;font-weight:600;}"
    "pre{background:#020617;border:1px solid #1f2937;padding:8px;border-radius:6px;}"
    "</style></head><body>"
    "<h1>Go Time Software</h1>"
    "<h2>Cowboy Hat OS</h2>"
    "<button onclick='setMode(0)'>Awareness</button>"
    "<button onclick='setMode(1)'>Compass</button>"
    "<button onclick='setMode(2)'>Navigation</button>"
    "<button onclick='setMode(3)'>Stealth</button>"
    "<button onclick='setMode(4)'>System</button>"
    "<button onclick='setMode(5)'>Weather</button>"
    "<h3>Sensitivity</h3>"
    "<button onclick='setSense(0)'>Crowd</button>"
    "<button onclick='setSense(1)'>Balanced</button>"
    "<button onclick='setSense(2)'>Home-Alone</button>"
    "<h3>State</h3><pre id='stateBox'></pre>"
    "<script>"
    "function fetchState(){fetch('/state').then(r=>r.json()).then(j=>{"
      "document.getElementById('stateBox').innerText=JSON.stringify(j,null,2);"
    "});}"
    "function setMode(m){fetch('/mode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'mode='+m}).then(fetchState);}"
    "function setSense(s){fetch('/sensitivity',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'sense='+s}).then(fetchState);}"
    "setInterval(fetchState,1000);fetchState();"
    "</script></body></html>"
  );
  server.send(200, "text/html", html);
}

void handleStateJson() {
  String json = "{";
  json += "\"mode\":" + String((int)currentMode) + ",";
  json += "\"sensitivity\":" + String((int)safetyMode) + ",";
  json += "\"danger\":" + String(phone.danger ? "true" : "false") + ",";
  json += "\"heading\":" + String(phone.headingDeg, 1) + ",";
  json += "\"direction\":\"" + phone.direction + "\",";
  json += "\"objectType\":\"" + phone.objectType + "\",";
  json += "\"objectDir\":\"" + phone.objectDir + "\",";
  json += "\"objectSpeed\":\"" + phone.objectSpeed + "\",";
  json += "\"weather\":\"" + phone.weatherSummary + "\",";
  json += "\"tempC\":" + String(phone.tempC, 1) + ",";
  json += "\"windKph\":" + String(phone.windKph, 1) + ",";
  json += "\"userSpeedMps\":" + String(userSpeedMps, 2) + ",";
  json += "\"phoneSpeedMps\":" + String(phone.speedMps, 2) + ",";
  json += "\"phoneHasSpeed\":" + String(phone.hasSpeed ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleModeChange() {
  if (server.hasArg("mode")) {
    int m = server.arg("mode").toInt();
    if (m >= 0 && m <= 5) currentMode = (HatMode)m;
  }
  server.send(200, "text/plain", "OK");
}

void handleSensitivityChange() {
  if (server.hasArg("sense")) {
    int s = server.arg("sense").toInt();
    if (s >= 0 && s <= 2) safetyMode = (SensitivityMode)s;
  }
  server.send(200, "text/plain", "OK");
}

void handlePhoneUpdate() {
  if (server.hasArg("heading"))    phone.headingDeg = server.arg("heading").toFloat();
  if (server.hasArg("direction"))  phone.direction = server.arg("direction");
  if (server.hasArg("nav"))        phone.navInstruction = server.arg("nav");
  if (server.hasArg("objType"))    phone.objectType = server.arg("objType");
  if (server.hasArg("objDir"))     phone.objectDir = server.arg("objDir");
  if (server.hasArg("objSpeed"))   phone.objectSpeed = server.arg("objSpeed");
  if (server.hasArg("danger"))     phone.danger = (server.arg("danger") == "1" || server.arg("danger") == "true");
  if (server.hasArg("weather"))    phone.weatherSummary = server.arg("weather");
  if (server.hasArg("temp"))       phone.tempC = server.arg("temp").toFloat();
  if (server.hasArg("wind"))       phone.windKph = server.arg("wind").toFloat();
  if (server.hasArg("speed")) {
    phone.speedMps = server.arg("speed").toFloat();
    phone.hasSpeed = true;
  }

  server.send(200, "text/plain", "PHONE UPDATE OK");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ================== OLED HUD ==================
String dirFromHeading(float h) {
  if (h < 22.5) return "N";
  if (h < 67.5) return "NE";
  if (h < 112.5) return "E";
  if (h < 157.5) return "SE";
  if (h < 202.5) return "S";
  if (h < 247.5) return "SW";
  if (h < 292.5) return "W";
  if (h < 337.5) return "NW";
  return "N";
}

void setupOLED() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
}

void drawHUD() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Danger override
  if (phone.danger) {
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("GO TIME");

    display.setTextSize(1);
    display.setCursor(0, 24);
    display.print("IMMEDIATE HAZARD");

    display.setCursor(0, 36);
    display.print(phone.objectType + " " + phone.objectDir);

    display.setCursor(0, 48);
    display.print("SPD: " + phone.objectSpeed);

    display.display();
    return;
  }

  switch (currentMode) {
    case MODE_AWARENESS: {
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print("GO TIME");

      display.setTextSize(3);
      display.setCursor(32, 20);
      display.print(phone.direction);

      display.setTextSize(1);
      display.setCursor(0, 52);
      display.print(phone.objectType + " " + phone.objectDir + " " + phone.objectSpeed);
      break;
    }

    case MODE_COMPASS: {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("COMPASS – GO TIME");

      display.setTextSize(2);
      display.setCursor(0, 16);
      display.printf("%3.0f%c", phone.headingDeg, 248);

      display.setTextSize(3);
      display.setCursor(40, 36);
      display.print(dirFromHeading(phone.headingDeg));
      break;
    }

    case MODE_NAV: {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("NAV – GO TIME");

      display.setTextSize(2);
      display.setCursor(0, 16);
      display.print(phone.navInstruction);

      display.setTextSize(1);
      display.setCursor(0, 48);
      display.print("Dir: " + phone.direction);
      break;
    }

    case MODE_STEALTH: {
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print("STEALTH");

      display.setTextSize(1);
      display.setCursor(0, 24);
      display.print("Haptics only.");
      break;
    }

    case MODE_SYSTEM: {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("SYSTEM – GO TIME");

      display.setCursor(0, 16);
      display.print("Sensitivity:");

      display.setCursor(0, 28);
      if (safetyMode == SENSE_CROWD) display.print("Crowd Mode");
      if (safetyMode == SENSE_BALANCED) display.print("Balanced Mode");
      if (safetyMode == SENSE_HOMEALONE) display.print("Home-Alone Mode");

      display.setCursor(0, 44);
      display.print("User v: ");
      display.print(userSpeedMps, 1);
      display.print(" m/s");
      break;
    }

    case MODE_WEATHER: {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("WEATHER – GO TIME");

      display.setTextSize(2);
      display.setCursor(0, 16);
      display.printf("%.1fC", phone.tempC);

      display.setTextSize(1);
      display.setCursor(0, 40);
      display.print(phone.weatherSummary);

      display.setCursor(0, 52);
      display.print("Wind: ");
      display.print(phone.windKph, 1);
      display.print("kph");
      break;
    }
  }

  display.display();
}

// ================== IMU + RADAR STUBS ==================
void readIMU(IMUState &s) {
  // TODO: replace with real IMU reads
  s.pitch = 0;
  s.roll = 0;
  s.yawRate = 0;

  // Speed-related stubs
  s.forwardAccel = 0;     // m/s^2
  s.stepFrequency = 0;    // steps per second
  s.stepDetected = false; // set true when you detect a step
}

void readRadar(RadarState &r) {
  // TODO: replace with real radar reads
  r.motionFront = false;
  r.motionBack = false;
  r.strengthFront = 0;
  r.strengthBack = 0;
}

// ================== GESTURE ENGINE ==================
void processGestures() {
  if (imu.pitch > NOD_THRESHOLD) onNod();
  if (imu.roll > TILT_THRESHOLD) onTiltRight();
  else if (imu.roll < -TILT_THRESHOLD) onTiltLeft();
  if (abs(imu.yawRate) > SHAKE_THRESHOLD) onShake();
}

void onNod() {
  // Select / confirm – customize per mode if needed
}

void onTiltLeft() {
  int m = (int)currentMode - 1;
  if (m < 0) m = 5;
  currentMode = (HatMode)m;
}

void onTiltRight() {
  int m = (int)currentMode + 1;
  if (m > 5) m = 0;
  currentMode = (HatMode)m;
}

void onShake() {
  currentMode = MODE_AWARENESS;
}

// ================== VIBRATION ENGINE ==================
void vibrateMotor(int pin, int ms) {
  digitalWrite(pin, HIGH);
  delay(ms);
  digitalWrite(pin, LOW);
}

void vibrateDirection(String dir, String speed) {
  if (phone.danger) return;

  int pulse = 80;
  if (speed == "medium") pulse = 150;
  else if (speed == "fast") pulse = 300;

  bool f = false, b = false, l = false, r = false;

  if (dir == "front") f = true;
  else if (dir == "back") b = true;
  else if (dir == "left") l = true;
  else if (dir == "right") r = true;
  else if (dir == "front-left") { f = true; l = true; }
  else if (dir == "front-right") { f = true; r = true; }
  else if (dir == "back-left") { b = true; l = true; }
  else if (dir == "back-right") { b = true; r = true; }

  if (f) vibrateMotor(VIB_FRONT, pulse);
  if (b) vibrateMotor(VIB_BACK,  pulse);
  if (l) vibrateMotor(VIB_LEFT,  pulse);
  if (r) vibrateMotor(VIB_RIGHT, pulse);
}

void vibrateDangerLoop() {
  unsigned long now = millis();
  if (now - lastDangerVibe >= DANGER_VIBE_INTERVAL) {
    lastDangerVibe = now;
    digitalWrite(VIB_FRONT, HIGH);
    digitalWrite(VIB_BACK,  HIGH);
    digitalWrite(VIB_LEFT,  HIGH);
    digitalWrite(VIB_RIGHT, HIGH);
    delay(120);
    digitalWrite(VIB_FRONT, LOW);
    digitalWrite(VIB_BACK,  LOW);
    digitalWrite(VIB_LEFT,  LOW);
    digitalWrite(VIB_RIGHT, LOW);
  }
}

// ================== SPEED FUSION ==================
void computeUserSpeed() {
  unsigned long now = millis();
  float dt = (now - lastSpeedUpdateMs) / 1000.0; // seconds
  if (dt <= 0) dt = 0.01;
  lastSpeedUpdateMs = now;

  // 1) Prefer phone GPS speed if available
  if (phone.hasSpeed) {
    userSpeedMps = phone.speedMps;
    return;
  }

  // 2) Use IMU step frequency if step detected / stable
  if (imu.stepFrequency > 0.1f) {
    // Approximate step length (m). You can calibrate this.
    const float STEP_LENGTH_M = 0.78f;
    userSpeedMps = imu.stepFrequency * STEP_LENGTH_M;
    return;
  }

  // 3) Fallback: integrate forward acceleration
  // Very rough, but better than nothing
  userSpeedMps += imu.forwardAccel * dt;

  // Clamp to non-negative
  if (userSpeedMps < 0) userSpeedMps = 0;
}

float mapObjectSpeedToMps(const String &speedStr) {
  if (speedStr == "slow")   return 1.0; // ~3.6 km/h
  if (speedStr == "medium") return 3.0; // ~10.8 km/h
  if (speedStr == "fast")   return 6.0; // ~21.6 km/h
  return 0.0;
}
