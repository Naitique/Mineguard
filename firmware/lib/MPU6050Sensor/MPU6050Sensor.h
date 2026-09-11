// ===========================================================================
// MPU6050Sensor  --  thin, readable wrapper around Adafruit's MPU6050 driver.
//
// Why a wrapper?
//   * main.cpp never talks to the Adafruit library directly, so later phases
//     (calibration offsets in Phase 4, filtering) have one place to live.
//   * We keep BOTH the raw accel/gyro values and the derived roll/pitch in the
//     same struct. The AI team may want the raw time series later, so raw data
//     is never thrown away here.
//
// PHASE 1: no calibration, no filtering. Just read + convert + derive angles.
// ===========================================================================
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>

// One sample from the MPU6050.
struct MpuReading {
  // --- Raw / primary measurements ---
  float accel_x_g = 0.0f;   // acceleration, units of g (1 g = 9.80665 m/s^2)
  float accel_y_g = 0.0f;
  float accel_z_g = 0.0f;

  float gyro_x_dps = 0.0f;  // angular rate, degrees per second
  float gyro_y_dps = 0.0f;
  float gyro_z_dps = 0.0f;

  // --- Derived from the accelerometer only (Phase 1) ---
  float roll_deg  = 0.0f;   // rotation about X axis
  float pitch_deg = 0.0f;   // rotation about Y axis

  // True only if this sample was read successfully.
  bool valid = false;
};

class MPU6050Sensor {
 public:
  // Initialise the sensor on the given I2C bus at the given 7-bit address.
  // Returns false if the device does not respond (missing / miswired).
  bool begin(TwoWire& wire, uint8_t address);

  // Read one sample. Returns false (and sets out.valid = false) on I2C error.
  bool read(MpuReading& out);

  // True once begin() has succeeded. Useful for status lines.
  bool isPresent() const { return present_; }

 private:
  Adafruit_MPU6050 mpu_;
  bool present_ = false;
};
