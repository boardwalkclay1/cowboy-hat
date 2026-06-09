#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================== CONFIG ==================
const char* AP_SSID     = "CowboyHatOS-GoTime";
const char* AP_PASSWORD = "boardwalk";

const char* UPLOAD_USER = "admin";
const char* UPLOAD_PASS = "cowboy123";

// ESP32‑C3 safe pins
#define VIB_FRONT  4
#define VIB_BACK   5
#define VIB_LEFT   6
#define VIB_RIGHT  7
#define OLED_SDA   8
#define OLED_SCL   9

WebServer server(80);

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
  float forwardAccel;
  float stepFrequency;
  bool  stepDetected;
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
  bool   hasSpeed;
  float  speedMps;
};

IMUState imu;
PhoneState phone;

float userSpeedMps = 0.0;
unsigned long lastSpeedUpdateMs = 0;
unsigned long lastDangerVibe = 0;

const float NOD_THRESHOLD   = 15.0;
const float TILT_THRESHOLD  = 15.0;
const float SHAKE_THRESHOLD = 80.0;
const unsigned long DANGER_VIBE_INTERVAL = 400;

// ================== OLED ==================
Adafruit_SSD1306 display(128, 64, &Wire, -1);

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

// ================== VIBRATION ==================
void vibrateMotor(int pin, int ms) {
  digitalWrite(pin, HIGH);
  delay(ms);
  digitalWrite(pin, LOW);
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

void vibrateDirection(String dir, String speed) {
  if (phone.danger) return;

  int pulse = 80;
  if (speed == "medium") pulse = 150;
  if (speed == "fast")   pulse = 300;

  if (dir == "front") vibrateMotor(VIB_FRONT, pulse);
  if (dir == "back")  vibrateMotor(VIB_BACK,  pulse);
  if (dir == "left")  vibrateMotor(VIB_LEFT,  pulse);
  if (dir == "right") vibrateMotor(VIB_RIGHT, pulse);
}

// ================== IMU STUB ==================
void readIMU(IMUState &s) {
  s.pitch = 0;
  s.roll = 0;
  s.yawRate = 0;
  s.forwardAccel = 0;
  s.stepFrequency = 0;
  s.stepDetected = false;
}

// ================== GESTURES ==================
void onNod() {}
void onTiltLeft()  { currentMode = (HatMode)((currentMode + 5) % 6); }
void onTiltRight() { currentMode = (HatMode)((currentMode + 1) % 6); }
void onShake()     { currentMode = MODE_AWARENESS; }

void processGestures() {
  if (imu.pitch > NOD_THRESHOLD) onNod();
  if (imu.roll > TILT_THRESHOLD) onTiltRight();
  else if (imu.roll < -TILT_THRESHOLD) onTiltLeft();
  if (abs(imu.yawRate) > SHAKE_THRESHOLD) onShake();
}

// ================== SPEED FUSION ==================
void computeUserSpeed() {
  unsigned long now = millis();
  float dt = (now - lastSpeedUpdateMs) / 1000.0;
  if (dt <= 0) dt = 0.01;
  lastSpeedUpdateMs = now;

  if (phone.hasSpeed) {
    userSpeedMps = phone.speedMps;
    return;
  }

  if (imu.stepFrequency > 0.1f) {
    userSpeedMps = imu.stepFrequency * 0.78f;
    return;
  }

  userSpeedMps += imu.forwardAccel * dt;
  if (userSpeedMps < 0) userSpeedMps = 0;
}

float mapObjectSpeedToMps(const String &speedStr) {
  if (speedStr == "slow")   return 1.0;
  if (speedStr == "medium") return 3.0;
  if (speedStr == "fast")   return 6.0;
  return 0.0;
}

// ================== HUD ==================
void drawHUD() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

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
    case MODE_AWARENESS:
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

    case MODE_COMPASS:
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

    case MODE_NAV:
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

    case MODE_STEALTH:
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print("STEALTH");
      display.setTextSize(1);
      display.setCursor(0, 24);
      display.print("Haptics only.");
      break;

    case MODE_SYSTEM:
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

    case MODE_WEATHER:
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

  display.display();
}

