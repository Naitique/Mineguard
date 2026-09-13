// ===========================================================================
// main.cpp  --  Mineguard sensor node firmware.
//
// Flow:   ESP32  ->  I2C     ->  MPU6050 (tilt/motion)
//                ->  UART2   ->  GPS (NEO-6M/M8N style, location)
//                ->  analog/digital -> MQ-2 (methane/smoke, uncalibrated)
//                ->  1-Wire  ->  DS18B20 (temperature)
//                ->  SPI     ->  LoRa Ra-02  (node <-> node radio link)
//                ->  Serial Monitor
//
// This file is an ORCHESTRATOR only. Each subsystem's detail lives in its own
// module under lib/. All tunable settings live in src/config.h.
//
// Active features are chosen by ENABLE_MPU6050 / ENABLE_GPS / ENABLE_MQ2 /
// ENABLE_DS18B20 / ENABLE_LORA in config.h. Each bus is independent -- bring
// up any subset. (The VL53L1X / ToF sensor was removed from the project.)
// ===========================================================================
#include <Arduino.h>

#include "config.h"

// True when the I2C sensor is compiled in. I2C is only initialised and
// diagnosed when this holds.
#define I2C_ENABLED (ENABLE_MPU6050)

// True when ANY sensor (of any bus) is compiled in -- gates the shared
// sample-interval scheduler and "[DATA]" block in loop(). LoRa is reported
// separately (its own timing, its own [LoRa] lines).
#define SENSORS_ENABLED (ENABLE_MPU6050 || ENABLE_GPS || ENABLE_MQ2 || ENABLE_DS18B20)

#if I2C_ENABLED
#include <Wire.h>
#endif
#if ENABLE_MPU6050
#include "MPU6050Sensor.h"
#endif
#if ENABLE_GPS
#include "GPSModule.h"
#endif
#if ENABLE_MQ2
#include "MQ2Sensor.h"
#endif
#if ENABLE_DS18B20
#include "DS18B20Sensor.h"
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

#if !SENSORS_ENABLED && !ENABLE_LORA
#error "Enable at least one feature in config.h (ENABLE_MPU6050 / ENABLE_GPS / ENABLE_MQ2 / ENABLE_DS18B20 / ENABLE_LORA)"
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
#if ENABLE_GPS
static GPSModule g_gps;
static bool g_gps_ok = false;
#endif
#if ENABLE_MQ2
static MQ2Sensor g_mq2;
#endif
#if ENABLE_DS18B20
static DS18B20Sensor g_ds18b20;
static bool g_ds18b20_ok = false;
#endif
#if ENABLE_LORA
static LoRaTransport g_lora;
static bool g_lora_ok = false;

// Latest reading from each enabled sensor, cached here so the LoRa sender
// (its own 2s timing) can build a packet from whatever the 1s sample loop
// last measured, without re-reading the hardware itself.
#if ENABLE_MPU6050
static MpuReading g_last_mpu;
static bool g_last_mpu_valid = false;
#endif
#if ENABLE_GPS
static GpsReading g_last_gps;
#endif
#if ENABLE_MQ2
static Mq2Reading g_last_mq2;
#endif
#endif  // ENABLE_LORA
#if ENABLE_SYNTHETIC_DATA
static SyntheticData g_synth;
#endif
#if ENABLE_WIFI_FORWARD
static WiFiForwarder g_wifi;
static bool g_wifi_ok = false;
#endif

#if SENSORS_ENABLED
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
        "[I2C]   A device responded. Expected: MPU6050 at 0x68/0x69. "
        "Set the matching address in config.h.");
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
  char buf[96];
  snprintf(buf, sizeof(buf),
           "node=%s seq=%lu up=%.1f synth=1 roll=%.2f pitch=%.2f "
           "az=%.2f gz=%.1f",
           NODE_ID, (unsigned long)seq, now_ms / 1000.0f,
           s.roll_deg, s.pitch_deg, s.accel_z_g, s.gyro_z_dps);
  return String(buf);
}
#endif  // ENABLE_SYNTHETIC_DATA

