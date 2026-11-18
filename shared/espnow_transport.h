// ESP-NOW transport (ESP32 Arduino only).
//
//  * fixed Wi-Fi channel, STA mode without any AP connection, power save off
//  * PMK + per-peer LMK, encrypted unicast peers only (no broadcast peer)
//  * the receive callback only checks the length and that the sender MAC is a
//    registered peer, copies the frame into a FreeRTOS queue and returns;
//    decoding and every actuator action happen in loop()
//  * absorbs the callback signature differences between Arduino-ESP32 2.x
//    (IDF 4.4) and 3.x (IDF 5.x, incl. the IDF 5.5 send-callback change)
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#if defined(ARDUINO)

namespace ufo {

struct RxFrame {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[kMaxPacketSize];
  int8_t rssi;  // 0 = unknown (Arduino-ESP32 2.x does not report it)
};

struct TransportStats {
  volatile uint32_t rx_callbacks = 0;
  volatile uint32_t rx_dropped_unknown_mac = 0;  // incl. broadcasts from other exhibits
  volatile uint32_t rx_dropped_length = 0;
  volatile uint32_t rx_dropped_queue_full = 0;
  volatile uint32_t tx_ok = 0;       // MAC-layer only: NOT an application ACK
  volatile uint32_t tx_fail = 0;     // MAC-layer delivery failure
  volatile uint32_t tx_errors = 0;   // esp_now_send() returned an error
};

class EspNowTransport {
 public:
  static constexpr size_t kMaxPeers = 6;

  // Initializes Wi-Fi (STA, no AP), the channel, ESP-NOW and the PMK.
  bool begin(uint8_t channel, const uint8_t pmk[16], size_t rx_queue_len = 16);
  // Registers an encrypted unicast peer. Rejects zero / broadcast / multicast MACs.
  bool addPeer(const uint8_t mac[6], const uint8_t lmk[16]);
  bool send(const uint8_t mac[6], const uint8_t* data, size_t len);
  // Pops one received frame (non-blocking).
  bool poll(RxFrame& out);
  void selfMac(uint8_t out[6]) const;
  uint8_t channel() const { return channel_; }
  const TransportStats& stats() const { return stats_; }
  size_t peerCount() const { return peer_count_; }

  // internal (called from the static callbacks)
  void handleRx(const uint8_t* mac, const uint8_t* data, int len, int8_t rssi);
  void handleTx(bool ok);

 private:
  bool isPeer(const uint8_t* mac) const;
  void* queue_ = nullptr;
  bool ready_ = false;  // begin() succeeded; send() is refused until then
  uint8_t channel_ = 1;
  uint8_t peers_[kMaxPeers][6] = {{0}};
  volatile size_t peer_count_ = 0;
  TransportStats stats_;
};

}  // namespace ufo

#endif  // ARDUINO
