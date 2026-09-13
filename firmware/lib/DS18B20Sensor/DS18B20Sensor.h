// ===========================================================================
// DS18B20Sensor  --  wraps OneWire + DallasTemperature for the DS18B20
// digital temperature sensor.
//
// Library choice: paulstoffregen/OneWire (1-Wire bus protocol) +
// milesburton/DallasTemperature (sensor-specific commands on top) is the
// standard, mature pairing for this chip on Arduino/ESP32.
//
// Conversion takes ~750ms at the default 12-bit resolution. To keep this
// firmware non-blocking (nothing else in main.cpp ever blocks), we don't
// call the library's blocking wait -- instead update() runs a tiny state
// machine: kick off a conversion, keep returning immediately, and only read
// the result (then start the next conversion) once enough time has passed.
// Call update() every loop() iteration; call read() any time for the latest
// completed reading.
// ===========================================================================
#pragma once

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

struct Ds18b20Reading {
  float temperature_c = -127.0f;  // DallasTemperature's DEVICE_DISCONNECTED_C
  bool  valid = false;
};

class DS18B20Sensor {
 public:
  // Scans the 1-Wire bus on the given pin. Returns false if no device
  // responds -- almost always a missing pull-up resistor (see config.h) or
  // wiring, not a sensor fault.
  bool begin(uint8_t pin);

  // Advances the non-blocking request/wait/read cycle. Call every loop()
  // iteration regardless of the sample interval, same as GPSModule::poll().
  void update();

  // Latest cached reading from the most recently completed conversion.
  void read(Ds18b20Reading& out) const { out = last_; }

  bool isPresent() const { return present_; }

 private:
  OneWire onewire_;
  DallasTemperature sensors_;
  bool     present_            = false;
  bool     conversion_pending_ = false;
  uint32_t conversion_start_ms_ = 0;
  uint16_t conversion_wait_ms_  = 750;
  Ds18b20Reading last_;
};
