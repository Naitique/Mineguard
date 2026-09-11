// ===========================================================================
// main.cpp  --  Mineguard sensor node firmware.
//
// Flow:   ESP32  ->  I2C  ->  { MPU6050 (tilt/motion) , VL53L1X (distance) }
//                ->  SPI  ->  LoRa Ra-02  (node <-> node radio link)
//                ->  Serial Monitor
//
// This file is an ORCHESTRATOR only. Each subsystem's detail lives in its own
// module under lib/. All tunable settings live in src/config.h.
//
// Active features are chosen by ENABLE_MPU6050 / ENABLE_VL53L1X / ENABLE_LORA
// in config.h:
//   Phase 1  MPU6050 only
//   Phase 2  VL53L1X only
//   Phase 3  both sensors together
//   Phase 8  LoRa link (sensors optional)   <-- current
// ===========================================================================
#include <Arduino.h>

#include "config.h"

// True when at least one I2C sensor is compiled in. I2C is only initialised
// and diagnosed when this holds; LoRa is on SPI and fully independent.
#define I2C_ENABLED (ENABLE_MPU6050 || ENABLE_VL53L1X)

#if I2C_ENABLED
#include <Wire.h>
#endif
#if ENABLE_MPU6050
#include "MPU6050Sensor.h"
#endif
#if ENABLE_VL53L1X
#include "VL53L1XSensor.h"
#endif
#if ENABLE_LORA
#include "LoRaTransport.h"
#endif
#if ENABLE_SYNTHETIC_DATA
#include "SyntheticData.h"
#endif
#if ENABLE_WIFI_FORWARD
#include <WiFi.h>
#include "WiFiForwarder.h"
#include "secrets.h"  // WIFI_SSID / WIFI_PASSWORD -- copy secrets.h.example
#endif

#if !I2C_ENABLED && !ENABLE_LORA
#error "Enable at least one feature in config.h (ENABLE_MPU6050 / ENABLE_VL53L1X / ENABLE_LORA)"
#endif
#if ENABLE_SYNTHETIC_DATA && !ENABLE_LORA
#error "ENABLE_SYNTHETIC_DATA needs ENABLE_LORA -- it only feeds the LoRa sender"
#endif
#if ENABLE_WIFI_FORWARD && !ENABLE_LORA
#error "ENABLE_WIFI_FORWARD needs ENABLE_LORA -- it only forwards packets the LoRa receiver gets"
#endif

// --- Subsystem instances + their init status ------------------------------
#if ENABLE_MPU6050
static MPU6050Sensor g_mpu;
static bool g_mpu_ok = false;
#endif
#if ENABLE_VL53L1X
static VL53L1XSensor g_tof;
static bool g_tof_ok = false;
#endif
#if ENABLE_LORA
static LoRaTransport g_lora;
static bool g_lora_ok = false;
#endif
#if ENABLE_SYNTHETIC_DATA
static SyntheticData g_synth;
#endif
#if ENABLE_WIFI_FORWARD
static WiFiForwarder g_wifi;
static bool g_wifi_ok = false;
#endif

#if I2C_ENABLED
// millis() timestamp of the last sensor sample (non-blocking scheduler).
static uint32_t g_last_sample_ms = 0;
#endif

// ===========================================================================
// I2C helpers (compiled only when an I2C sensor is enabled)
// ===========================================================================
#if I2C_ENABLED
// Probe every 7-bit I2C address and report which ones acknowledge. Tells us
// whether the bus is dead (nothing responds -> wiring/power) or a device is
// simply at an unexpected address.
static uint8_t scanI2CBus() {
  Serial.println("[I2C] Scanning bus for any device (0x08..0x77)...");
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C]   device found at 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("[I2C]   scan done: %u device(s) responded.\n", found);
  return found;
}

