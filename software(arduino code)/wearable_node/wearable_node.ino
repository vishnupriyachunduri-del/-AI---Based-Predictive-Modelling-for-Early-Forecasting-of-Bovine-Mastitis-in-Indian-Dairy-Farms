/*
  wearable_node.ino  --  ANIMAL WEARABLE UNIT (ESP32 #1)
  --------------------------------------------------------
  Sensors on this board:
    - MPU-6050            : activity index (via I2C, direct register read,
                             no extra library needed)
    - NEO-6M GPS          : mobility score (distance moved between readings)
    - DHT11               : STAND-IN for skin/contact temperature.
                             HONESTY NOTE: DHT11 measures ambient air temp,
                             NOT contact temp like the MAX30205 specified in
                             the brief. Say this plainly in your demo -- it's
                             a placeholder until a real contact probe (e.g.
                             MAX30205) is added.
    - KY-038 sound sensor (OPTIONAL, red board): logged as a rough acoustic
      "chewing/rumination" proxy. This is a genuine technique used in real
      precision-livestock research (acoustic rumination monitoring), but a
      simple comparator threshold only gives crude pulse counts, not real
      acoustic analysis. Treat it as a bonus signal, not a validated one.

  Sends a WEARABLE_PACKET to the Shed ESP via ESP-NOW every ~30 seconds
  (shortened from "once per day" for demo purposes -- see README).

  ---- WIRING (ESP32 DevKit V1, 30-pin) ----
  MPU-6050:
    VCC -> 3V3        GND -> GND
    SCL -> GPIO22     SDA -> GPIO21
  NEO-6M GPS module:
    VCC -> 3V3 (or 5V if your GPS board wants it - check silkscreen)
    GND -> GND
    TX  -> GPIO16 (ESP32 RX2)
    RX  -> GPIO17 (ESP32 TX2)   [optional, GPS rarely needs data sent to it]
  DHT11 module (3-pin breakout, has built-in pull-up):
    + -> 3V3   - -> GND   OUT -> GPIO4
  KY-038 sound sensor (OPTIONAL):
    + -> 3V3   G -> GND   D0 -> GPIO27   (A0 unused)

  ---- LIBRARIES TO INSTALL (Arduino IDE Library Manager) ----
    - "TinyGPSPlus" by Mikal Hart
    - "DHT sensor library" by Adafruit  (+ its dependency "Adafruit Unified Sensor")
    - ESP-NOW and Wire are built into the ESP32 board package, no install needed.

  ---- BEFORE UPLOADING ----
  1. Set SHED_ESP_MAC below to the Shed ESP's actual MAC address.
     Get it by uploading this one-liner sketch to the Shed board first:
        void setup(){ Serial.begin(115200); Serial.println(WiFi.macAddress()); }
     Copy the printed address into SHED_ESP_MAC on both wearable_node.ino
     and milk_node.ino.
  2. Board setting: Tools > Board > ESP32 Dev Module.
*/

#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <TinyGPSPlus.h>
#include <DHT.h>
#include <HardwareSerial.h>

// ---------------- CONFIG ----------------
uint8_t SHED_ESP_MAC[] = {0x28, 0x05, 0xA5, 0x31, 0x5C, 0x94};  // <-- SET THIS
const char* COW_TAG = "COW-DEMO";  // change per-animal if you build more than one

#define DHTPIN 4
#define DHTTYPE DHT11
#define SOUND_D0_PIN 27
#define MPU6050_ADDR 0x68

DHT dht(DHTPIN, DHTTYPE);
TinyGPSPlus gps;
HardwareSerial GPSSerial(2);  // UART2: RX=16, TX=17

// Struct sent over ESP-NOW -- MUST match the struct in shed_node.ino exactly
typedef struct WearablePacket {
  char cow_tag[16];
  float skin_temp_c;      // DHT11 proxy - see honesty note above
  float activity_index;   // 0-100ish, derived from MPU6050 accel variance
  float rumination_min;   // proxy count from sound sensor, scaled
  float mobility_score;   // 0-100ish, derived from GPS distance moved
} WearablePacket;

WearablePacket packet;

double lastLat = 0, lastLon = 0;
bool haveLastFix = false;
unsigned long soundPulseCount = 0;

