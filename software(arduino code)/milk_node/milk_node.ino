/*
  milk_node.ino  --  MILK MEASUREMENT UNIT (ESP32 #2)
  ------------------------------------------------------
  Sensors on this board:
    - DS18B20 waterproof probe : milk_temp_c
    - HX711 + load cell        : milk_yield_kg
    - KY-039 heartbeat sensor (OPTIONAL): logged to Serial only

  NOTE:
    You don't have a pH sensor yet.
    milk_ph is therefore sent as -1.0.
    The Shed ESP will substitute the baseline pH value.

  ------------------------------------------------------
  WIRING
  ------------------------------------------------------

  DS18B20:
    VCC  -> 3V3
    GND  -> GND
    DATA -> GPIO4
    4.7k resistor between DATA and 3V3

  HX711:
    VCC -> 3V3 (or 5V depending on module)
    GND -> GND
    DT  -> GPIO32
    SCK -> GPIO33

  Load Cell:
    Red   -> E+
    Black -> E-
    White -> A-
    Green -> A+
    
    NOTE: Wire colors can vary between load cells.

  KY-039 heartbeat sensor (OPTIONAL):
    + -> 3V3
    - -> GND
    S -> GPIO34

  ------------------------------------------------------
  LIBRARIES
  ------------------------------------------------------

  Install:
    - OneWire
    - DallasTemperature
    - HX711

  ESP-NOW and WiFi are included with ESP32 Arduino Core.
*/

#include <WiFi.h>
#include <esp_now.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HX711.h>

// ======================================================
// CONFIGURATION
// ======================================================

// Shed ESP32 MAC address
// Your confirmed Shed MAC:
uint8_t SHED_ESP_MAC[] = {
  0x28, 0x05, 0xA5, 0x31, 0x5C, 0x94
};

const char* COW_TAG = "COW-DEMO";

// ======================================================
// PIN DEFINITIONS
// ======================================================

#define ONE_WIRE_PIN    4
#define HX711_DT_PIN    32
#define HX711_SCK_PIN   33
#define HEARTBEAT_PIN   34

// ======================================================
// LOAD CELL CALIBRATION
// ======================================================

// IMPORTANT:
// 1180.0 is only a placeholder.
// You must calibrate the load cell for accurate weight.
float LOADCELL_SCALE_FACTOR = 1180.0;

// ======================================================
// SENSOR OBJECTS
// ======================================================

OneWire oneWire(ONE_WIRE_PIN);

DallasTemperature milkTempSensor(&oneWire);

HX711 scale;

// ======================================================
// DATA PACKET
// ======================================================

// This packet is exactly 28 bytes:
// 16 bytes cow_tag
// 4 bytes milk_temp_c
// 4 bytes milk_ph
// 4 bytes milk_yield_kg

typedef struct MilkPacket {

  char cow_tag[16];

  float milk_temp_c;

  float milk_ph;

  float milk_yield_kg;

} MilkPacket;

MilkPacket packet;

// ======================================================
// ESP-NOW SEND CALLBACK
// ======================================================
//
// ESP32 Arduino Core 3.x requires:
//
// const wifi_tx_info_t *tx_info
//
// DO NOT change this back to:
// const uint8_t *mac_addr
//

void onDataSent(
  const wifi_tx_info_t *tx_info,
  esp_now_send_status_t status
) {

  Serial.print("ESP-NOW send status: ");

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED");
  }
}

// ======================================================
// LOAD CELL CALIBRATION
// ======================================================
//
// Current value is only a placeholder.
//
// For proper calibration:
//
// 1. Set LOADCELL_SCALE_FACTOR = 1.0
//
// 2. Upload the program.
//
// 3. Open Serial Monitor.
//
// 4. Make sure nothing is on the load cell.
//
// 5. Tare the load cell.
//
// 6. Put a known weight, for example 1 kg.
//
// 7. Determine the raw reading.
//
// 8. Calculate:
//
//      SCALE FACTOR = raw reading / known weight
//
// Example:
//
// Raw reading = 9120
// Known weight = 1 kg
//
// SCALE FACTOR = 9120 / 1
//               = 9120
//
// Then change:
//
// float LOADCELL_SCALE_FACTOR = 9120.0;
//

