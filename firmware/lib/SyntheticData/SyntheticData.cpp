// ===========================================================================
// SyntheticData.cpp  --  implementation. See SyntheticData.h for the "why".
//
// All numbers below (amplitudes, periods, baseline distance) are arbitrary
// bench values chosen only so the serial log visibly moves. They are not a
// model of anything physical.
// ===========================================================================
#include "SyntheticData.h"

#include <math.h>

void SyntheticData::begin(uint32_t seed) {
  randomSeed(seed);
}

float SyntheticData::noise(float amplitude) {
  // Uniform in [-amplitude, +amplitude].
  return (static_cast<float>(random(-1000, 1001)) / 1000.0f) * amplitude;
}

void SyntheticData::read(SyntheticReading& out) {
  const float t = millis() / 1000.0f;

  // Two slow tilt drifts of a couple of degrees (periods 37 s and 91 s).
  out.roll_deg  = 1.5f * sinf(t * (2.0f * PI / 37.0f))        + noise(0.05f);
  out.pitch_deg = 1.0f * sinf(t * (2.0f * PI / 91.0f) + 1.0f) + noise(0.05f);

  // Accelerometer Z: gravity, plus small wobble.
  out.accel_z_g = 1.0f + noise(0.01f);

  // Gyroscope Z: essentially at rest.
  out.gyro_z_dps = noise(0.3f);
}