// Scan at the configured clock; if nothing answers, retry once at 100 kHz
// (long breadboard wiring can fail completely at 400 kHz). Prints guidance.
static void diagnoseEmptyBus() {
  uint8_t found = scanI2CBus();

  if (found == 0) {
    Serial.println("[I2C] Nothing at configured speed. Retrying at 100 kHz...");
    Wire.setClock(100000);
    found = scanI2CBus();
    if (found > 0) {
      Serial.println(
          "[I2C]   >>> Works at 100 kHz but not 400 kHz: set "
          "I2C_CLOCK_HZ = 100000 in config.h.");
    }
  }

  if (found == 0) {
    Serial.println(
        "[I2C]   NOTHING responded at either speed. The bus itself is not "
        "working -- this is almost always power, ground, or the two signal "
        "wires, NOT the sensor. Check, with a multimeter if possible:");
    Serial.println(
        "[I2C]     1. 3.3V between the sensor VIN/VCC pin and GND "
        "(try the ESP32 5V/VIN pin instead -- both breakouts have a "
        "regulator).");
    Serial.println(
        "[I2C]     2. Continuity from sensor GND to an ESP32 GND pin.");
    Serial.println(
        "[I2C]     3. SDA on GPIO21 (D21) and SCL on GPIO22 (D22) -- "
        "recount the header; RX0/TX0 sit between them.");
    Serial.println(
        "[I2C]     4. Swap the two signal jumpers; then replace all 4 "
        "jumpers; then try without the breadboard.");
  } else {
    Serial.println(
        "[I2C]   A device responded. Expected: MPU6050 at 0x68/0x69, "
        "VL53L1X at 0x29. Set the matching address in config.h.");
  }
}
#endif  // I2C_ENABLED

#if ENABLE_MPU6050
static void printMpu(const MpuReading& r) {
  Serial.printf("[MPU6050] accel_g   x=%.3f  y=%.3f  z=%.3f\n",
                r.accel_x_g, r.accel_y_g, r.accel_z_g);
  Serial.printf("[MPU6050] gyro_dps  x=%.2f  y=%.2f  z=%.2f\n",
                r.gyro_x_dps, r.gyro_y_dps, r.gyro_z_dps);
  Serial.printf("[MPU6050] roll=%.2f deg  pitch=%.2f deg\n",
                r.roll_deg, r.pitch_deg);
}
#endif

#if ENABLE_VL53L1X
static void printTof(const TofReading& r) {
  Serial.printf("[VL53L1X] distance=%umm  status=%u (%s)\n",
                r.distance_mm, r.range_status, r.status_text);
}
#endif

// ===========================================================================
// LoRa service (compiled only when ENABLE_LORA)
// ===========================================================================
#if ENABLE_LORA

#if ENABLE_SYNTHETIC_DATA
// Pack a fabricated reading into a compact key=value line for the radio.
// "synth=1" marks the payload as made-up data. Kept short on purpose: LoRa
// airtime grows with length. Not all raw fields go on the wire -- the full
// set stays in SyntheticReading for when real sensors replace this.
static String formatSyntheticPayload(uint32_t seq, uint32_t now_ms,
                                     const SyntheticReading& s) {
  char buf[128];
  snprintf(buf, sizeof(buf),
           "node=%s seq=%lu up=%.1f synth=1 roll=%.2f pitch=%.2f "
           "az=%.2f gz=%.1f dist_mm=%u rstat=%u",
           NODE_ID, (unsigned long)seq, now_ms / 1000.0f,
           s.roll_deg, s.pitch_deg, s.accel_z_g, s.gyro_z_dps,
           s.dist_mm, s.range_status);
  return String(buf);
}
#endif  // ENABLE_SYNTHETIC_DATA

// Sender: transmit a short structured test line every LORA_TX_INTERVAL_MS.
// Receiver: drain any frames that have arrived and print them with RSSI/SNR.
// Called every loop() iteration; keeps its own timing so it never blocks.
static void serviceLoRa() {
  if (!g_lora_ok) {
    return;  // init failure already reported; nothing to do
  }

  if (LORA_ROLE_SENDER) {
    static uint32_t last_tx_ms = 0;
    static uint32_t seq = 0;
    const uint32_t now = millis();
    if ((now - last_tx_ms) < LORA_TX_INTERVAL_MS) {
      return;
    }
    last_tx_ms = now;

    // Plain text for bring-up. Structured JSON-over-LoRa comes after Phase 5.
#if ENABLE_SYNTHETIC_DATA
    SyntheticReading s;
    g_synth.read(s);
    String msg = formatSyntheticPayload(seq, now, s);
#else
    String msg = String("node=") + NODE_ID + " seq=" + String(seq) +
                 " uptime_s=" + String(now / 1000.0f, 1);
#endif
    const bool ok = g_lora.send(msg);
    Serial.printf("[LoRa] TX seq=%lu (%s): \"%s\"\n",
                  (unsigned long)seq, ok ? "ok" : "FAILED", msg.c_str());
    seq++;
  } else {
    LoRaRxPacket pkt;
    while (g_lora.receive(pkt)) {
      Serial.printf("[LoRa] RX rssi=%ddBm snr=%.1fdB len=%u: \"%s\"\n",
                    pkt.rssi_dbm, pkt.snr_db, pkt.payload.length(),
                    pkt.payload.c_str());

#if ENABLE_WIFI_FORWARD
      if (g_wifi_ok) {
        String escaped_payload = pkt.payload;
        escaped_payload.replace("\"", "\\\"");
        char json[320];
        snprintf(json, sizeof(json),
                 "{\"rssi_dbm\":%d,\"snr_db\":%.1f,\"len\":%u,\"payload\":\"%s\"}",
                 pkt.rssi_dbm, pkt.snr_db, pkt.payload.length(),
                 escaped_payload.c_str());
        if (!g_wifi.send(WIFI_FORWARD_HOST, WIFI_FORWARD_PORT,
                         WIFI_FORWARD_PATH, String(json))) {
          Serial.println(
              "[ERROR] WiFi forward failed -- listener unreachable? "
              "Packet above was still received and printed, just not "
              "forwarded. Check wifi_listener.py is running and "
              "WIFI_FORWARD_HOST/PORT in config.h.");
        }
      }
#endif
    }
  }
}
#endif  // ENABLE_LORA

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  // Give the USB CDC / serial monitor a moment to attach so we don't lose the
  // banner. Bounded wait -- never block forever on the serial port.
  const uint32_t serial_wait_start = millis();
  while (!Serial && (millis() - serial_wait_start) < 2000) {
    delay(10);
  }

  Serial.println();
  Serial.printf("[BOOT] Mineguard node %s -- %s\n", NODE_ID, FIRMWARE_PHASE);

