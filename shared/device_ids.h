// Logical device IDs and the static provisioning table (ID <-> MAC <-> link key).
//
// MAC addresses and keys come from config/devices.h and config/secrets.h
// (copied from the *.example.h files). Unknown devices are never paired
// automatically: only the entries of this table are registered as ESP-NOW peers.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "messages.h"

namespace ufo {

enum class DeviceId : uint8_t {
  Hub = 0x01,
  Illumination = 0x10,
  M5Dial1 = 0x20,
  M5Dial2 = 0x21,
};

constexpr uint8_t kBroadcastId = 0xFF;  // never accepted as a destination
constexpr uint8_t kHubId = static_cast<uint8_t>(DeviceId::Hub);

struct DeviceEntry {
  uint8_t id;
  DeviceType type;
  const char* name;
  uint8_t mac[6];
  uint8_t lmk[16];  // link key between the hub and this device (unused for the hub entry)
};

size_t deviceCount();
const DeviceEntry& deviceAt(size_t index);
const DeviceEntry* findDevice(uint8_t id);
const DeviceEntry* findDeviceByMac(const uint8_t* mac);
const uint8_t* primaryMasterKey();  // 16 bytes

bool usingExampleKeys();     // config/secrets.h missing -> example keys (NOT for production)
bool usingExampleDevices();  // config/devices.h missing -> example MACs
uint8_t wifiChannel();
uint8_t systemId();  // UFO_SYSTEM_ID of this firmware (1..)

// Cross-system checks of the provisioning (both systems are in config/):
//   SharedMac / SharedKeys / BadChannel are errors, SameChannel / Overlapping
//   are warnings (still isolated by magic + keys + MAC table, but they share airtime).
constexpr uint8_t kSepSharedMac = 0x01;
constexpr uint8_t kSepSharedKeys = 0x02;
constexpr uint8_t kSepBadChannel = 0x04;
constexpr uint8_t kSepSameChannel = 0x08;
constexpr uint8_t kSepOverlappingChannel = 0x10;
constexpr uint8_t kSepErrors = kSepSharedMac | kSepSharedKeys | kSepBadChannel;
uint8_t systemSeparationIssues();
const char* systemSeparationText(uint8_t issues);  // most important issue

}  // namespace ufo
