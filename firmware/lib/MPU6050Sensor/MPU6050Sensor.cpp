// ===========================================================================
// MPU6050Sensor.cpp  --  implementation. See MPU6050Sensor.h for the "why".
//
// Register addresses and the power-on sequence are the public InvenSense
// MPU-60x0 / MPU-65xx register map -- documented, not vendor-invented, and
// shared across the MPU6050/6500/9250/9255 family for these basic registers.
// ===========================================================================
#include "MPU6050Sensor.h"

#include <math.h>

namespace {
constexpr uint8_t REG_SMPLRT_DIV   = 0x19;
constexpr uint8_t REG_CONFIG       = 0x1A;  // DLPF_CFG
constexpr uint8_t REG_GYRO_CONFIG  = 0x1B;  // FS_SEL, bits [4:3]
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;  // AFS_SEL, bits [4:3]
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;  // 14-byte burst: accel,temp,gyro
constexpr uint8_t REG_PWR_MGMT_1   = 0x6B;
constexpr uint8_t REG_WHO_AM_I     = 0x75;

constexpr uint8_t PWR1_DEVICE_RESET      = 0x80;
constexpr uint8_t PWR1_CLKSEL_PLL_GYRO_X = 0x01;

// Full-scale ranges used here: +/-2 g / +/-250 deg/s -- best resolution for
// near-static ground-tilt monitoring (a ~1 g signal, slow rotation).
constexpr uint8_t ACCEL_AFS_SEL_2G    = 0x00;
constexpr uint8_t GYRO_FS_SEL_250DPS  = 0x00;
constexpr float   ACCEL_LSB_PER_G     = 16384.0f;  // datasheet constant @ +/-2g
constexpr float   GYRO_LSB_PER_DPS    = 131.0f;    // datasheet constant @ +/-250dps

// DLPF_CFG = 4 -> ~21 Hz accel / ~20 Hz gyro bandwidth, 1 kHz internal rate.
constexpr uint8_t DLPF_CFG_21HZ = 0x04;

constexpr float kRadToDeg = 57.2957795131f;
}  // namespace

bool MPU6050Sensor::writeReg8(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool MPU6050Sensor::readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  wire_->beginTransmission(addr_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) {  // repeated start, keep bus held
    return false;
  }
  if (wire_->requestFrom(addr_, len) != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    buf[i] = wire_->read();
  }
  return true;
}

bool MPU6050Sensor::begin(TwoWire& wire, uint8_t address) {
  present_ = false;
  chip_id_ = 0;
  wire_    = &wire;
  addr_    = address;

  uint8_t who_am_i = 0;
  if (!readRegs(REG_WHO_AM_I, &who_am_i, 1)) {
    return false;  // nothing answered at this address
  }
  chip_id_ = who_am_i;

  if (who_am_i != MPU_WHOAMI_MPU6050 && who_am_i != MPU_WHOAMI_MPU6500) {
    return false;  // answered, but not a chip ID we recognise
  }

  // Power-on sequence: reset, then select the PLL clock referenced to the
  // X gyro (more stable than the internal RC oscillator).
  if (!writeReg8(REG_PWR_MGMT_1, PWR1_DEVICE_RESET)) return false;
  delay(100);
  if (!writeReg8(REG_PWR_MGMT_1, PWR1_CLKSEL_PLL_GYRO_X)) return false;
  delay(10);

  if (!writeReg8(REG_SMPLRT_DIV, 0x00)) return false;
  if (!writeReg8(REG_CONFIG, DLPF_CFG_21HZ)) return false;
  if (!writeReg8(REG_GYRO_CONFIG, GYRO_FS_SEL_250DPS << 3)) return false;
  if (!writeReg8(REG_ACCEL_CONFIG, ACCEL_AFS_SEL_2G << 3)) return false;

  present_ = true;
  return true;
}

bool MPU6050Sensor::read(MpuReading& out) {
  out = MpuReading{};

  if (!present_) {
    return false;
  }

  uint8_t raw[14];
  if (!readRegs(REG_ACCEL_XOUT_H, raw, sizeof(raw))) {
    return false;
  }

  auto toI16 = [](uint8_t hi, uint8_t lo) -> int16_t {
    return static_cast<int16_t>((hi << 8) | lo);
  };

  const int16_t ax = toI16(raw[0], raw[1]);
  const int16_t ay = toI16(raw[2], raw[3]);
  const int16_t az = toI16(raw[4], raw[5]);
  // raw[6..7] = temperature -- not modelled in MpuReading, unused here.
  const int16_t gx = toI16(raw[8], raw[9]);
  const int16_t gy = toI16(raw[10], raw[11]);
  const int16_t gz = toI16(raw[12], raw[13]);

  out.accel_x_g = ax / ACCEL_LSB_PER_G;
  out.accel_y_g = ay / ACCEL_LSB_PER_G;
  out.accel_z_g = az / ACCEL_LSB_PER_G;

  out.gyro_x_dps = gx / GYRO_LSB_PER_DPS;
  out.gyro_y_dps = gy / GYRO_LSB_PER_DPS;
  out.gyro_z_dps = gz / GYRO_LSB_PER_DPS;

  // Tilt from the gravity vector (accelerometer only). Axis convention (chip
  // flat, USB toward you): +X right, +Y away, +Z up (~+1 g at rest). Noisy,
  // and wrong under linear acceleration -- a gyro complementary filter is
  // Phase 4, not here.
  out.roll_deg  = atan2f(out.accel_y_g, out.accel_z_g) * kRadToDeg;
  out.pitch_deg = atan2f(-out.accel_x_g,
                         sqrtf(out.accel_y_g * out.accel_y_g +
                               out.accel_z_g * out.accel_z_g)) *
                  kRadToDeg;

  // Phase 4: calibration offsets (accel/gyro bias, zero-orientation) applied
  // here, before roll/pitch, once the simple pipeline is proven.

  out.valid = true;
  return true;
}
