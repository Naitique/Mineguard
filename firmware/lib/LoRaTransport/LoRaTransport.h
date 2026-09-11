// ===========================================================================
// LoRaTransport  --  thin wrapper around the sandeepmistry/LoRa driver for the
// AI-Thinker Ra-02 (SX1278).
//
// Same pattern as the sensor modules: main.cpp never touches the vendor
// library directly. This is the node <-> node radio link ONLY; the ESP32 <->
// Raspberry Pi path is Wi-Fi and lives elsewhere.
//
// PHASE 8: bring-up. begin() + a plain send()/receive() of text payloads.
// Structured JSON-over-LoRa comes after the JSON packet format (Phase 5).
// ===========================================================================
#pragma once

#include <Arduino.h>

// One received LoRa frame.
struct LoRaRxPacket {
  String payload;         // raw bytes of the frame, as text
  int    rssi_dbm = 0;    // received signal strength (more negative = weaker)
  float  snr_db   = 0.0f; // signal-to-noise ratio
};

class LoRaTransport {
 public:
  // Configure SPI + radio and put the module in standby.
  //   returns false if the SX1278 does not respond over SPI (bad wiring, no
  //   power, wrong pins) -- the driver checks the chip version register.
  // All radio parameters must match on the other end of the link.
  bool begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t nss,
             uint8_t rst, uint8_t dio0,
             long frequency_hz, int spreading_factor, long bandwidth_hz,
             int coding_rate_denom, int sync_word, int tx_power_dbm);

  // Send one text payload. Blocks until transmission completes.
  //   returns false if not initialised or the transmit failed.
  bool send(const String& payload);

  // Non-blocking check for an incoming frame.
  //   returns true and fills 'out' if a frame was waiting, false otherwise.
  bool receive(LoRaRxPacket& out);

 private:
  bool ready_ = false;
};