// ---------------- ONLY CHANGE IS HERE ----------------
// Compatible with your newer ESP32 core
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAILED");
}

// ---- Minimal direct-register MPU6050 read (no extra library needed) ----
void mpu6050_init() {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // wake up the sensor
  Wire.endTransmission(true);
}

void mpu6050_readAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);  // ACCEL_XOUT_H
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 6, true);
  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();
  ax = rawX / 16384.0;  // convert to g (±2g range default)
  ay = rawY / 16384.0;
  az = rawZ / 16384.0;
}

// Sample accel for ~2 seconds, return a 0-100-ish "activity index" based
// on how much the magnitude of acceleration varies (movement/jostling).
// A stationary board reads near 0; shaking it by hand reads high.
float measureActivityIndex() {
  const int N = 40;
  float sumMag = 0, sumMagSq = 0;
  for (int i = 0; i < N; i++) {
    float ax, ay, az;
    mpu6050_readAccel(ax, ay, az);
    float mag = sqrt(ax * ax + ay * ay + az * az);
    sumMag += mag;
    sumMagSq += mag * mag;
    delay(50);  // ~2 seconds total
  }
  float mean = sumMag / N;
  float variance = (sumMagSq / N) - (mean * mean);
  float activity = constrain(variance * 2000.0, 0, 100);  // scaled for demo
  return activity;
}

// Read GPS for a few seconds, compute distance moved since last fix
// (haversine), scale into a 0-100-ish "mobility score" for the demo.
float measureMobilityScore() {
  unsigned long start = millis();
  while (millis() - start < 3000) {
    while (GPSSerial.available() > 0) {
      gps.encode(GPSSerial.read());
    }
  }
  if (!gps.location.isValid()) {
    Serial.println("GPS: no fix yet (normal indoors) - returning neutral mobility score");
    return 50.0;  // neutral fallback so the demo doesn't stall indoors
  }
  double lat = gps.location.lat();
  double lon = gps.location.lng();
  float mobility = 50.0;
  if (haveLastFix) {
    double distMeters = TinyGPSPlus::distanceBetween(lastLat, lastLon, lat, lon);
    mobility = constrain(distMeters * 5.0, 0, 100);  // scaled for demo
  }
  lastLat = lat;
  lastLon = lon;
  haveLastFix = true;
  return mobility;
}

// Count sound-sensor digital pulses over ~2s, scale into a
// rumination-minutes-style number for the demo (NOT a real minute count).
float measureRuminationProxy() {
  soundPulseCount = 0;
  unsigned long start = millis();
  int lastState = digitalRead(SOUND_D0_PIN);
  while (millis() - start < 2000) {
    int state = digitalRead(SOUND_D0_PIN);
    if (state == LOW && lastState == HIGH) soundPulseCount++;  // falling edge = sound event
    lastState = state;
    delayMicroseconds(500);
  }
  // Scale pulse count into a plausible "rumination minutes" range for demo
  float ruminationProxy = 460.0 - constrain(soundPulseCount * 3.0, 0, 200);
  return ruminationProxy;
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  mpu6050_init();
  dht.begin();
  GPSSerial.begin(9600, SERIAL_8N1, 16, 17);
  pinMode(SOUND_D0_PIN, INPUT);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, SHED_ESP_MAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add Shed ESP as peer");
  }

  Serial.println("Wearable node ready.");
}

void loop() {
  strncpy(packet.cow_tag, COW_TAG, sizeof(packet.cow_tag));

  float dhtTemp = dht.readTemperature();
  packet.skin_temp_c = isnan(dhtTemp) ? 38.2 : dhtTemp;  // DHT11 proxy, see note at top

  packet.activity_index = measureActivityIndex();
  packet.rumination_min = measureRuminationProxy();
  packet.mobility_score = measureMobilityScore();

  Serial.printf("[Wearable] temp=%.2f activity=%.1f rumination=%.1f mobility=%.1f\n",
                packet.skin_temp_c, packet.activity_index,
                packet.rumination_min, packet.mobility_score);

  esp_err_t result = esp_now_send(SHED_ESP_MAC, (uint8_t *)&packet, sizeof(packet));
  if (result != ESP_OK) Serial.println("Error sending wearable packet");

  delay(30000);  // every 30s for demo; real deployment would be much longer
}