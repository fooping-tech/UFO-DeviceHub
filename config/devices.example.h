// Static provisioning: Device ID <-> MAC address.
//   cp config/devices.example.h config/devices.h   (git-ignored) and edit.
// Each firmware prints its own MAC on boot ("MAC=..") - copy it from there.
// Unknown/unregistered MACs are never paired automatically.
#pragma once

// All devices must use the same 2.4GHz channel (1..13). No AP connection is made.
#define UFO_WIFI_CHANNEL 1

// Hub (M5ATOM, USB to the PC). PLACEHOLDER - replace with the real MAC.
#define UFO_MAC_HUB {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}
// Illumination ATOM. PLACEHOLDER - replace with the real MAC.
#define UFO_MAC_ILLUMINATION {0x02, 0x00, 0x00, 0x00, 0x00, 0x10}
// M5Dial #1 / #2: the MACs used by NeoGacha (setGachaId()). Verify on the real units.
#define UFO_MAC_M5DIAL_1 {0xC0, 0x4E, 0x30, 0x12, 0x6B, 0x90}
#define UFO_MAC_M5DIAL_2 {0xC0, 0x4E, 0x30, 0x12, 0x67, 0xD4}

// Optional timing overrides (defaults in shared/link_config.h):
// #define UFO_HEARTBEAT_INTERVAL_MS 1000
// #define UFO_LINK_TIMEOUT_MS 3000
// #define UFO_ACK_TIMEOUT_MS 100
// #define UFO_MAX_RETRIES 3