// ================== WEB HANDLERS ==================
void handleRoot() {
  File f = SPIFFS.open("/index.html", "r");
  if (!f) {
    server.send(404, "text/plain", "index.html missing");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleState() {
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

void handleMode() {
  if (server.hasArg("mode")) {
    currentMode = (HatMode)server.arg("mode").toInt();
  }
  server.send(200, "text/plain", "OK");
}

void handleSensitivity() {
  if (server.hasArg("sense")) {
    safetyMode = (SensitivityMode)server.arg("sense").toInt();
  }
  server.send(200, "text/plain", "OK");
}

void handlePhone() {
  auto get = [&](const char* name){
    return server.hasArg(name) ? server.arg(name) : String("");
  };

  String v;

  v = get("heading");    if (v.length()) phone.headingDeg = v.toFloat();
  v = get("direction");  if (v.length()) phone.direction = v;
  v = get("nav");        if (v.length()) phone.navInstruction = v;
  v = get("objType");    if (v.length()) phone.objectType = v;
  v = get("objDir");     if (v.length()) phone.objectDir = v;
  v = get("objSpeed");   if (v.length()) phone.objectSpeed = v;
  v = get("danger");     if (v.length()) phone.danger = (v == "1" || v == "true");
  v = get("weather");    if (v.length()) phone.weatherSummary = v;
  v = get("temp");       if (v.length()) phone.tempC = v.toFloat();
  v = get("wind");       if (v.length()) phone.windKph = v.toFloat();

  v = get("speed");
  if (v.length()) {
    phone.speedMps = v.toFloat();
    phone.hasSpeed = true;
  }

  server.send(200, "text/plain", "PHONE UPDATE OK");
}

// ================== UPLOAD ==================
void handleUploadPage() {
  if (!server.authenticate(UPLOAD_USER, UPLOAD_PASS))
    return server.requestAuthentication();

  File f = SPIFFS.open("/upload.html", "r");
  if (!f) {
    server.send(500, "text/plain", "upload.html missing");
    return;
  }

  server.streamFile(f, "text/html");
  f.close();
}

void handleFileUpload() {
  if (!server.authenticate(UPLOAD_USER, UPLOAD_PASS))
    return server.requestAuthentication();

  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    if (SPIFFS.exists(filename)) SPIFFS.remove(filename);
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    File f = SPIFFS.open("/" + upload.filename, FILE_APPEND);
    if (f) {
      f.write(upload.buf, upload.currentSize);
      f.close();
    }
  }

  if (upload.status == UPLOAD_FILE_END) {
    server.send(200, "text/plain", "Upload complete.");
  }
}

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(VIB_FRONT, OUTPUT);
  pinMode(VIB_BACK,  OUTPUT);
  pinMode(VIB_LEFT,  OUTPUT);
  pinMode(VIB_RIGHT, OUTPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  setupOLED();

  SPIFFS.begin(true);

  // Create upload.html if missing
  if (!SPIFFS.exists("/upload.html")) {
    File f = SPIFFS.open("/upload.html", FILE_WRITE);
    f.print(
      "<html><body>"
      "<h2>Cowboy Hat OS Upload</h2>"
      "<form method='POST' action='/upload' enctype='multipart/form-data'>"
      "<input type='file' name='data'><br><br>"
      "<input type='submit' value='Upload'>"
      "</form>"
      "</body></html>"
    );
    f.close();
  }

  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/state", HTTP_GET, handleState);
  server.on("/mode", HTTP_POST, handleMode);
  server.on("/sensitivity", HTTP_POST, handleSensitivity);
  server.on("/phone", HTTP_POST, handlePhone);

  server.on("/upload", HTTP_GET, handleUploadPage);
  server.on("/upload", HTTP_POST, [](){}, handleFileUpload);

  server.begin();
}

// ================== LOOP ==================
void loop() {
  server.handleClient();

  readIMU(imu);
  processGestures();
  computeUserSpeed();

  if (phone.danger) vibrateDangerLoop();
  else vibrateDirection(phone.objectDir, phone.objectSpeed);

  drawHUD();
}
