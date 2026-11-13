// Interference / coexistence test tool (any ESP32, e.g. a spare ATOM).
//
// Simulates "other exhibits" on the same channel and an attacker without keys:
//   1. NeoGacha-style 5-byte broadcast frames (like the existing gacha system)
//   2. random-length junk broadcasts
//   3. well-formed UFO frames (valid magic/version, source=HUB) sent as BROADCAST
//      carrying RELEASE_BALL / RELAY_PULSE / STOP_ALL
//   4. the same forged frames as UNENCRYPTED UNICAST to each provisioned device
//   5. (optional, -DNOISE_SPOOF_HUB_MAC=1) the same with this board's MAC set to
//      the hub MAC but WITHOUT the correct LMK -> must still be rejected
// Expected: no device acts on any of these; their counters (GET_DIAG /
// STATUS / heartbeat rx_rejected) grow, real traffic keeps working (with
// higher latency / loss which the diag counters make visible).
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "device_ids.h"
#include "messages.h"
#include "ufo_config.h"

#ifndef NOISE_INTERVAL_MS
#define NOISE_INTERVAL_MS 5
#endif
#ifndef NOISE_SPOOF_HUB_MAC
#define NOISE_SPOOF_HUB_MAC 0
#endif

using namespace ufo;

namespace {
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint32_t g_sent = 0, g_fail = 0, g_round = 0;

void addPlainPeer(const uint8_t* mac) {
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = wifiChannel();
  p.ifidx = WIFI_IF_STA;
  p.encrypt = false;  // no keys: this is the "other exhibit" / attacker
  esp_now_add_peer(&p);
}

void sendRaw(const uint8_t* mac, const uint8_t* d, size_t n) {
  if (esp_now_send(mac, d, n) == ESP_OK)
    ++g_sent;
  else
    ++g_fail;
}

size_t forge(uint8_t dest, uint8_t* out, uint8_t which) {
  Packet p;
  p.h.source = kHubId;
  p.h.destination = dest;
  p.h.type = MsgType::Command;
  p.h.sequence = static_cast<uint16_t>(g_round);
  p.h.session = esp_random();
  p.h.txn = 0x00010000 + g_round;
  switch (which % 3) {
    case 0: p.h.payload_len = encodeCommand(CommandCode::ReleaseBall, p.payload, sizeof(p.payload)); break;
    case 1: {
      RelayPulseParams rp;
      rp.duration_ms = 3000;
      p.h.payload_len = encodeRelayPulse(rp, p.payload, sizeof(p.payload));
      break;
    }
    default: p.h.payload_len = encodeCommand(CommandCode::StopAll, p.payload, sizeof(p.payload)); break;
  }
  return encodePacket(p, out, kMaxPacketSize);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
#if NOISE_SPOOF_HUB_MAC
  const DeviceEntry* hub = findDevice(kHubId);
  if (hub) esp_wifi_set_mac(WIFI_IF_STA, hub->mac);
#endif
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(wifiChannel(), WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  esp_now_init();
  addPlainPeer(kBroadcast);
  for (size_t i = 0; i < deviceCount(); ++i) addPlainPeer(deviceAt(i).mac);
  Serial.printf("noise broadcaster on channel %u, interval %d ms, spoof_hub_mac=%d\n", wifiChannel(),
                NOISE_INTERVAL_MS, NOISE_SPOOF_HUB_MAC);
}

void loop() {
  static uint32_t last = 0, last_log = 0;
  uint32_t now = millis();
  if (now - last >= NOISE_INTERVAL_MS) {
    last = now;
    ++g_round;
    uint8_t buf[kMaxPacketSize];
    switch (g_round % 4) {
      case 0: {  // NeoGacha-style frame
        uint8_t d[5] = {3, static_cast<uint8_t>(g_round & 1 ? 1 : 2), 1, 0, static_cast<uint8_t>(g_round)};
        sendRaw(kBroadcast, d, sizeof(d));
        break;
      }
      case 1: {  // junk
        size_t n = 1 + (esp_random() % 64);
        for (size_t i = 0; i < n; ++i) buf[i] = static_cast<uint8_t>(esp_random());
        sendRaw(kBroadcast, buf, n);
        break;
      }
      case 2: {  // forged UFO frame, broadcast
        const DeviceEntry& d = deviceAt(1 + (g_round / 4) % (deviceCount() - 1));
        size_t n = forge(d.id, buf, g_round / 4);
        sendRaw(kBroadcast, buf, n);
        break;
      }
      default: {  // forged UFO frame, unencrypted unicast
        const DeviceEntry& d = deviceAt(1 + (g_round / 4) % (deviceCount() - 1));
        size_t n = forge(d.id, buf, g_round / 4 + 1);
        sendRaw(d.mac, buf, n);
        break;
      }
    }
  }
  if (now - last_log >= 2000) {
    last_log = now;
    Serial.printf("noise: sent=%lu fail=%lu\n", static_cast<unsigned long>(g_sent), static_cast<unsigned long>(g_fail));
  }
  delay(1);
}
