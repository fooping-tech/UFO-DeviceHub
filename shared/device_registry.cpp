#include <stdlib.h>
#include <string.h>

#include "device_ids.h"
#include "ufo_config.h"

namespace ufo {

namespace {

struct SystemTable {
  uint8_t channel;
  uint8_t pmk[16];
  DeviceEntry devices[4];
};

#define UFO_SYSTEM_TABLE(N)                                                                              \
  {                                                                                                      \
    UFO_S##N##_WIFI_CHANNEL, UFO_S##N##_PMK, {                                                           \
      {static_cast<uint8_t>(DeviceId::Hub), DeviceType::Hub, "hub", UFO_S##N##_MAC_HUB, {0}},           \
          {static_cast<uint8_t>(DeviceId::Illumination), DeviceType::Illumination, "illumination",       \
           UFO_S##N##_MAC_ILLUMINATION, UFO_S##N##_LMK_ILLUMINATION},                                    \
          {static_cast<uint8_t>(DeviceId::M5Dial1), DeviceType::Dial, "m5dial_1", UFO_S##N##_MAC_M5DIAL_1, \
           UFO_S##N##_LMK_M5DIAL_1},                                                                     \
          {static_cast<uint8_t>(DeviceId::M5Dial2), DeviceType::Dial, "m5dial_2", UFO_S##N##_MAC_M5DIAL_2, \
           UFO_S##N##_LMK_M5DIAL_2},                                                                     \
    }                                                                                                    \
  }

const SystemTable kSystems[UFO_SYSTEM_COUNT] = {UFO_SYSTEM_TABLE(1), UFO_SYSTEM_TABLE(2)};
const SystemTable& self() { return kSystems[UFO_SYSTEM_ID - 1]; }

}  // namespace

size_t deviceCount() { return sizeof(self().devices) / sizeof(self().devices[0]); }
const DeviceEntry& deviceAt(size_t index) { return self().devices[index]; }

const DeviceEntry* findDevice(uint8_t id) {
  for (const auto& d : self().devices)
    if (d.id == id) return &d;
  return nullptr;
}

const DeviceEntry* findDeviceByMac(const uint8_t* mac) {
  for (const auto& d : self().devices)
    if (macEqual(d.mac, mac)) return &d;
  return nullptr;
}

const uint8_t* primaryMasterKey() { return self().pmk; }
bool usingExampleKeys() { return UFO_USING_EXAMPLE_KEYS != 0; }
bool usingExampleDevices() { return UFO_USING_EXAMPLE_DEVICES != 0; }
uint8_t wifiChannel() { return self().channel; }
uint8_t systemId() { return UFO_SYSTEM_ID; }

uint8_t systemSeparationIssues() {
  uint8_t f = 0;
  for (size_t a = 0; a < UFO_SYSTEM_COUNT; ++a) {
    const SystemTable& sa = kSystems[a];
    if (sa.channel < 1 || sa.channel > 13) f |= kSepBadChannel;
    for (size_t b = a + 1; b < UFO_SYSTEM_COUNT; ++b) {
      const SystemTable& sb = kSystems[b];
      int d = abs(static_cast<int>(sa.channel) - static_cast<int>(sb.channel));
      if (d == 0)
        f |= kSepSameChannel;
      else if (d < 5)
        f |= kSepOverlappingChannel;
      if (memcmp(sa.pmk, sb.pmk, 16) == 0) f |= kSepSharedKeys;
      for (const auto& x : sa.devices)
        for (const auto& y : sb.devices) {
          if (macEqual(x.mac, y.mac)) f |= kSepSharedMac;
          if (x.id != static_cast<uint8_t>(DeviceId::Hub) && y.id != static_cast<uint8_t>(DeviceId::Hub) &&
              memcmp(x.lmk, y.lmk, 16) == 0)
            f |= kSepSharedKeys;
        }
    }
  }
  return f;
}

const char* systemSeparationText(uint8_t issues) {
  if (issues & kSepSharedMac) return "a MAC is in BOTH systems (config/devices.h)";
  if (issues & kSepSharedKeys) return "systems share ESP-NOW keys (run gen_secrets.py)";
  if (issues & kSepBadChannel) return "channel out of 1..13";
  if (issues & kSepSameChannel) return "both systems on the same channel";
  if (issues & kSepOverlappingChannel) return "system channels overlap (< 5 apart)";
  return "ok";
}

}  // namespace ufo
