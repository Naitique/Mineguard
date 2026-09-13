// ===========================================================================
// config.h  --  SINGLE configuration surface for a Mineguard sensor node.
//
// This is the ONE place to change per-board settings. Do not scatter node IDs,
// pin numbers, or timing constants through the rest of the code.
//
// Board:   ESP32-WROOM-32 development board.
// Code uses GPIO numbers; the silkscreen label on our board is noted in
// comments (e.g. GPIO 21 == board label "D21").
//
// PHASE 1: only the MPU6050 (I2C) section is actually used. The other pins are
// documented now so the wiring stays stable as later phases are added.
// ===========================================================================
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Node identity
// ---------------------------------------------------------------------------
// Change this per physical node: "NODE_01", "NODE_02", ...
// Nothing else in the codebase should hard-code a node name.
#define NODE_ID "NODE_01"

// Firmware phase label, printed at boot so we can tell units apart on the
// bench. Purely cosmetic -- update it as the enabled feature set changes.
#define FIRMWARE_PHASE "Phase 8 (LoRa receiver)"

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------
// Keep in sync with monitor_speed in platformio.ini.
static const uint32_t SERIAL_BAUD = 115200;

// ---------------------------------------------------------------------------
// Which features this build enables. Bring one subsystem up at a time.
//   MPU6050 (I2C)          ->  ENABLE_MPU6050 1
//   GPS (UART2)            ->  ENABLE_GPS 1
//   MQ-2 (analog+digital)  ->  ENABLE_MQ2 1
//   DS18B20 (1-Wire)       ->  ENABLE_DS18B20 1
//   LoRa link (SPI)        ->  ENABLE_LORA 1 (sensors optional)
// Each bus is independent -- I2C only touched if ENABLE_MPU6050, UART2 only
// if ENABLE_GPS, LoRa's SPI only if ENABLE_LORA. Mix and match freely.
// (VL53L1X / ToF sensor was removed from the project -- no longer used.)
// (Currently set to bring up MPU6050 + GPS + MQ-2 together. LoRa left off --
// enable it separately once these three are confirmed on the bench.)
// ---------------------------------------------------------------------------
#define ENABLE_MPU6050  0
#define ENABLE_GPS      0
#define ENABLE_MQ2      0
#define ENABLE_DS18B20  0
#define ENABLE_LORA     1

// ---------------------------------------------------------------------------
// I2C bus  (MPU6050)
// ---------------------------------------------------------------------------
static const uint8_t  I2C_SDA_PIN  = 21;      // board label "D21"
static const uint8_t  I2C_SCL_PIN  = 22;      // board label "D22"
static const uint32_t I2C_CLOCK_HZ = 100000;  // 100 kHz "standard mode" --
                                              // dropped from 400 kHz to test
                                              // whether the MPU6050 timeout
                                              // is a bus-timing margin issue.

// MPU6050 7-bit I2C address.
//   AD0 pin low  (GY-521 default) -> 0x68
//   AD0 pin high                  -> 0x69
static const uint8_t MPU6050_I2C_ADDR = 0x68;

// ---------------------------------------------------------------------------
// GPS module (NEO-6M / NEO-M8N style) -- plain NMEA over the ESP32's second
// hardware UART. Wiring: GPS TX -> GPS_RX_PIN, GPS RX -> GPS_TX_PIN (only
// needed to send commands to the module), VCC -> 3V3 (confirm your specific
// module tolerates 3.3V; most NEO-6M/M8N breakouts do), GND -> GND.
// ---------------------------------------------------------------------------
static const uint8_t  GPS_RX_PIN = 16;    // board label "RX2" -- ESP32 receives here
static const uint8_t  GPS_TX_PIN = 17;    // board label "TX2" -- ESP32 transmits here
static const uint32_t GPS_BAUD   = 9600;  // NEO-6M/M8N power-on default

// ---------------------------------------------------------------------------
// MQ-2 gas/smoke sensor (methane + smoke, bring-up/testing only -- see
// lib/MQ2Sensor for why this is deliberately uncalibrated).
//
// Wiring: VCC -> 3V3 (NOT 5V/VIN -- at 5V the AO/DO outputs can approach the
// supply rail, above the ESP32's 3.3V max input, and could damage the pins).
// GND -> GND. Running the heater at 3.3V instead of the datasheet 5V means
// readings are qualitative (rise/fall with gas) only, not calibrated ppm.
// ---------------------------------------------------------------------------
static const uint8_t MQ2_ANALOG_PIN  = 34;  // board label "D34", ADC1_CH6
static const uint8_t MQ2_DIGITAL_PIN = 35;  // board label "D35", ADC1_CH7

// Heater warm-up time before readings are meaningful. This is a bring-up
// minimum, not a calibration soak -- the datasheet recommends much longer
// (hours) for readings to match its ppm curves.
static const uint32_t MQ2_WARMUP_MS = 60000;

