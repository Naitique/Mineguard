// ===========================================================================
// MPU6050Sensor.cpp  --  implementation. See MPU6050Sensor.h for the "why".
// ===========================================================================
#include "MPU6050Sensor.h"

#include <math.h>

namespace {
// 1 g in m/s^2. The Adafruit driver reports acceleration in m/s^2; the
// project data model uses g, so we convert on the way out.
constexpr float kGravity = 9.80665f;

// The Adafruit driver reports gyro rate in rad/s; we want deg/s.
constexpr float kRadToDeg = 57.2957795131f;  // 180 / pi
}  // namespace

bool MPU6050Sensor::begin(TwoWire& wire, uint8_t address) {
  present_ = false;

  // mpu_.begin() performs the I2C "who am I" check; false means no response.
  if (!mpu_.begin(address, &wire)) {
    return false;
  }

  // Ranges chosen for near-static ground-tilt monitoring:
  //   +/-2 g       -> best accelerometer resolution; enough for a 1 g tilt
  //                   vector. Revisit if strong vibration clips the reading.
  //   +/-250 deg/s -> best gyro resolution; ground motion is slow.
  //   21 Hz DLPF   -> on-chip low-pass to knock down high-frequency noise
  //                   before it ever reaches us.
  mpu_.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu_.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu_.setFilterBandwidth(MPU6050_BAND_21_HZ);

  present_ = true;
  return true;
}

bool MPU6050Sensor::read(MpuReading& out) {
  out = MpuReading{};  // reset, valid stays false until we finish

  if (!present_) {
    return false;
  }

  sensors_event_t accel;
  sensors_event_t gyro;
  sensors_event_t temp;  // required by the API; unused in Phase 1
  if (!mpu_.getEvent(&accel, &gyro, &temp)) {
    return false;  // I2C read failed
  }

  // --- Unit conversion, raw values preserved in the struct ---
  out.accel_x_g = accel.acceleration.x / kGravity;
  out.accel_y_g = accel.acceleration.y / kGravity;
  out.accel_z_g = accel.acceleration.z / kGravity;

  out.gyro_x_dps = gyro.gyro.x * kRadToDeg;
  out.gyro_y_dps = gyro.gyro.y * kRadToDeg;
  out.gyro_z_dps = gyro.gyro.z * kRadToDeg;

  // --- Tilt from the gravity vector (accelerometer only) ---
  // Axis convention (chip flat, components up, USB toward you):
  //   +X points right, +Y points away, +Z points up (~ +1 g at rest).
  //   roll  = rotation about X, tips the Y axis down/up
  //   pitch = rotation about Y, tips the X axis down/up
  // This is accel-only, so it is noisy and wrong during linear acceleration.
  // A gyro complementary filter is added in Phase 4 -- not here.
  const float ax = out.accel_x_g;
  const float ay = out.accel_y_g;
  const float az = out.accel_z_g;

  out.roll_deg  = atan2f(ay, az) * kRadToDeg;
  out.pitch_deg = atan2f(-ax, sqrtf(ay * ay + az * az)) * kRadToDeg;

  // Phase 4: calibration offsets (accel/gyro bias, zero-orientation) applied
  // here, before roll/pitch, once the simple pipeline is proven.

  out.valid = true;
  return true;
}
