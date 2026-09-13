// ===========================================================================
// MQ2Sensor.cpp  --  implementation. See MQ2Sensor.h for the "why".
// ===========================================================================
#include "MQ2Sensor.h"

namespace {
constexpr float kAdcMaxCounts = 4095.0f;  // ESP32 ADC: 12-bit, 0..4095
constexpr float kAdcRefVolts  = 3.3f;
}  // namespace

void MQ2Sensor::begin(uint8_t analog_pin, uint8_t digital_pin) {
  analog_pin_  = analog_pin;
  digital_pin_ = digital_pin;
  pinMode(digital_pin_, INPUT);
}

void MQ2Sensor::read(Mq2Reading& out) const {
  out.analog_raw     = analogRead(analog_pin_);
  out.analog_volts   = (out.analog_raw / kAdcMaxCounts) * kAdcRefVolts;
  out.digital_pin_high = digitalRead(digital_pin_) == HIGH;
}