// ---------------------------------------------------------------------------
// DS18B20 temperature sensor -- 1-Wire digital sensor, ambient/ground
// temperature alongside the other measurements.
//
// Wiring: DATA -> DS18B20_PIN, VCC -> 3V3, GND -> GND. REQUIRES a 4.7k ohm
// pull-up resistor between DATA and VCC -- a bare TO-92 sensor does not have
// one built in (some waterproof-probe breakout boards do; check yours).
// Without the pull-up the bus floats and no device will be found, even with
// wiring otherwise correct.
// ---------------------------------------------------------------------------
static const uint8_t DS18B20_PIN = 4;  // board label "D4"

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------
// How often loop() reads the sensor and prints a block. Tune later to match
// the physical experiment. Do not hard-code this interval elsewhere.
static const uint32_t SENSOR_INTERVAL_MS = 1000;

// ---------------------------------------------------------------------------
// LoRa  --  AI-Thinker Ra-02 (SX1278) on the ESP32 VSPI bus
// ---------------------------------------------------------------------------
// This is a SEPARATE radio link between ESP32 nodes only (node <-> node).
// It is NOT the ESP32 <-> Raspberry Pi path -- that is Wi-Fi, added later.

// Role of THIS board in the link:
//   true  -> transmit a test packet every LORA_TX_INTERVAL_MS
//   false -> listen continuously and print every packet received
// Flash one board true and another false to test a real link.
static const bool LORA_ROLE_SENDER = false;

// SPI + control pins. GPIO numbers; board silkscreen label in the comment.
// SCK/MISO/MOSI/NSS are the ESP32 VSPI defaults, wired to match.
static const uint8_t LORA_SCK_PIN  = 18;  // "D18"
static const uint8_t LORA_MISO_PIN = 19;  // "D19"
static const uint8_t LORA_MOSI_PIN = 23;  // "D23"
static const uint8_t LORA_NSS_PIN  = 5;   // "D5"   chip select
static const uint8_t LORA_RST_PIN  = 14;  // "D14"  reset
static const uint8_t LORA_DIO0_PIN = 26;  // "D26"  RX-done / TX-done IRQ

// Carrier frequency in Hz. The Ra-02 is the 433 MHz SX1278 variant.
// SET THIS to a frequency permitted in your region / licence-free band.
static const long LORA_FREQUENCY_HZ = 433000000L;  // 433.0 MHz

// Radio parameters -- BOTH ends of a link MUST use identical values or they
// will not hear each other.
static const int  LORA_SPREADING_FACTOR    = 9;        // 6..12 (higher = longer range, lower data rate)
static const long LORA_SIGNAL_BANDWIDTH_HZ = 125000L;  // 7.8k..500k
static const int  LORA_CODING_RATE_DENOM   = 5;        // 5..8  -> 4/5 .. 4/8
static const int  LORA_SYNC_WORD           = 0x24;     // private network id; avoid 0x34 (public LoRaWAN)
static const int  LORA_TX_POWER_DBM        = 17;       // 2..20 via the Ra-02 PA_BOOST pin

// How often the sender transmits a test packet.
static const uint32_t LORA_TX_INTERVAL_MS = 2000;

// While the real MPU6050 is being brought up, the SENDER can transmit
// fabricated readings so the node <-> node link is exercised end to end with
// data-shaped payloads instead of a bare counter. Every synthetic payload
// carries "synth=1"; nothing downstream may treat it as a real measurement.
// Requires ENABLE_LORA. Set back to 0 once real sensor data feeds the packet.
#define ENABLE_SYNTHETIC_DATA 0

// ---------------------------------------------------------------------------
// Wi-Fi forwarding (RECEIVER board only)
// ---------------------------------------------------------------------------
// Separate from the LoRa link above: this pushes each packet the RECEIVER
// picks up over LoRa onward to a listener (tools/wifi_listener.py) on an
// analysis laptop, over the local Wi-Fi network. The SENDER board doesn't
// need this enabled.
//
// Requires firmware/src/secrets.h with WIFI_SSID / WIFI_PASSWORD -- copy
// secrets.h.example and fill in your real network. secrets.h is git-ignored;
// never commit real credentials.
#define ENABLE_WIFI_FORWARD 0

// LAN IP (or hostname) and port of the laptop running wifi_listener.py.
// Find the laptop's IP with `ipconfig getifaddr en0` (macOS Wi-Fi),
// `hostname -I` (Linux), or `ipconfig` (Windows).
static const char* const WIFI_FORWARD_HOST = "10.59.194.146";
static const uint16_t    WIFI_FORWARD_PORT = 8000;
static const char* const WIFI_FORWARD_PATH = "/";

// How long to wait for the Wi-Fi connection at boot before giving up.
static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 10000;
