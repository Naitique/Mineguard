// ===========================================================================
// DS18B20Sensor.cpp  --  implementation. See DS18B20Sensor.h for the "why".
// ===========================================================================
#include "DS18B20Sensor.h"

bool DS18B20Sensor::begin(uint8_t pin) {
  present_            = false;
  conversion_pending_ = false;

  onewire_.begin(pin);
  sensors_.setOneWire(&onewire_);
  sensors_.begin();
  sensors_.setWaitForConversion(false);  // we manage the wait ourselves

  if (sensors_.getDeviceCount() == 0) {
    return false;  // nothing answered on the bus -- almost always the pull-up
  }

  conversion_wait_ms_ = sensors_.millisToWaitForConversion(sensors_.getResolution());

  sensors_.requestTemperatures();
  conversion_start_ms_ = millis();
  conversion_pending_  = true;

  present_ = true;
  return true;
}

void DS18B20Sensor::update() {
  if (!present_ || !conversion_pending_) {
    return;
  }
  if (millis() - conversion_start_ms_ < conversion_wait_ms_) {
    return;  // conversion still in progress -- never block waiting for it
  }

  const float c = sensors_.getTempCByIndex(0);
  last_.temperature_c = c;
  last_.valid          = (c != DEVICE_DISCONNECTED_C);

  // Immediately start the next conversion so readings keep flowing.
  sensors_.requestTemperatures();
  conversion_start_ms_ = millis();
}
