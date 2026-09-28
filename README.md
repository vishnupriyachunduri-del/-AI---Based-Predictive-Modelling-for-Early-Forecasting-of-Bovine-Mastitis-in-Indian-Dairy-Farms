# Hardware Demo — 3-ESP32 Mastitis Early-Warning System

Matches your architecture: Animal Wearable + Milk Measurement Unit + Shed ESP (Central Edge AI).

## What you actually have vs. the full architecture

| Architecture component | Your hardware | Status |
|---|---|---|
| MAX30205 contact temp | DHT11 (ambient temp) | **Placeholder** — say this in your demo |
| MPU6050 activity/rumination | MPU-6050 | Matches |
| NEO-6M GPS mobility | NEO-6M + antenna | Matches |
| DS18B20 milk temp | DS18B20 waterproof probe | Matches |
| PHSENS pH sensor | *(none)* |  — firmware handles this gracefully, see below |
| Load Cell + HX711 yield | HX711 + D-ring load cell | Matches |
| ESP32-CAM (QR + CMT vision) | *(none)* | Not built this round — fine, out of scope for this demo |

## How many ESP32 boards you need

- **3 boards (recommended)**: exactly matches your architecture diagram — Wearable, Milk Unit, Shed ESP as three separate nodes talking over ESP-NOW. Best for convincing judges the architecture is real, not just a slide.
- **2 boards (fallback)**: merge `milk_node.ino`'s sensor-reading code directly into `shed_node.ino` (skip the ESP-NOW hop for milk data — call the DS18B20/HX711 read functions directly in the Shed ESP's loop instead of waiting for a packet). The wearable stays separate since it needs to be physically on the "animal."

## Folder structure (each folder is a separate Arduino sketch)

```
hardware-demo/
├── wearable_node/
│   └── wearable_node.ino     -- upload to ESP32 #1
├── milk_node/
│   └── milk_node.ino         -- upload to ESP32 #2
├── shed_node/
│   ├── shed_node.ino         -- upload to ESP32 #3
│   └── mastitis_model_v2.cpp -- auto-included, don't open/edit
└── README.md (this file)
```

## Setup order (do this exactly, order matters)

1. **Get the Shed ESP's MAC address first.** Upload this tiny sketch to the board you'll use as Shed ESP:
   ```cpp
   #include <WiFi.h>
   void setup(){ Serial.begin(115200); delay(1000); Serial.println(WiFi.macAddress()); }
   void loop(){}
   ```
   Open Serial Monitor (115200 baud), copy the printed MAC address (looks like `AA:BB:CC:DD:EE:FF`).

2. **Edit `SHED_ESP_MAC[]`** at the top of both `wearable_node.ino` and `milk_node.ino` with that address, formatted as `{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}`.

3. **Install libraries** (Arduino IDE → Tools → Manage Libraries): `TinyGPSPlus`, `DHT sensor library` (Adafruit, plus its `Adafruit Unified Sensor` dependency), `OneWire`, `DallasTemperature`, `HX711` (by Bogdan Necula).

4. **Calibrate the load cell** — required, or your yield numbers are meaningless. Full steps are in the comment above `calibrate()` in `milk_node.ino`. Short version: read the raw value with a known weight (e.g. a 1kg bag) on the load cell, divide raw-by-weight, put that number in `LOADCELL_SCALE_FACTOR`.

5. **Wire each board** per the pin comments at the top of each `.ino` file. Quick reference:

   **Wearable (ESP32 #1):** MPU6050 → I2C (SDA=21, SCL=22) · GPS → UART2 (RX=16, TX=17) · DHT11 → GPIO4 · Sound sensor D0 → GPIO27

   **Milk unit (ESP32 #2):** DS18B20 → GPIO4 (+4.7k pull-up to 3V3) · HX711 DT/SCK → GPIO32/33 · Heartbeat sensor S → GPIO34

   **Shed ESP (ESP32 #3):** no sensors wired directly — it only listens over ESP-NOW and runs inference.

6. **Upload each sketch** to its matching board (Board: "ESP32 Dev Module" for all three).

7. **Open Serial Monitor on the Shed ESP.** You should see `[Shed] Received wearable packet` / `[Shed] Received milk packet` messages, followed by a full risk breakdown, once both other boards start transmitting.

## How to demo this convincingly without a real cow

You already decided this correctly earlier — simulate drift by hand, live, in front of the judges:

- **Activity/rumination drop**: leave the wearable board still on the table for a "healthy" baseline reading, then pick it up and shake it gently for a "healthy active" reading, then leave it still again — the activity index should visibly change in the Shed ESP's Serial output.
- **Skin temp rise**: cup your hand around the DHT11 to warm it up a few degrees — this simulates fever.
- **Milk temp rise**: dip the DS18B20 probe briefly in slightly warm water, then back to room temp for baseline.
- **Milk yield drop**: place a consistent weight (e.g. a bottle of water) on the load cell for "normal yield," then a lighter weight for "reduced yield."
- **GPS mobility**: if you have a fix (best done outdoors or near a window), walk a short distance between readings for "high mobility," then leave it stationary for "low mobility." Indoors with no fix, the firmware returns a neutral 50 so the demo doesn't stall.

Run through 3-4 cycles: normal → normal → drifting → drifting-more, and read out the Shed ESP's risk score climbing from NORMAL → WATCH → HIGH RISK on Serial Monitor, live. That's a genuinely convincing demo of the actual mechanism, with real sensors reacting to real physical changes you're making by hand.