#if I2C_ENABLED
  Serial.printf("[I2C]  SDA=GPIO%u (D%u)  SCL=GPIO%u (D%u)  @%lukHz\n",
                I2C_SDA_PIN, I2C_SDA_PIN, I2C_SCL_PIN, I2C_SCL_PIN,
                (unsigned long)(I2C_CLOCK_HZ / 1000));
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);

  bool any_sensor_ok = false;

#if ENABLE_MPU6050
  g_mpu_ok = g_mpu.begin(Wire, MPU6050_I2C_ADDR);
  if (g_mpu_ok) {
    Serial.printf("[MPU6050] Detected at 0x%02X\n", MPU6050_I2C_ADDR);
    any_sensor_ok = true;
  } else {
    Serial.printf(
        "[ERROR] MPU6050 not detected at 0x%02X -- check wiring "
        "(SDA=GPIO%u/D%u, SCL=GPIO%u/D%u, VCC=3V3). "
        "If AD0 is tied high use 0x69 in config.h.\n",
        MPU6050_I2C_ADDR, I2C_SDA_PIN, I2C_SDA_PIN, I2C_SCL_PIN, I2C_SCL_PIN);
  }
#endif

#if ENABLE_VL53L1X
  g_tof_ok = g_tof.begin(Wire, VL53L1X_LONG_RANGE, VL53L1X_TIMING_BUDGET_US,
                         VL53L1X_INTERMEASUREMENT_MS, VL53L1X_IO_TIMEOUT_MS);
  if (g_tof_ok) {
    Serial.printf("[VL53L1X] Detected at 0x%02X (%s range)\n",
                  VL53L1X_I2C_ADDR, VL53L1X_LONG_RANGE ? "long" : "short");
    any_sensor_ok = true;
  } else {
    Serial.printf(
        "[ERROR] VL53L1X not detected at 0x%02X -- check wiring "
        "(SDA=GPIO%u/D%u, SCL=GPIO%u/D%u, VIN=3V3).\n",
        VL53L1X_I2C_ADDR, I2C_SDA_PIN, I2C_SDA_PIN, I2C_SCL_PIN, I2C_SCL_PIN);
  }
#endif

  // If not a single enabled sensor came up, scan the bus so the log is
  // actionable. We do NOT hard-stop: loop() keeps reporting the error.
  if (!any_sensor_ok) {
    diagnoseEmptyBus();
    Serial.println("[FATAL] No enabled sensor initialised. Fix wiring, reset.");
  } else {
    Serial.println("[SENSOR] Init complete.");
  }

  Serial.printf("[READY] sampling every %lu ms\n",
                (unsigned long)SENSOR_INTERVAL_MS);
  g_last_sample_ms = millis() - SENSOR_INTERVAL_MS;  // force first sample now
#endif  // I2C_ENABLED

