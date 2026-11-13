// Arduino glue shared by every device firmware (Illumination, M5Dial):
// ESP-NOW transport + hub peer registration + NodeCore + boot-time checks.
#pragma once

#if defined(ARDUINO)

#include "device_ids.h"
#include "espnow_transport.h"
#include "node_core.h"

namespace ufo {

class NodeRuntime : public NodeTransport {
 public:
  NodeRuntime(uint8_t self_id, NodeApp& app);
  // Returns false if the radio could not be initialized.
  bool begin();
  // Drains received frames into NodeCore and runs its timers. Call every loop().
  void loop();
  NodeCore& core() { return core_; }
  bool sendToHub(const uint8_t* data, size_t len) override;

  bool macMismatch() const { return mac_mismatch_; }
  bool radioOk() const { return radio_ok_; }
  const uint8_t* selfMac() const { return self_mac_; }
  const char* selfMacStr() const { return self_mac_str_; }
  const EspNowTransport& transport() const { return radio_; }
  const DeviceEntry* self() const { return self_; }

 private:
  static NodeConfig makeConfig(uint8_t self_id);
  uint8_t self_id_;
  const DeviceEntry* self_ = nullptr;
  const DeviceEntry* hub_ = nullptr;
  EspNowTransport radio_;
  NodeCore core_;
  uint8_t self_mac_[6] = {0};
  char self_mac_str_[18] = {0};
  bool mac_mismatch_ = false;
  bool radio_ok_ = false;
};

}  // namespace ufo

#endif