#if !ENABLE_SYNTHETIC_DATA
// Pack the latest REAL sensor readings into a compact key=value line. Each
// section only appears if that sensor is enabled and has produced at least
// one valid reading -- a sensor that's down just leaves its fields out
// rather than sending stale or fabricated numbers.
static String formatRealPayload(uint32_t seq, uint32_t now_ms) {
  char buf[200];
  int n = snprintf(buf, sizeof(buf), "node=%s seq=%lu up=%.1f",
                   NODE_ID, (unsigned long)seq, now_ms / 1000.0f);

#if ENABLE_MPU6050
  if (g_last_mpu_valid && n > 0 && (size_t)n < sizeof(buf)) {
    n += snprintf(buf + n, sizeof(buf) - n,
                  " roll=%.2f pitch=%.2f az=%.2f gz=%.1f",
                  g_last_mpu.roll_deg, g_last_mpu.pitch_deg,
                  g_last_mpu.accel_z_g, g_last_mpu.gyro_z_dps);
  }
#endif
#if ENABLE_GPS
  if (n > 0 && (size_t)n < sizeof(buf)) {
    n += snprintf(buf + n, sizeof(buf) - n, " fix=%d",
                  g_last_gps.has_fix ? 1 : 0);
  }
  if (g_last_gps.has_fix && n > 0 && (size_t)n < sizeof(buf)) {
    n += snprintf(buf + n, sizeof(buf) - n, " lat=%.6f lon=%.6f",
                  g_last_gps.latitude_deg, g_last_gps.longitude_deg);
  }
#endif
#if ENABLE_MQ2
  if (n > 0 && (size_t)n < sizeof(buf)) {
    n += snprintf(buf + n, sizeof(buf) - n, " mq2=%u mq2d=%d",
                  g_last_mq2.analog_raw, g_last_mq2.digital_pin_high ? 1 : 0);
  }
#endif

  return String(buf);
}
#endif  // !ENABLE_SYNTHETIC_DATA

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
    String msg = formatRealPayload(seq, now);
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
    const uint8_t chip = g_mpu.chipId();
    const char* chip_name = (chip == MPU_WHOAMI_MPU6050)   ? "MPU6050"
                             : (chip == MPU_WHOAMI_MPU6500) ? "MPU6500 (GY-521 clone)"
                                                             : "unknown";
    Serial.printf("[MPU6050] Detected at 0x%02X  chip=%s (WHO_AM_I=0x%02X)\n",
                  MPU6050_I2C_ADDR, chip_name, chip);
    any_sensor_ok = true;
  } else if (g_mpu.chipId() != 0) {
    // Something answered the address but wasn't a recognised chip ID -- see
    // MPU_WHOAMI_* in MPU6050Sensor.h for the accepted list.
    Serial.printf(
        "[ERROR] MPU6050 not recognised: a chip answered at 0x%02X but its "
        "WHO_AM_I=0x%02X isn't a known MPU6050/MPU6500 value. Either a "
        "different clone chip, or a noisy read -- try I2C_CLOCK_HZ = 100000 "
        "in config.h.\n",
        MPU6050_I2C_ADDR, g_mpu.chipId());
  } else {
    Serial.printf(
        "[ERROR] MPU6050 not detected at 0x%02X -- check wiring "
        "(SDA=GPIO%u/D%u, SCL=GPIO%u/D%u, VCC=3V3). "
        "If AD0 is tied high use 0x69 in config.h.\n",
        MPU6050_I2C_ADDR, I2C_SDA_PIN, I2C_SDA_PIN, I2C_SCL_PIN, I2C_SCL_PIN);
  }
#endif

  // If not a single enabled I2C sensor came up, scan the bus so the log is
  // actionable. We do NOT hard-stop: loop() keeps reporting the error.
  if (!any_sensor_ok) {
    diagnoseEmptyBus();
    Serial.println("[FATAL] No enabled I2C sensor initialised. Fix wiring, reset.");
  } else {
    Serial.println("[SENSOR] I2C init complete.");
  }
#endif  // I2C_ENABLED

