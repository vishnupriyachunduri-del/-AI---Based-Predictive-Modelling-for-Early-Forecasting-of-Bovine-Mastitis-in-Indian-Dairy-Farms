/*
  shed_node.ino
  SMART DAIRY MASTITIS AI - 2 NODE DEMO

  Receives the exact 32-byte WearablePacket from wearable_node.ino
  and runs the optimized CatBoost model.

  The Milk ESP32 is NOT connected in this version.
  Therefore milk/rolling/deviation features use neutral training
  baseline values until the Milk Unit is added.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <vector>
#include <math.h>

// Implemented by mastitis_model.cpp
double ApplyCatboostModel(const std::vector<float>& floatFeatures);

// MUST exactly match wearable_node.ino
typedef struct WearablePacket {
  char cow_tag[16];
  float skin_temp_c;
  float activity_index;
  float rumination_min;
  float mobility_score;
} WearablePacket;

static_assert(sizeof(WearablePacket) == 32, "WearablePacket must be 32 bytes");

const float ALERT_THRESHOLD = 0.50f;

// Median values from the training dataset, used ONLY as placeholders
// while the Milk ESP32 is not connected.
const float BASE_MILK_TEMP       = 35.2230f;
const float BASE_MILK_PH         = 6.6660f;
const float BASE_MILK_YIELD      = 9.2400f;

const float BASE_ROLL_SKIN       = 38.2160f;
const float BASE_ROLL_ACTIVITY   = 59.1400f;
const float BASE_ROLL_RUMINATION = 453.3000f;
const float BASE_ROLL_MOBILITY   = 63.1200f;
const float BASE_ROLL_MILK_TEMP  = 35.2110f;
const float BASE_ROLL_MILK_PH    = 6.6610f;
const float BASE_ROLL_MILK_YIELD = 9.2600f;

const float BASE_DEV_SKIN        = 0.0045f;
const float BASE_DEV_ACTIVITY    = -0.2850f;
const float BASE_DEV_RUMINATION  = -1.0000f;
const float BASE_DEV_MOBILITY    = -0.2850f;
const float BASE_DEV_MILK_TEMP   = 0.0040f;
const float BASE_DEV_MILK_PH     = 0.0020f;
const float BASE_DEV_MILK_YIELD  = -0.0200f;

volatile bool packetReady = false;
WearablePacket latestPacket;
portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED;

float sigmoid(float x) {
  if (x >= 0.0f) {
    float z = expf(-x);
    return 1.0f / (1.0f + z);
  }
  float z = expf(x);
  return z / (1.0f + z);
}

// ESP32 Arduino Core 3.x callback
void onDataRecv(const esp_now_recv_info_t *info,
                const uint8_t *data,
                int len) {

  if (len != sizeof(WearablePacket)) {
    Serial.print("[Shed] Ignored packet: expected ");
    Serial.print(sizeof(WearablePacket));
    Serial.print(" bytes, received ");
    Serial.print(len);
    Serial.println(" bytes");
    return;
  }

  portENTER_CRITICAL(&packetMux);
  memcpy(&latestPacket, data, sizeof(WearablePacket));
  packetReady = true;
  portEXIT_CRITICAL(&packetMux);
}

// Exact feature order used when training/exporting the optimized model.
std::vector<float> buildModelFeatures(const WearablePacket &p) {

  std::vector<float> f;
  f.reserve(21);

  // 0-3: REAL wearable measurements
  f.push_back(p.skin_temp_c);
  f.push_back(p.activity_index);
  f.push_back(p.rumination_min);
  f.push_back(p.mobility_score);

  // 4-6: milk measurements - placeholders for 2-node demo
  f.push_back(BASE_MILK_TEMP);
  f.push_back(BASE_MILK_PH);
  f.push_back(BASE_MILK_YIELD);

  // 7-13: rolling values - placeholders for 2-node demo
  f.push_back(BASE_ROLL_SKIN);
  f.push_back(BASE_ROLL_ACTIVITY);
  f.push_back(BASE_ROLL_RUMINATION);
  f.push_back(BASE_ROLL_MOBILITY);
  f.push_back(BASE_ROLL_MILK_TEMP);
  f.push_back(BASE_ROLL_MILK_PH);
  f.push_back(BASE_ROLL_MILK_YIELD);

  // 14-20: deviation values - placeholders for 2-node demo
  f.push_back(BASE_DEV_SKIN);
  f.push_back(BASE_DEV_ACTIVITY);
  f.push_back(BASE_DEV_RUMINATION);
  f.push_back(BASE_DEV_MOBILITY);
  f.push_back(BASE_DEV_MILK_TEMP);
  f.push_back(BASE_DEV_MILK_PH);
  f.push_back(BASE_DEV_MILK_YIELD);

  return f;
}

void runInference(const WearablePacket &p) {

  std::vector<float> features = buildModelFeatures(p);

  double rawScore = ApplyCatboostModel(features);
  float probability = sigmoid((float)rawScore);
  float riskPercent = probability * 100.0f;

  Serial.println();
  Serial.println("========================================");
  Serial.println("       SMART DAIRY - SHED AI");
  Serial.println("========================================");

  Serial.print("Cow tag           : ");
  Serial.println(p.cow_tag);

  Serial.println();
  Serial.println("--- REAL WEARABLE DATA ---");

  Serial.print("Skin temp         : ");
  Serial.print(p.skin_temp_c, 2);
  Serial.println(" C");

  Serial.print("Activity index    : ");
  Serial.println(p.activity_index, 2);

  Serial.print("Rumination proxy  : ");
  Serial.println(p.rumination_min, 2);

  Serial.print("Mobility score    : ");
  Serial.println(p.mobility_score, 2);

  Serial.println();
  Serial.println("--- MILK DATA ---");
  Serial.println("Milk ESP32 not connected yet.");
  Serial.println("Using neutral training baselines.");

  Serial.println();
  Serial.println("--- CATBOOST RESULT ---");

  Serial.print("Raw model score   : ");
  Serial.println(rawScore, 6);

  Serial.print("Mastitis risk     : ");
  Serial.print(riskPercent, 2);
  Serial.println(" %");

  Serial.print("Decision          : ");
  if (probability >= ALERT_THRESHOLD) {
    Serial.println("HIGH RISK");
  } else {
    Serial.println("NORMAL / LOW RISK");
  }

  Serial.println("========================================");
}

void setup() {

  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" Smart Dairy Mastitis AI - Shed Node");
  Serial.println(" 2-NODE DEMO: WEARABLE + SHED");
  Serial.println("========================================");

  WiFi.mode(WIFI_STA);
  delay(300);

  Serial.print("Shed ESP MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW initialization failed!");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("ESP-NOW receiver ready.");
  Serial.println("Waiting for 32-byte wearable packets...");
  Serial.println("CatBoost model loaded automatically by Arduino.");
}

void loop() {

  WearablePacket p;
  bool hasPacket = false;

  portENTER_CRITICAL(&packetMux);

  if (packetReady) {
    memcpy(&p, &latestPacket, sizeof(WearablePacket));
    packetReady = false;
    hasPacket = true;
  }

  portEXIT_CRITICAL(&packetMux);

  if (hasPacket) {
    Serial.println("[Shed] Received wearable packet");
    runInference(p);
  }

  delay(10);
}
