#if defined(ARDUINO)

#include "espnow_transport.h"

#include <esp_system.h>

#include <Arduino.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <string.h>

namespace ufo {

namespace {
EspNowTransport* g_instance = nullptr;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!g_instance || !info) return;
  int8_t rssi = info->rx_ctrl ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
  g_instance->handleRx(info->src_addr, data, len, rssi);
}
#else
void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (!g_instance) return;
  g_instance->handleRx(mac, data, len, 0);
}
#endif

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onSent(const wifi_tx_info_t* /*info*/, esp_now_send_status_t status) {
  if (g_instance) g_instance->handleTx(status == ESP_NOW_SEND_SUCCESS);
}
#else
void onSent(const uint8_t* /*mac*/, esp_now_send_status_t status) {
  if (g_instance) g_instance->handleTx(status == ESP_NOW_SEND_SUCCESS);
}
#endif
}  // namespace

bool EspNowTransport::begin(uint8_t channel, const uint8_t pmk[16], size_t rx_queue_len) {
  g_instance = this;
  channel_ = channel;
  queue_ = xQueueCreate(rx_queue_len, sizeof(RxFrame));
  if (!queue_) return false;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  // ESP-NOW only, never associate with an AP
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) return false;
  if (esp_now_set_pmk(pmk) != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);
  ready_ = true;
  return true;
}

bool EspNowTransport::addPeer(const uint8_t mac[6], const uint8_t lmk[16]) {
  if (peer_count_ >= kMaxPeers) return false;
  if (macIsZero(mac) || (mac[0] & 0x01)) return false;  // zero / broadcast / multicast
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = channel_;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = true;
  memcpy(peer.lmk, lmk, 16);
  if (esp_now_is_peer_exist(mac)) esp_now_del_peer(mac);
  if (esp_now_add_peer(&peer) != ESP_OK) return false;
  memcpy(peers_[peer_count_], mac, 6);
  peer_count_ = peer_count_ + 1;
  return true;
}

bool EspNowTransport::isPeer(const uint8_t* mac) const {
  for (size_t i = 0; i < peer_count_; ++i)
    if (macEqual(peers_[i], mac)) return true;
  return false;
}

void EspNowTransport::handleRx(const uint8_t* mac, const uint8_t* data, int len, int8_t rssi) {
  // Radio task context: minimal checks + enqueue only.
  stats_.rx_callbacks = stats_.rx_callbacks + 1;
  if (!mac || !data || len < static_cast<int>(kHeaderSize) || len > static_cast<int>(kMaxPacketSize)) {
    stats_.rx_dropped_length = stats_.rx_dropped_length + 1;
    return;
  }
  if (!isPeer(mac)) {
    stats_.rx_dropped_unknown_mac = stats_.rx_dropped_unknown_mac + 1;
    return;
  }
  RxFrame f;
  memcpy(f.mac, mac, 6);
  f.len = static_cast<uint8_t>(len);
  memcpy(f.data, data, len);
  f.rssi = rssi;
  if (xQueueSend(static_cast<QueueHandle_t>(queue_), &f, 0) != pdTRUE) {
    stats_.rx_dropped_queue_full = stats_.rx_dropped_queue_full + 1;
  }
}

void EspNowTransport::handleTx(bool ok) {
  if (ok)
    stats_.tx_ok = stats_.tx_ok + 1;
  else
    stats_.tx_fail = stats_.tx_fail + 1;
}

bool EspNowTransport::send(const uint8_t mac[6], const uint8_t* data, size_t len) {
  if (len == 0 || len > kMaxPacketSize) return false;
  if (!ready_) return false;  // radio not started (config error): esp_now_send would crash
  esp_err_t r = esp_now_send(mac, data, len);
  if (r != ESP_OK) {
    stats_.tx_errors = stats_.tx_errors + 1;
    return false;
  }
  return true;
}

bool EspNowTransport::poll(RxFrame& out) {
  if (!queue_) return false;
  return xQueueReceive(static_cast<QueueHandle_t>(queue_), &out, 0) == pdTRUE;
}

// Factory STA MAC from efuse: valid even when the radio was not started.
void EspNowTransport::selfMac(uint8_t out[6]) const { esp_read_mac(out, ESP_MAC_WIFI_STA); }

}  // namespace ufo

#endif  // ARDUINO
