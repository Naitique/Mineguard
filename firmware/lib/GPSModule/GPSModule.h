// ===========================================================================
// GPSModule  --  thin wrapper around TinyGPSPlus for a plain NMEA GPS module
// (e.g. NEO-6M / NEO-M8N) on the ESP32's second hardware UART.
//
// Library choice: mikalhart/TinyGPSPlus is the de-facto standard Arduino NMEA
// parser -- minimal, mature, well documented. We feed it raw bytes from a
// HardwareSerial and read back its already-parsed fix fields; no NMEA
// sentence handling of our own.
//
// A bare UART has no address/ID register the way I2C does, so "is a sensor
// here" can't be answered by asking the chip -- begin() instead watches for
// ANY byte to arrive in a bounded window. Silence there means wiring/baud/
// power, not "no fix yet" (a fix genuinely can take 30s-minutes outdoors, and
// may never arrive indoors).
//
// PHASE: bring-up only. No baseline/calibration -- just report what the
// module says, raw fields alongside whatever TinyGPSPlus derives for us.
// ===========================================================================
#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

struct GpsReading {
  bool has_fix = false;

  // Derived by TinyGPSPlus from the raw NMEA fields -- this library's output
  // *is* the "processed" tier for a GPS; there is no lower-level raw form we
  // could usefully keep instead.
  double   latitude_deg  = 0.0;
  double   longitude_deg = 0.0;
  double   altitude_m    = 0.0;
  uint32_t satellites    = 0;
  double   hdop          = 0.0;  // horizontal dilution of precision (lower = better)

  // Diagnostics: lets "wired, module talking, just no fix yet" be told apart
  // from "wiring/baud is wrong" even before/without a fix.
  uint32_t sentences_ok     = 0;
  uint32_t sentences_failed = 0;
};

class GPSModule {
 public:
  // Starts the given HardwareSerial at 8N1 on rx_pin/tx_pin, then watches for
  // up to ~2s for any byte to arrive. Returns false if nothing was seen
  // (check wiring/baud/power); true means the module is talking, independent
  // of whether it has a satellite fix yet.
  bool begin(HardwareSerial& serial, uint8_t rx_pin, uint8_t tx_pin, uint32_t baud);

  // Drain all bytes currently waiting in the UART buffer into the NMEA
  // parser. Call every loop() iteration (not just on the sample interval) --
  // GPS data streams continuously and an unread UART buffer will overflow.
  void poll();

  // Snapshot of the latest parsed state. Always "succeeds"; check
  // out.has_fix (and out.sentences_ok) to judge whether it's useful yet.
  // Not const: TinyGPSPlus's accessors clear an internal "updated" flag.
  void read(GpsReading& out);

  bool isPresent() const { return present_; }

 private:
  TinyGPSPlus     gps_;
  HardwareSerial* serial_ = nullptr;
  bool            present_ = false;
};
