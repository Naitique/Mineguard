// ===========================================================================
// MPU6050Sensor  --  direct I2C register driver for the MPU60x0 / MPU65xx
// accel+gyro family, no vendor library.
//
// Why not Adafruit_MPU6050? Its begin() hard-rejects any chip whose WHO_AM_I
// register isn't exactly 0x68 (genuine MPU6050). Many cheap GY-521 breakouts
// actually carry an MPU6500 die instead (WHO_AM_I 0x70) -- a different chip,
// but register-compatible with the MPU6050 for the plain accel/gyro reads
// this project needs: same register addresses, same power-on sequence, same
// LSB-per-unit sensitivity at the full-scale ranges used here. Talking to it
// directly (well-documented public InvenSense register map, not invented)
// lets this module work with either chip, and drops a library dependency
// that turned out to be actively incompatible with the hardware in hand.
//
// Why a wrapper class at all?
//   * main.cpp never touches registers directly, so later phases
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

// WHO_AM_I values this driver accepts as "a usable chip".
static const uint8_t MPU_WHOAMI_MPU6050 = 0x68;  // genuine MPU6050
static const uint8_t MPU_WHOAMI_MPU6500 = 0x70;  // MPU6500 -- common GY-521 clone

// One sample from the sensor.
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
  // Returns false if nothing answers, or a chip answers but its WHO_AM_I
  // doesn't match a recognised MPU6050/MPU6500-family value -- see chipId().
  bool begin(TwoWire& wire, uint8_t address);

  // Read one sample. Returns false (and sets out.valid = false) on I2C error.
  bool read(MpuReading& out);

  // True once begin() has succeeded. Useful for status lines.
  bool isPresent() const { return present_; }

  // WHO_AM_I register value read during begin(). 0 if nothing ever answered.
  uint8_t chipId() const { return chip_id_; }

 private:
  bool writeReg8(uint8_t reg, uint8_t value);
  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len);

  TwoWire* wire_ = nullptr;
  uint8_t  addr_ = 0;
  uint8_t  chip_id_ = 0;
  bool     present_ = false;
};
