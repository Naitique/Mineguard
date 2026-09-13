// ===========================================================================
// GPSModule.cpp  --  implementation. See GPSModule.h for the "why".
// ===========================================================================
#include "GPSModule.h"

bool GPSModule::begin(HardwareSerial& serial, uint8_t rx_pin, uint8_t tx_pin,
                      uint32_t baud) {
  serial_ = &serial;
  serial_->begin(baud, SERIAL_8N1, rx_pin, tx_pin);

  // No handshake exists on a bare UART, so we can't just check "did any byte
  // arrive" -- an unconnected RX pin floats and the UART hardware happily
  // reports garbage "bytes" from that noise (confirmed on the bench: a
  // disconnected GPS still showed byte activity, and even charsProcessed()
  // is no better -- TinyGPSPlus increments it unconditionally for every
  // byte encode() sees, verified in TinyGPS++.cpp, so noise inflates it too).
  //
  // passedChecksum() is the real signal: it only increments once a full
  // "$...*XX\r\n" frame was assembled AND its checksum matched (verified in
  // TinyGPS++.cpp) -- noise cannot plausibly fake that. 3s gives an
  // actively-chattering module (~1 Hz bursts at 9600 baud) comfortable
  // margin to produce at least one clean sentence, even with no fix.
  const uint32_t start = millis();
  while (millis() - start < 3000) {
    if (serial_->available()) {
      gps_.encode(serial_->read());
    }
  }

  present_ = gps_.passedChecksum() > 0;
  return present_;
}

void GPSModule::poll() {
  if (!serial_) {
    return;
  }
  while (serial_->available()) {
    gps_.encode(serial_->read());
  }
}

void GPSModule::read(GpsReading& out) {
  out = GpsReading{};

  out.has_fix = gps_.location.isValid();
  if (out.has_fix) {
    out.latitude_deg  = gps_.location.lat();
    out.longitude_deg = gps_.location.lng();
  }
  if (gps_.altitude.isValid()) {
    out.altitude_m = gps_.altitude.meters();
  }
  if (gps_.satellites.isValid()) {
    out.satellites = static_cast<uint32_t>(gps_.satellites.value());
  }
  if (gps_.hdop.isValid()) {
    out.hdop = gps_.hdop.hdop();
  }

  out.sentences_ok     = gps_.passedChecksum();
  out.sentences_failed = gps_.failedChecksum();
}