void calibrate() {

  scale.set_scale(LOADCELL_SCALE_FACTOR);

  Serial.println("Taring load cell...");

  scale.tare();

  Serial.println("Load cell tare complete.");
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("      MILK NODE - ESP32 #2");
  Serial.println("================================");

  // ----------------------------------------------------
  // DS18B20
  // ----------------------------------------------------

  milkTempSensor.begin();

  Serial.println("DS18B20 initialized.");

  // ----------------------------------------------------
  // HX711
  // ----------------------------------------------------

  scale.begin(
    HX711_DT_PIN,
    HX711_SCK_PIN
  );

  Serial.println("HX711 initialized.");

  calibrate();

  // ----------------------------------------------------
  // KY-039
  // ----------------------------------------------------

  pinMode(
    HEARTBEAT_PIN,
    INPUT
  );

  // ----------------------------------------------------
  // WiFi
  // ----------------------------------------------------

  WiFi.mode(WIFI_STA);

  Serial.print("Milk ESP32 MAC: ");
  Serial.println(WiFi.macAddress());

  // ----------------------------------------------------
  // ESP-NOW INITIALIZATION
  // ----------------------------------------------------

  if (esp_now_init() != ESP_OK) {

    Serial.println("ESP-NOW initialization FAILED!");

    return;
  }

  Serial.println("ESP-NOW initialized.");

  // ----------------------------------------------------
  // REGISTER SEND CALLBACK
  // ----------------------------------------------------

  esp_now_register_send_cb(onDataSent);

  // ----------------------------------------------------
  // ADD SHED ESP32 AS PEER
  // ----------------------------------------------------

  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    SHED_ESP_MAC,
    6
  );

  peerInfo.channel = 0;

  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {

    Serial.println(
      "Failed to add Shed ESP32 as peer!"
    );

  } else {

    Serial.println(
      "Shed ESP32 added as ESP-NOW peer."
    );
  }

  // ----------------------------------------------------
  // READY
  // ----------------------------------------------------

  Serial.println();
  Serial.println("Milk Node ready.");
  Serial.println("DS18B20 -> Milk temperature");
  Serial.println("HX711   -> Milk yield");
  Serial.println("pH      -> Not installed");
  Serial.println("KY-039  -> Optional heartbeat");
  Serial.println();
}

// ======================================================
// MAIN LOOP
// ======================================================

void loop() {

  // ----------------------------------------------------
  // COW TAG
  // ----------------------------------------------------

  strncpy(
    packet.cow_tag,
    COW_TAG,
    sizeof(packet.cow_tag)
  );

  // Make sure string is terminated
  packet.cow_tag[
    sizeof(packet.cow_tag) - 1
  ] = '\0';

  // ----------------------------------------------------
  // MILK TEMPERATURE
  // ----------------------------------------------------

  milkTempSensor.requestTemperatures();

  float temp =
    milkTempSensor.getTempCByIndex(0);

  if (temp == DEVICE_DISCONNECTED_C) {

    // Fallback value
    packet.milk_temp_c = 35.2;

    Serial.println(
      "WARNING: DS18B20 disconnected."
    );

  } else {

    packet.milk_temp_c = temp;
  }

  // ----------------------------------------------------
  // MILK YIELD / LOAD CELL
  // ----------------------------------------------------

  float measuredWeight =
    scale.get_units(10);

  // Prevent negative values
  packet.milk_yield_kg =
    max(measuredWeight, 0.0f);

  // ----------------------------------------------------
  // pH SENSOR
  // ----------------------------------------------------
  //
  // No pH sensor installed yet.
  //
  // -1.0 tells the Shed that pH is unavailable.
  //

  packet.milk_ph = -1.0;

  // ----------------------------------------------------
  // OPTIONAL KY-039 HEARTBEAT
  // ----------------------------------------------------

  int heartbeatRaw =
    analogRead(HEARTBEAT_PIN);

  // ----------------------------------------------------
  // SERIAL MONITOR
  // ----------------------------------------------------

  Serial.println();
  Serial.println("----------- MILK DATA -----------");

  Serial.printf(
    "Cow Tag       : %s\n",
    packet.cow_tag
  );

  Serial.printf(
    "Milk Temp     : %.2f °C\n",
    packet.milk_temp_c
  );

  Serial.printf(
    "Milk Yield    : %.2f kg\n",
    packet.milk_yield_kg
  );

  Serial.printf(
    "Milk pH       : %.2f (not installed)\n",
    packet.milk_ph
  );

  Serial.printf(
    "Heartbeat Raw : %d\n",
    heartbeatRaw
  );

  Serial.println("--------------------------------");

  // ----------------------------------------------------
  // SEND DATA TO SHED ESP32
  // ----------------------------------------------------

  esp_err_t result = esp_now_send(
    SHED_ESP_MAC,
    (uint8_t*)&packet,
    sizeof(packet)
  );

  if (result == ESP_OK) {

    Serial.println(
      "Milk packet sent to Shed."
    );

  } else {

    Serial.print(
      "ESP-NOW send error: "
    );

    Serial.println(result);
  }

  // ----------------------------------------------------
  // WAIT 30 SECONDS
  // ----------------------------------------------------
  //
  // For demonstration:
  // every 30 seconds
  //
  // Real deployment:
  // once per milking session / appropriate interval
  //

  delay(30000);
}