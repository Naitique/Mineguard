// ===========================================================================
// MQ2Sensor  --  raw analog + digital read of an MQ-2 gas/smoke breakout
// (methane/smoke detection over underground panels).
//
// No vendor library: an MQ-2 breakout is just a heated resistive element
// biased by two onboard resistors, read out as a plain analog voltage (AO)
// and a threshold-comparator digital flag (DO). There is nothing here an
// I2C/SPI driver would add -- analogRead()/digitalRead() is the whole API.
//
// IMPORTANT -- this module is UNCALIBRATED by design:
//   * No ppm conversion. The MQ-2 datasheet's ppm curves assume the heater
//     runs at its rated 5V; this project runs it at 3V3 for ESP32-safe
//     analog levels (see config.h), so raw ADC counts are reported as-is --
//     converting them to a fake ppm number would be actively misleading.
//   * No "safe"/"danger" threshold. Per project policy, risk classification
//     is not this firmware's job -- report the raw signal, nothing more.
//   * Needs a warm-up period (MQ2_WARMUP_MS in config.h) before the heater
//     stabilises; readings before that are noise.
//
// PHASE: bring-up/testing only.
// ===========================================================================
#pragma once

#include <Arduino.h>

struct Mq2Reading {
  uint16_t analog_raw   = 0;      // raw ADC counts, 0..4095 (ESP32 12-bit ADC)
  float    analog_volts = 0.0f;   // analog_raw scaled by the 3.3V ADC reference

  // Raw level of the onboard comparator's digital output. Which level means
  // "gas detected" varies by breakout batch (some go LOW past threshold,
  // some go HIGH) -- confirm on the bench (e.g. wave an unlit lighter's gas
  // near the sensor and see which level flips), don't assume.
  bool digital_pin_high = false;
};

class MQ2Sensor {
 public:
  // Configures the pins. No handshake exists for this sensor, so this always
  // "succeeds" -- there is no missing/miswired detection possible here the
  // way there is for I2C/SPI/UART peripherals.
  void begin(uint8_t analog_pin, uint8_t digital_pin);

  void read(Mq2Reading& out) const;

 private:
  uint8_t analog_pin_  = 0;
  uint8_t digital_pin_ = 0;
};
