#if defined(ARDUINO)

#include "node_runtime.h"

#include <Arduino.h>
#include <esp_random.h>
#include <stdio.h>

#include "ufo_config.h"
#include "version.h"

namespace ufo {

NodeConfig NodeRuntime::makeConfig(uint8_t self_id) {
  NodeConfig c;
  c.self_id = self_id;
  const DeviceEntry* self = findDevice(self_id);
  c.device_type = self ? self->type : DeviceType::Dial;
  c.hub_id = kHubId;
  const DeviceEntry* hub = findDevice(kHubId);
  if (hub) memcpy(c.hub_mac, hub->mac, 6);
  c.link_timeout_ms = UFO_LINK_TIMEOUT_MS;
  c.event_policy.ack_timeout_ms = UFO_ACK_TIMEOUT_MS;
  c.event_policy.max_retries = UFO_MAX_RETRIES;
  c.event_policy.retry_forever = true;  // events: same id until the hub ACKs
  c.event_policy.slow_retry_ms = UFO_EVENT_SLOW_RETRY_MS;
  c.fw_major = UFO_FW_VERSION_MAJOR;
  c.fw_minor = UFO_FW_VERSION_MINOR;
  c.example_keys = usingExampleKeys();
  return c;
}

NodeRuntime::NodeRuntime(uint8_t self_id, NodeApp& app)
    : self_id_(self_id), core_(makeConfig(self_id), *this, app) {}

bool NodeRuntime::begin() {
  self_ = findDevice(self_id_);
  hub_ = findDevice(kHubId);
  radio_ok_ = radio_.begin(wifiChannel(), primaryMasterKey());
  radio_.selfMac(self_mac_);
  snprintf(self_mac_str_, sizeof(self_mac_str_), "%02X:%02X:%02X:%02X:%02X:%02X", self_mac_[0], self_mac_[1],
           self_mac_[2], self_mac_[3], self_mac_[4], self_mac_[5]);
  mac_mismatch_ = !self_ || !macEqual(self_->mac, self_mac_);
  Serial.printf("UFO device id=0x%02X name=%s MAC=%s channel=%u fw=%s\n", self_id_, self_ ? self_->name : "?",
                self_mac_str_, wifiChannel(), UFO_FW_VERSION_STR);
  if (mac_mismatch_) Serial.println("WARNING: own MAC differs from config/devices.h - the hub will ignore us");
  if (usingExampleKeys()) Serial.println("WARNING: EXAMPLE ESP-NOW KEYS IN USE (tests/tools/gen_secrets.py)");
  // Only the hub is a peer: frames from any other MAC are dropped in the radio callback.
  if (radio_ok_ && self_ && hub_) radio_ok_ = radio_.addPeer(hub_->mac, self_->lmk);
  if (!radio_ok_) Serial.println("ERROR: ESP-NOW init / hub peer registration failed");
  core_.begin(millis(), esp_random(), esp_random());
  return radio_ok_;
}

bool NodeRuntime::sendToHub(const uint8_t* data, size_t len) {
  if (!hub_) return false;
  return radio_.send(hub_->mac, data, len);
}

void NodeRuntime::loop() {
  uint32_t now = millis();
  RxFrame f;
  int budget = 8;
  while (budget-- > 0 && radio_.poll(f)) core_.onFrame(f.mac, f.data, f.len, f.rssi, now);
  core_.tick(now);
}

}  // namespace ufo

#endif
