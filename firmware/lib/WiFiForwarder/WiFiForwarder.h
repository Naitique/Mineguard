// ===========================================================================
// WiFiForwarder  --  pushes one JSON-encoded LoRa packet at a time to a
// listener on the analysis laptop over the local Wi-Fi network.
//
// This is separate from (and independent of) the LoRa node <-> node link:
// LoRaTransport moves packets ESP32 <-> ESP32, this module moves them
// ESP32 (receiver) -> laptop, matching the project's eventual
// ESP32 <-> Raspberry Pi/laptop Wi-Fi path. Only the RECEIVER board needs it.
// ===========================================================================
#pragma once

#include <Arduino.h>

class WiFiForwarder {
 public:
  // Join the network and block (up to timeout_ms) until connected.
  //   returns false if the credentials are wrong, the AP is out of range, or
  //   it's a 5 GHz-only network (the ESP32 is 2.4 GHz only).
  bool begin(const char* ssid, const char* password, uint32_t timeout_ms);

  // HTTP POST json_body to http://host:port/path. Blocks until the request
  // completes or times out.
  //   returns false if not connected, unreachable, or a non-2xx response.
  bool send(const char* host, uint16_t port, const char* path,
            const String& json_body);

 private:
  bool ready_ = false;
};
