// ===========================================================================
// SyntheticData  --  fabricated sensor readings for exercising the node <->
// node LoRa link while the real MPU6050 / VL53L1X hardware is being replaced.
//
// This is TEMPORARY bench scaffolding, NOT part of the measurement chain:
//   * every value here is made up (slow sine waves + random noise);
//   * the waveforms are arbitrary -- they do NOT model ground behaviour and
//     imply nothing about subsidence or collapse;
//   * payloads built from this are tagged "synth=1" so nothing downstream
//     ever treats them as a real reading.
// Delete this module and ENABLE_SYNTHETIC_DATA (config.h) once real sensor
// data feeds the packet.
//
// Same shape as the real sensor wrappers: begin() + read(<Reading>&), with a
// plain struct that carries both raw and derived values.
// ===========================================================================
#pragma once

#include <Arduino.h>

struct SyntheticReading {
  // Only the fields formatSyntheticPayload() (main.cpp) actually puts on the
  // wire. Not a full mirror of MpuReading/TofReading -- add fields here only
  // when something reads them.
  float    accel_z_g   = 1.0f;
  float    gyro_z_dps  = 0.0f;
  uint16_t dist_mm     = 0;
  uint8_t  range_status = 0;   // 0 == "RangeValid", matching VL53L1XSensor

  // derived values
  float roll_deg  = 0.0f;
  float pitch_deg = 0.0f;
};

class SyntheticData {
 public:
  // Seed the noise generator. Call once from setup().
  void begin(uint32_t seed);

  // Fill 'out' with the next fabricated sample (driven by millis()).
  void read(SyntheticReading& out);

 private:
  float noise(float amplitude);
};