#if ENABLE_LORA
  Serial.printf(
      "[LoRa] SPI SCK=GPIO%u MISO=GPIO%u MOSI=GPIO%u NSS=GPIO%u RST=GPIO%u "
      "DIO0=GPIO%u\n",
      LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN, LORA_NSS_PIN, LORA_RST_PIN,
      LORA_DIO0_PIN);
  g_lora_ok = g_lora.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN,
                           LORA_NSS_PIN, LORA_RST_PIN, LORA_DIO0_PIN,
                           LORA_FREQUENCY_HZ, LORA_SPREADING_FACTOR,
                           LORA_SIGNAL_BANDWIDTH_HZ, LORA_CODING_RATE_DENOM,
                           LORA_SYNC_WORD, LORA_TX_POWER_DBM);
  if (g_lora_ok) {
    Serial.printf(
        "[LoRa] Ready. freq=%ldHz SF=%d BW=%ldHz CR=4/%d sync=0x%02X "
        "power=%ddBm  role=%s\n",
        LORA_FREQUENCY_HZ, LORA_SPREADING_FACTOR, LORA_SIGNAL_BANDWIDTH_HZ,
        LORA_CODING_RATE_DENOM, LORA_SYNC_WORD, LORA_TX_POWER_DBM,
        LORA_ROLE_SENDER ? "SENDER" : "RECEIVER");
  } else {
    Serial.println(
        "[ERROR] LoRa init failed -- the SX1278 did not answer over SPI. "
        "Check 3.3V (NOT 5V) to the Ra-02, GND, and the 6 signal wires "
        "(NSS=GPIO5, SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23, RST=GPIO14, "
        "DIO0=GPIO26).");
  }

#if ENABLE_SYNTHETIC_DATA
  g_synth.begin(micros());
  if (LORA_ROLE_SENDER) {
    Serial.println(
        "[LoRa] TX payload source: SYNTHETIC -- fabricated data tagged "
        "\"synth=1\", NOT real measurements. Disable ENABLE_SYNTHETIC_DATA "
        "in config.h once real sensors feed the packet.");
  }
#endif

#if ENABLE_WIFI_FORWARD
  Serial.printf("[WiFi] Connecting to \"%s\"...\n", WIFI_SSID);
  g_wifi_ok = g_wifi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CONNECT_TIMEOUT_MS);
  if (g_wifi_ok) {
    Serial.printf("[WiFi] Connected. IP=%s  forwarding RX packets to %s:%u%s\n",
                  WiFi.localIP().toString().c_str(), WIFI_FORWARD_HOST,
                  WIFI_FORWARD_PORT, WIFI_FORWARD_PATH);
  } else {
    Serial.println(
        "[ERROR] WiFi connect failed -- check WIFI_SSID/WIFI_PASSWORD in "
        "secrets.h, and that the network is 2.4GHz (the ESP32 can't join "
        "5GHz-only networks). Packets will still print to Serial but won't "
        "be forwarded.");
  }
#endif
#endif  // ENABLE_LORA
}

// ---------------------------------------------------------------------------
// loop()  --  non-blocking. Each subsystem keeps its own timing.
// ---------------------------------------------------------------------------
void loop() {
#if ENABLE_LORA
  serviceLoRa();
#endif

#if I2C_ENABLED
  const uint32_t now = millis();
  if ((now - g_last_sample_ms) >= SENSOR_INTERVAL_MS) {
    g_last_sample_ms = now;

    // Local device uptime in seconds. This is NOT a synchronised wall-clock
    // time -- NTP timestamps arrive in a later phase. Labelled as such.
    const float uptime_s = millis() / 1000.0f;
    Serial.printf("[DATA] node=%s  uptime=%.1fs\n", NODE_ID, uptime_s);

#if ENABLE_MPU6050
    if (g_mpu_ok) {
      MpuReading r;
      if (g_mpu.read(r) && r.valid) {
        printMpu(r);
      } else {
        Serial.println("[ERROR] MPU6050 read failed (I2C). Check wiring/power.");
      }
    } else {
      Serial.println("[ERROR] MPU6050 not detected -- check wiring / config.h.");
    }
#endif

#if ENABLE_VL53L1X
    if (g_tof_ok) {
      TofReading r;
      if (!g_tof.read(r)) {
        Serial.println("[ERROR] VL53L1X read timeout (I2C). Check wiring/power.");
      } else if (!r.valid) {
        Serial.printf(
            "[VL53L1X] rejected reading: status=%u (%s)  raw_distance=%umm\n",
            r.range_status, r.status_text, r.distance_mm);
      } else {
        printTof(r);
      }
    } else {
      Serial.println("[ERROR] VL53L1X not detected -- check wiring / power.");
    }
#endif
  }
#endif  // I2C_ENABLED
}
