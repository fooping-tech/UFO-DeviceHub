#include <string.h>

#include "device_ids.h"
#include "ufo_config.h"

namespace ufo {

namespace {
const DeviceEntry kDevices[] = {
    {static_cast<uint8_t>(DeviceId::Hub), DeviceType::Hub, "hub", UFO_MAC_HUB, {0}},
    {static_cast<uint8_t>(DeviceId::Illumination), DeviceType::Illumination, "illumination",
     UFO_MAC_ILLUMINATION, UFO_LMK_ILLUMINATION},
    {static_cast<uint8_t>(DeviceId::M5Dial1), DeviceType::Dial, "m5dial_1", UFO_MAC_M5DIAL_1,
     UFO_LMK_M5DIAL_1},
    {static_cast<uint8_t>(DeviceId::M5Dial2), DeviceType::Dial, "m5dial_2", UFO_MAC_M5DIAL_2,
     UFO_LMK_M5DIAL_2},
};
const uint8_t kPmk[16] = UFO_PMK;
}  // namespace

size_t deviceCount() { return sizeof(kDevices) / sizeof(kDevices[0]); }
const DeviceEntry& deviceAt(size_t index) { return kDevices[index]; }

const DeviceEntry* findDevice(uint8_t id) {
  for (const auto& d : kDevices)
    if (d.id == id) return &d;
  return nullptr;
}

const DeviceEntry* findDeviceByMac(const uint8_t* mac) {
  for (const auto& d : kDevices)
    if (macEqual(d.mac, mac)) return &d;
  return nullptr;
}

const uint8_t* primaryMasterKey() { return kPmk; }
bool usingExampleKeys() { return UFO_USING_EXAMPLE_KEYS != 0; }
bool usingExampleDevices() { return UFO_USING_EXAMPLE_DEVICES != 0; }
uint8_t wifiChannel() { return UFO_WIFI_CHANNEL; }

}  // namespace ufo
