#include <WiFi.h>
#include <FirebaseESP32.h>

// ── YOUR DETAILS ────────────────────────────────────────────
#define WIFI_SSID  "ESP32"
#define WIFI_PASS  "1234567890"
#define FB_HOST    "https://smart-energy-monitor-842e9-default-rtdb.firebaseio.com"
#define FB_AUTH    "RT43Egf9YJEIHn1Rsfjh5Xl9t2HdNuc6PrgC0IIy"

// ── PINS ─────────────────────────────────────────────────────
#define VOLT_PIN  35
#define CURR_PIN  34
#define RELAY1    26   // Bulb 1
#define RELAY2    27   // Bulb 2

// ── RELAY: LOW=ON, HIGH=OFF ──────────────────────────────────
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// ── CALIBRATION ──────────────────────────────────────────────
float VCAL       = 4.86;   // adjust until voltage shows ~220V
float ACS_SENS   = 0.185;  // ACS712-5A

// ── SETTINGS ─────────────────────────────────────────────────
#define SAMPLES      1000
#define COST_PER_KWH 7.5

// ── FIREBASE ─────────────────────────────────────────────────
FirebaseData   fbd;
FirebaseConfig fbc;
FirebaseAuth   fba;

// ── VARIABLES ────────────────────────────────────────────────
float  voltage  = 0;
float  current  = 0;
float  power    = 0;
float  energy   = 0;
float  cost     = 0;
float  acsZero  = 0;
float  pLimit   = 500;

bool   bulb1    = false;
bool   bulb2    = false;
bool   alerted  = false;

unsigned long tFB     = 0;
unsigned long tPrint  = 0;
unsigned long tEnergy = 0;

// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n==== Smart Energy Monitor ====");

  // Relay setup
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY1, RELAY_OFF);
  digitalWrite(RELAY2, RELAY_OFF);
  Serial.println("Relays OFF");

  // Test relay — should hear 2 clicks
  Serial.println("Testing relays...");
  digitalWrite(RELAY1, RELAY_ON);  delay(800);
  digitalWrite(RELAY1, RELAY_OFF); delay(400);
  digitalWrite(RELAY2, RELAY_ON);  delay(800);
  digitalWrite(RELAY2, RELAY_OFF);
  Serial.println("Relay test done (heard 2 clicks?)");

  // ADC
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  // Calibrate ACS712
  Serial.println("Calibrating... remove AC load for 3s");
  delay(3000);
  long sum = 0;
  for (int i = 0; i < 2000; i++) {
    sum += analogRead(CURR_PIN);
    delayMicroseconds(50);
  }
  acsZero = (sum / 2000.0) * (3.3 / 4095.0);
  Serial.print("ACS zero = "); Serial.println(acsZero, 4);

  // WiFi
  Serial.print("Connecting WiFi: "); Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 40) {
    delay(500); Serial.print("."); t++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK! IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi FAILED — check hotspot!");
  }

  // Firebase
  fbc.host = FB_HOST;
  fbc.signer.tokens.legacy_token = FB_AUTH;
  Firebase.begin(&fbc, &fba);
  Firebase.reconnectWiFi(true);

  // Set defaults in Firebase
  Firebase.setBool (fbd, "/bulb/b1",    false);
  Firebase.setBool (fbd, "/bulb/b2",    false);
  Firebase.setFloat(fbd, "/set/limit",  pLimit);
  Firebase.setBool (fbd, "/alert/on",   false);
  Serial.println("Firebase OK");
  Serial.println("System Running!\n");

  tEnergy = millis();
}

