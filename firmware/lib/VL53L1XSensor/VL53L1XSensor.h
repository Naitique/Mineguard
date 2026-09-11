// ===========================================================================
// VL53L1XSensor  --  thin, readable wrapper around Pololu's VL53L1X driver.
//
// Same idea as MPU6050Sensor: main.cpp never touches the vendor library, so
// baseline/delta-distance logic (Phase 4) has one place to live later.
//
// The reading keeps BOTH the raw distance and the raw range-status code, so a
// rejected measurement can still be logged and understood -- we never silently
// drop a sample.
//
// PHASE 2: detect the sensor, read distance in continuous mode, classify each
// reading as usable or rejected. No baseline, no delta, no filtering yet.
// ===========================================================================
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <VL53L1X.h>  // Pololu VL53L1X library

// One distance sample from the VL53L1X.
struct TofReading {
  uint16_t distance_mm = 0;        // raw distance reported by the sensor

  // true only when the sensor produced a clean range. When false, the other
  // fields still hold the raw values so the caller can log why it failed.
  bool valid = false;

  uint8_t     range_status = 255;      // raw VL53L1X status code (255 = "None")
  const char* status_text  = "None";  // human-readable form of range_status
};

class VL53L1XSensor {
 public:
  // Initialise on the given I2C bus and start continuous ranging.
  //   long_range           : true = Long distance mode, false = Short
  //   timing_budget_us     : per-measurement integration time (microseconds)
  //   intermeasurement_ms  : gap between measurements in continuous mode (ms)
  //   io_timeout_ms        : I2C read timeout (ms)
  // Returns false if the sensor does not respond.
  bool begin(TwoWire& wire, bool long_range, uint32_t timing_budget_us,
             uint32_t intermeasurement_ms, uint16_t io_timeout_ms);

  // Read the most recent continuous measurement.
  //   returns false          -> I2C timeout (sensor not responding)
  //   returns true, valid==false -> sensor answered but the range is unusable
  //   returns true, valid==true  -> good distance in out.distance_mm
  bool read(TofReading& out);

  // True once begin() has succeeded.
  bool isPresent() const { return present_; }

 private:
  VL53L1X sensor_;
  bool present_ = false;
};
