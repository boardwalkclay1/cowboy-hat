/****************************************************
 *  COWBOY HAT OS – GO TIME EDITION
 *  ESP8266 + OLED + 4 Vibration Motors
 *  Phone-driven awareness + radar + IMU gestures
 *  Modes:
 *    - Awareness
 *    - Compass
 *    - Navigation
 *    - Stealth
 *    - System
 *    - Weather
 *
 *  Sensitivity Levels:
 *    - Crowd Mode
 *    - Balanced Mode
 *    - Home-Alone Mode
 *
 *  Danger Override:
 *    - Continuous vibration
 *    - OLED shows GO TIME warning
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
};

struct PhoneState {
  float headingDeg;
  String direction;
  String navInstruction;

  String objectType;
  String objectDir;
  String objectSpeed;
  bool   danger;

  String weatherSummary;
  float  tempC;
  float  windKph;
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

// Gesture thresholds
const float NOD_THRESHOLD   = 15.0;
const float TILT_THRESHOLD  = 15.0;
const float SHAKE_THRESHOLD = 80.0;

// Danger vibration timing
unsigned long lastDangerVibe = 0;
const unsigned long DANGER_VIBE_INTERVAL = 400;

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
}

// ================== LOOP ==================
void loop() {
  server.handleClient();

  readIMU(imu);
  readRadar(radar);

  processGestures();

  // Danger override
  if (phone.danger) {
    vibrateDangerLoop();
  } else {
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
      shouldAlert = true;
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
  json += "\"windKph\":" + String(phone.windKph, 1);
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
  if (server.hasArg("danger"))     phone.danger = (server.arg("danger") == "1");
  if (server.hasArg("weather"))    phone.weatherSummary = server.arg("weather");
  if (server.hasArg("temp"))       phone.tempC = server.arg("temp").toFloat();
  if (server.hasArg("wind"))       phone.windKph = server.arg("wind").toFloat();

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
      display.print("Tilt L/R to change");
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
  s.pitch = 0;
  s.roll = 0;
  s.yawRate = 0;
}

void readRadar(RadarState &r) {
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
  // Select / confirm
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
  else if (dir == "back-right") { b = true
