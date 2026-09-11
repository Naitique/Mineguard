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
#define FIRMWARE_PHASE "Phase 8 (LoRa link, synthetic data)"

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------
// Keep in sync with monitor_speed in platformio.ini.
static const uint32_t SERIAL_BAUD = 115200;

// ---------------------------------------------------------------------------
// Which features this build enables. Bring one subsystem up at a time.
//   Phase 1  MPU6050 only   ->  ENABLE_MPU6050 1
//   Phase 2  VL53L1X only   ->  ENABLE_VL53L1X 1
//   Phase 3  both sensors   ->  ENABLE_MPU6050 1 , ENABLE_VL53L1X 1
//   Phase 8  LoRa link      ->  ENABLE_LORA 1 (sensors optional)
// I2C is only touched when a sensor is enabled; LoRa is on SPI, independent.
// (Currently set for Phase 8 LoRa bring-up.)
// ---------------------------------------------------------------------------
#define ENABLE_MPU6050  0
#define ENABLE_VL53L1X  0
#define ENABLE_LORA     1

// ---------------------------------------------------------------------------
// I2C bus  (shared in later phases by MPU6050 + VL53L1X)
// ---------------------------------------------------------------------------
static const uint8_t  I2C_SDA_PIN  = 21;      // board label "D21"
static const uint8_t  I2C_SCL_PIN  = 22;      // board label "D22"
static const uint32_t I2C_CLOCK_HZ = 400000;  // 400 kHz "fast mode"; drop to
                                              // 100000 if wiring is long/noisy.

// MPU6050 7-bit I2C address.
//   AD0 pin low  (GY-521 default) -> 0x68
//   AD0 pin high                  -> 0x69
static const uint8_t MPU6050_I2C_ADDR = 0x68;

// ---------------------------------------------------------------------------
// VL53L1X Time-of-Flight distance sensor (shares the I2C bus above)
// ---------------------------------------------------------------------------
// Fixed 7-bit address for the VL53L1X. It differs from the MPU6050's, so both
// devices can sit on the same bus without changing anything.
static const uint8_t VL53L1X_I2C_ADDR = 0x29;

// Distance mode:
//   true  -> Long  (~4 m range, more sensitive to ambient light)
//   false -> Short (~1.3 m range, best immunity to ambient light / sunlight)
// For a fixed indoor reference target under ~1 m, Short is usually steadier.
static const bool VL53L1X_LONG_RANGE = true;

// Measurement timing budget, microseconds. Longer budget = less noise, slower
// updates. 50 ms is a solid general-purpose value.
static const uint32_t VL53L1X_TIMING_BUDGET_US = 50000;

// Inter-measurement period for continuous ranging, milliseconds.
// Must be >= timing budget (in ms). 50 pairs with the 50 ms budget above.
static const uint32_t VL53L1X_INTERMEASUREMENT_MS = 50;

// I2C read timeout for the ToF sensor, milliseconds. A read that exceeds this
// is reported as a timeout error instead of hanging.
static const uint16_t VL53L1X_IO_TIMEOUT_MS = 500;

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------
// How often loop() reads the sensors and prints a block. Tune later to match
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
static const bool LORA_ROLE_SENDER = true;

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

// While the real MPU6050 / VL53L1X are being replaced, the SENDER can transmit
// fabricated readings so the node <-> node link is exercised end to end with
// data-shaped payloads instead of a bare counter. Every synthetic payload
// carries "synth=1"; nothing downstream may treat it as a real measurement.
// Requires ENABLE_LORA. Set back to 0 once real sensor data feeds the packet.
#define ENABLE_SYNTHETIC_DATA 1
