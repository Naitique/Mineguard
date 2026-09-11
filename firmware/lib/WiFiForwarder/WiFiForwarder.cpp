// ===========================================================================
// WiFiForwarder.cpp  --  implementation. See WiFiForwarder.h for the "why".
// ===========================================================================
#include "WiFiForwarder.h"

#include <HTTPClient.h>
#include <WiFi.h>

bool WiFiForwarder::begin(const char* ssid, const char* password,
                          uint32_t timeout_ms) {
  ready_ = false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < timeout_ms) {
    delay(200);
  }

  ready_ = (WiFi.status() == WL_CONNECTED);
  return ready_;
}

bool WiFiForwarder::send(const char* host, uint16_t port, const char* path,
                         const String& json_body) {
  if (!ready_ || WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  const String url = String("http://") + host + ":" + String(port) + path;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(2000);  // don't let a dead/unreachable listener stall loop()

  const int status = http.POST(json_body);
  http.end();

  return status > 0 && status < 300;
}
