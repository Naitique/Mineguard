// ===========================================================================
// LoRaTransport.cpp  --  implementation. See LoRaTransport.h for the "why".
//
// Uses the global `LoRa` object from sandeepmistry/LoRa.
// ===========================================================================
#include "LoRaTransport.h"

#include <SPI.h>
#include <LoRa.h>

bool LoRaTransport::begin(uint8_t sck, uint8_t miso, uint8_t mosi, uint8_t nss,
                          uint8_t rst, uint8_t dio0,
                          long frequency_hz, int spreading_factor,
                          long bandwidth_hz, int coding_rate_denom,
                          int sync_word, int tx_power_dbm) {
  ready_ = false;

  // Bind the VSPI bus to our explicit pins (don't rely on board defaults).
  SPI.begin(sck, miso, mosi, nss);
  LoRa.setPins(nss, rst, dio0);

  // LoRa.begin() resets the module and reads its version register; it returns
  // 0 if the chip is not an SX127x that responds -- i.e. an SPI/wiring fault.
  if (!LoRa.begin(frequency_hz)) {
    return false;
  }

  // Link parameters -- must be identical on both ends.
  LoRa.setSpreadingFactor(spreading_factor);
  LoRa.setSignalBandwidth(bandwidth_hz);
  LoRa.setCodingRate4(coding_rate_denom);
  LoRa.setSyncWord(sync_word);

  // The Ra-02 routes its output through the PA_BOOST pin, which is this
  // library's default second argument to setTxPower().
  LoRa.setTxPower(tx_power_dbm);

  LoRa.enableCrc();  // drop corrupted frames instead of handing up garbage

  ready_ = true;
  return true;
}

bool LoRaTransport::send(const String& payload) {
  if (!ready_) {
    return false;
  }
  LoRa.beginPacket();
  LoRa.print(payload);
  // endPacket() returns 1 on success; it blocks until the packet is on air.
  return LoRa.endPacket() == 1;
}

bool LoRaTransport::receive(LoRaRxPacket& out) {
  const int size = LoRa.parsePacket();
  if (size <= 0) {
    return false;  // nothing waiting
  }

  out.payload = "";
  out.payload.reserve(size);
  while (LoRa.available()) {
    out.payload += static_cast<char>(LoRa.read());
  }
  out.rssi_dbm = LoRa.packetRssi();
  out.snr_db   = LoRa.packetSnr();
  return true;
}