#if ENABLE_GPS
  Serial.printf("[GPS] UART2 RX=GPIO%u (RX2) TX=GPIO%u (TX2) @%lu baud\n",
                GPS_RX_PIN, GPS_TX_PIN, (unsigned long)GPS_BAUD);
  g_gps_ok = g_gps.begin(Serial2, GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
  if (g_gps_ok) {
    Serial.println("[GPS] Module is talking (valid NMEA sentence seen).");
  } else {
    Serial.println(
        "[ERROR] GPS: no valid NMEA sentence in 3s -- check wiring (GPS TX -> "
        "ESP32 GPIO16/RX2, GPS RX -> ESP32 GPIO17/TX2 -- easy to swap, "
        "VCC=3V3, GND), that GPS_BAUD in config.h matches the module, and "
        "that its LED is blinking (has power).");
  }
#endif

#if ENABLE_MQ2
  g_mq2.begin(MQ2_ANALOG_PIN, MQ2_DIGITAL_PIN);
  Serial.printf(
      "[MQ2] AO=GPIO%u DO=GPIO%u -- uncalibrated bring-up, warming up %lus "
      "(readings before that are unreliable)\n",
      MQ2_ANALOG_PIN, MQ2_DIGITAL_PIN, (unsigned long)(MQ2_WARMUP_MS / 1000));
#endif

#if ENABLE_DS18B20
  g_ds18b20_ok = g_ds18b20.begin(DS18B20_PIN);
  if (g_ds18b20_ok) {
    Serial.printf("[DS18B20] Detected on GPIO%u (D%u)\n", DS18B20_PIN, DS18B20_PIN);
  } else {
    Serial.printf(
        "[ERROR] DS18B20 not detected on GPIO%u (D%u) -- check wiring "
        "(DATA=GPIO%u, VCC=3V3, GND) and that a 4.7k ohm pull-up resistor is "
        "present between DATA and VCC (required unless your breakout "
        "already includes one -- without it the bus floats and nothing "
        "will be found even if wiring is otherwise correct).\n",
        DS18B20_PIN, DS18B20_PIN, DS18B20_PIN);
  }
#endif

#if SENSORS_ENABLED
  Serial.printf("[READY] sampling every %lu ms\n",
                (unsigned long)SENSOR_INTERVAL_MS);
  g_last_sample_ms = millis() - SENSOR_INTERVAL_MS;  // force first sample now
#endif

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
#else
  if (LORA_ROLE_SENDER) {
    Serial.println(
        "[LoRa] TX payload source: REAL sensor readings (whichever of "
        "MPU6050/GPS/MQ2 are enabled and reporting).");
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

#if ENABLE_GPS
  // GPS bytes stream in continuously and must be drained every iteration --
  // not just on the sample interval below -- or the UART buffer overflows
  // and sentences get corrupted.
  g_gps.poll();
#endif

#if ENABLE_DS18B20
  // Advances the non-blocking conversion state machine every iteration so a
  // ~750ms conversion never stalls the rest of loop() (GPS polling, LoRa).
  g_ds18b20.update();
#endif

#if SENSORS_ENABLED
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
#if ENABLE_LORA
        g_last_mpu = r;
        g_last_mpu_valid = true;
#endif
      } else {
        Serial.println("[ERROR] MPU6050 read failed (I2C). Check wiring/power.");
#if ENABLE_LORA
        g_last_mpu_valid = false;  // don't let LoRa send a stale reading
#endif
      }
    } else {
      Serial.println("[ERROR] MPU6050 not detected -- check wiring / config.h.");
#if ENABLE_LORA
      g_last_mpu_valid = false;
#endif
    }
#endif

#if ENABLE_GPS
    if (g_gps_ok) {
      GpsReading r;
      g_gps.read(r);
#if ENABLE_LORA
      g_last_gps = r;
#endif
      if (r.has_fix) {
        Serial.printf(
            "[GPS] fix lat=%.6f lon=%.6f alt=%.1fm sats=%u hdop=%.1f\n",
            r.latitude_deg, r.longitude_deg, r.altitude_m, r.satellites,
            r.hdop);
      } else {
        Serial.printf(
            "[GPS] no fix yet (sentences_ok=%lu sentences_failed=%lu) -- "
            "normal for the first 30s-few min outdoors; may never fix "
            "indoors\n",
            (unsigned long)r.sentences_ok, (unsigned long)r.sentences_failed);
      }
    } else {
      Serial.println("[ERROR] GPS not talking -- check wiring / config.h.");
    }
#endif

#if ENABLE_MQ2
    {
      Mq2Reading r;
      g_mq2.read(r);
#if ENABLE_LORA
      g_last_mq2 = r;
#endif
      const bool warmed_up = millis() >= MQ2_WARMUP_MS;
      Serial.printf(
          "[MQ2] analog_raw=%u (%.2fV)  digital_pin_high=%s%s\n",
          r.analog_raw, r.analog_volts, r.digital_pin_high ? "true" : "false",
          warmed_up ? "" : "  (WARMING UP, ignore)");
    }
#endif

#if ENABLE_DS18B20
    if (g_ds18b20_ok) {
      Ds18b20Reading r;
      g_ds18b20.read(r);
      if (r.valid) {
        Serial.printf("[DS18B20] temp=%.2f C\n", r.temperature_c);
      } else {
        Serial.println(
            "[ERROR] DS18B20 read failed -- disconnected mid-run? "
            "Check wiring/pull-up.");
      }
    } else {
      Serial.println("[ERROR] DS18B20 not detected -- check wiring / config.h.");
    }
#endif
  }
#endif  // SENSORS_ENABLED
}
