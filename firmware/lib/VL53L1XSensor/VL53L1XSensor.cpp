// ===========================================================================
// VL53L1XSensor.cpp  --  implementation. See VL53L1XSensor.h for the "why".
// ===========================================================================
#include "VL53L1XSensor.h"

bool VL53L1XSensor::begin(TwoWire& wire, bool long_range,
                          uint32_t timing_budget_us,
                          uint32_t intermeasurement_ms,
                          uint16_t io_timeout_ms) {
  present_ = false;

  sensor_.setBus(&wire);
  sensor_.setTimeout(io_timeout_ms);

  // init() runs the full ST start-up sequence and checks the model ID over
  // I2C. false here means the sensor did not respond / is not a VL53L1X.
  if (!sensor_.init()) {
    return false;
  }

  sensor_.setDistanceMode(long_range ? VL53L1X::Long : VL53L1X::Short);
  sensor_.setMeasurementTimingBudget(timing_budget_us);

  // Continuous mode: the sensor ranges on its own timer; read() just returns
  // the latest result. The period must be >= the timing budget.
  sensor_.startContinuous(intermeasurement_ms);

  present_ = true;
  return true;
}

bool VL53L1XSensor::read(TofReading& out) {
  out = TofReading{};

  if (!present_) {
    return false;
  }

  // Blocking read, bounded by setTimeout(). The raw distance is returned
  // regardless of quality; the status code tells us whether to trust it.
  const uint16_t mm = sensor_.read();

  if (sensor_.timeoutOccurred()) {
    return false;  // sensor stopped responding on the bus
  }

  const VL53L1X::RangeStatus status = sensor_.ranging_data.range_status;

  out.distance_mm  = mm;
  out.range_status = static_cast<uint8_t>(status);
  out.status_text  = VL53L1X::rangeStatusToString(status);

  // Accept only a clean range, or "valid but wrap-check skipped" (status 6),
  // which is still a trustworthy short/medium-distance reading. Everything
  // else (SigmaFail, SignalFail, OutOfBounds, HardwareFail, ...) is a
  // rejected sample -- reported by the caller, never quietly kept.
  out.valid = (status == VL53L1X::RangeValid ||
               status == VL53L1X::RangeValidNoWrapCheckFail);

  return true;
}
