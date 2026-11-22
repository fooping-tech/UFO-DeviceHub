// Static provisioning: Device ID <-> MAC address, per UFO system.
//   cp config/devices.example.h config/devices.h   (git-ignored) and edit.
// Each firmware prints its own MAC on boot ("MAC=..") - copy it from there.
// Unknown/unregistered MACs are never paired automatically.
//
// Two UFO systems run in the same venue (docs/multi-system.md). Each system
// has its own block: hub + illumination + 2 dials, its own 2.4GHz channel and
// its own keys (config/secrets.h). Which block a firmware uses is chosen by the
// PlatformIO env (hub_s1 / hub_s2, illumination_s1 / ..., -DUFO_SYSTEM_ID=n).
// Keep BOTH blocks in this one file: every firmware checks at boot that the two
// systems share no MAC and use different, non-overlapping channels.
#pragma once

// ---------------- System 1 ----------------
#ifndef UFO_S1_MAC_HUB
// 2.4GHz channel (1..13), no AP connection. Systems should be >= 5 channels apart.
#define UFO_S1_WIFI_CHANNEL 1
// Hub (M5AtomS3R, USB to the PC). PLACEHOLDER - replace with the real MAC.
#define UFO_S1_MAC_HUB {0x02, 0x00, 0x00, 0x00, 0x01, 0x01}
// Illumination ATOM. PLACEHOLDER - replace with the real MAC.
#define UFO_S1_MAC_ILLUMINATION {0x02, 0x00, 0x00, 0x00, 0x01, 0x10}
// M5Dial #1 / #2: the MACs used by NeoGacha (setGachaId()). Verify on the real units.
#define UFO_S1_MAC_M5DIAL_1 {0xC0, 0x4E, 0x30, 0x12, 0x6B, 0x90}
#define UFO_S1_MAC_M5DIAL_2 {0xC0, 0x4E, 0x30, 0x12, 0x67, 0xD4}
#endif

// ---------------- System 2 ----------------
#ifndef UFO_S2_MAC_HUB
#define UFO_S2_WIFI_CHANNEL 11
// PLACEHOLDERS - replace with the MACs of the second set of devices.
#define UFO_S2_MAC_HUB {0x02, 0x00, 0x00, 0x00, 0x02, 0x01}
#define UFO_S2_MAC_ILLUMINATION {0x02, 0x00, 0x00, 0x00, 0x02, 0x10}
#define UFO_S2_MAC_M5DIAL_1 {0x02, 0x00, 0x00, 0x00, 0x02, 0x20}
#define UFO_S2_MAC_M5DIAL_2 {0x02, 0x00, 0x00, 0x00, 0x02, 0x21}
#endif

// ---------------- Staff remote (M5StopWatch, ID 0x30) ----------------
// Added after the first release: a devices.h written before it has no entry, so a
// placeholder is used (the remote then stays offline). Put the real MAC (printed on
// boot, also the USB serial number of the StopWatch) into config/devices.h.
#ifndef UFO_S1_MAC_STAFF_REMOTE
#define UFO_S1_MAC_STAFF_REMOTE {0x02, 0x00, 0x00, 0x00, 0x01, 0x30}  // PLACEHOLDER
#endif
#ifndef UFO_S2_MAC_STAFF_REMOTE
#define UFO_S2_MAC_STAFF_REMOTE {0x02, 0x00, 0x00, 0x00, 0x02, 0x30}  // PLACEHOLDER
#endif

// Optional timing overrides (defaults in shared/link_config.h):
// #define UFO_HEARTBEAT_INTERVAL_MS 1000
// #define UFO_LINK_TIMEOUT_MS 3000
// #define UFO_ACK_TIMEOUT_MS 100
// #define UFO_MAX_RETRIES 3