// ═══════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // Read sensors
  voltage = readV();
  current = readI();
  power   = voltage * current;

  // Energy
  float hrs = (now - tEnergy) / 3600000.0;
  energy   += (power / 1000.0) * hrs;
  cost      = energy * COST_PER_KWH;
  tEnergy   = now;

  // Serial print every 1s
  if (now - tPrint >= 1000) {
    Serial.println("----------------------------");
    Serial.print("V: "); Serial.print(voltage,1); Serial.print("V  ");
    Serial.print("I: "); Serial.print(current,3); Serial.print("A  ");
    Serial.print("P: "); Serial.print(power,1);   Serial.println("W");
    Serial.print("E: "); Serial.print(energy,5);  Serial.print("kWh  ");
    Serial.print("Rs."); Serial.println(cost,2);
    Serial.print("Bulb1:"); Serial.print(bulb1?"ON ":"OFF ");
    Serial.print("Bulb2:"); Serial.println(bulb2?"ON":"OFF");
    tPrint = now;
  }

  // Firebase every 2 seconds (faster response)
  if (now - tFB >= 2000) {
    if (WiFi.status() == WL_CONNECTED) {
      // Upload sensor data
      Firebase.setFloat(fbd,  "/sensor/voltage", voltage);
      Firebase.setFloat(fbd,  "/sensor/current", current);
      Firebase.setFloat(fbd,  "/sensor/power",   power);
      Firebase.setFloat(fbd,  "/sensor/energy",  energy);
      Firebase.setFloat(fbd,  "/sensor/cost",    cost);
      Firebase.setBool (fbd,  "/sensor/b1",      bulb1);
      Firebase.setBool (fbd,  "/sensor/b2",      bulb2);
      Firebase.setString(fbd, "/sensor/time",    String(now/1000));

      // Read bulb commands from dashboard
      if (Firebase.getBool(fbd, "/bulb/b1")) {
        bool v = fbd.boolData();
        if (v != bulb1) {
          bulb1 = v;
          digitalWrite(RELAY1, bulb1 ? RELAY_ON : RELAY_OFF);
          Serial.println(bulb1 ? ">>> Bulb 1 ON" : ">>> Bulb 1 OFF");
        }
      }
      if (Firebase.getBool(fbd, "/bulb/b2")) {
        bool v = fbd.boolData();
        if (v != bulb2) {
          bulb2 = v;
          digitalWrite(RELAY2, bulb2 ? RELAY_ON : RELAY_OFF);
          Serial.println(bulb2 ? ">>> Bulb 2 ON" : ">>> Bulb 2 OFF");
        }
      }

      // Power limit
      if (Firebase.getFloat(fbd, "/set/limit")) {
        pLimit = fbd.floatData();
      }

      // Alert
      if (power > pLimit && !alerted) {
        Firebase.setBool(fbd, "/alert/on", true);
        alerted = true;
        Serial.println("ALERT: power exceeded!");
      }
      if (power <= pLimit && alerted) {
        Firebase.setBool(fbd, "/alert/on", false);
        alerted = false;
      }

      Serial.println("Firebase OK");
    } else {
      Serial.println("WiFi lost, reconnecting...");
      WiFi.reconnect();
    }
    tFB = now;
  }

  delay(50);
}

// ═══════════════════════════════════════════════════════════
float readV() {
  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(VOLT_PIN); delayMicroseconds(40);
  }
  long off = sum / SAMPLES;
  long sq  = 0;
  for (int i = 0; i < SAMPLES; i++) {
    long s = analogRead(VOLT_PIN) - off;
    sq += s * s; delayMicroseconds(40);
  }
  float v = sqrt((float)sq / SAMPLES) * (3.3 / 4095.0) * VCAL * 100.0;
  return (v < 5.0) ? 0.0 : v;
}

float readI() {
  long sq = 0;
  for (int i = 0; i < SAMPLES; i++) {
    float v   = analogRead(CURR_PIN) * (3.3 / 4095.0);
    float ins = (v - acsZero) / ACS_SENS;
    sq += (long)(ins * ins * 10000.0);
    delayMicroseconds(40);
  }
  float r = sqrt((float)sq / SAMPLES / 10000.0);
  return (r < 0.05) ? 0.0 : r;
}
