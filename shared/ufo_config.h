// Loads the project configuration from config/ (added to the include path by
// platformio.ini: -I config). Real values live in the git-ignored files
// devices.h / hardware.h / secrets.h; whatever is missing is taken from the
// *.example.h files so that CI builds work, and the firmware reports it loudly.
//
// UFO_SYSTEM_ID (1 or 2, set by the PlatformIO env) selects which system's
// MAC table, channel and keys this firmware uses. See docs/multi-system.md.
#pragma once

#ifndef UFO_SYSTEM_ID
#define UFO_SYSTEM_ID 1
#endif
#if UFO_SYSTEM_ID < 1 || UFO_SYSTEM_ID > 2
#error "UFO_SYSTEM_ID must be 1 or 2"
#endif
#define UFO_SYSTEM_COUNT 2

// ---- devices (MAC tables + channel) ----
#if __has_include("devices.h")
#include "devices.h"
#endif
// Old single-system names are system 1.
#if !defined(UFO_S1_MAC_HUB) && defined(UFO_MAC_HUB)
#define UFO_S1_WIFI_CHANNEL UFO_WIFI_CHANNEL
#define UFO_S1_MAC_HUB UFO_MAC_HUB
#define UFO_S1_MAC_ILLUMINATION UFO_MAC_ILLUMINATION
#define UFO_S1_MAC_M5DIAL_1 UFO_MAC_M5DIAL_1
#define UFO_S1_MAC_M5DIAL_2 UFO_MAC_M5DIAL_2
#endif
#if defined(UFO_S1_MAC_HUB)
#define UFO_S1_EXAMPLE_DEVICES 0
#else
#define UFO_S1_EXAMPLE_DEVICES 1
#endif
#if defined(UFO_S2_MAC_HUB)
#define UFO_S2_EXAMPLE_DEVICES 0
#else
#define UFO_S2_EXAMPLE_DEVICES 1
#endif
#include "devices.example.h"  // fills only the missing system blocks

// ---- keys ----
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#if !defined(UFO_S1_PMK) && defined(UFO_PMK)
#define UFO_S1_PMK UFO_PMK
#define UFO_S1_LMK_ILLUMINATION UFO_LMK_ILLUMINATION
#define UFO_S1_LMK_M5DIAL_1 UFO_LMK_M5DIAL_1
#define UFO_S1_LMK_M5DIAL_2 UFO_LMK_M5DIAL_2
#endif
#if defined(UFO_S1_PMK)
#define UFO_S1_EXAMPLE_KEYS 0
#else
#define UFO_S1_EXAMPLE_KEYS 1
#endif
#if defined(UFO_S2_PMK)
#define UFO_S2_EXAMPLE_KEYS 0
#else
#define UFO_S2_EXAMPLE_KEYS 1
#endif
#include "secrets.example.h"  // fills only the missing system key sets

#if UFO_SYSTEM_ID == 1
#define UFO_USING_EXAMPLE_DEVICES UFO_S1_EXAMPLE_DEVICES
#define UFO_USING_EXAMPLE_KEYS UFO_S1_EXAMPLE_KEYS
#else
#define UFO_USING_EXAMPLE_DEVICES UFO_S2_EXAMPLE_DEVICES
#define UFO_USING_EXAMPLE_KEYS UFO_S2_EXAMPLE_KEYS
#endif

// A firmware built from the example MACs/keys cannot talk to the real hub
// (it was flashed from a PC without config/devices.h + secrets.h: the device
// stays OFFLINE). So that is a build error unless explicitly allowed, e.g. for a
// compile-only check on a fresh clone:
//   PLATFORMIO_BUILD_FLAGS=-DUFO_ALLOW_EXAMPLE_CONFIG pio run
// (UFO_REQUIRE_SECRETS, the old opt-in switch, is now the default.)
#if !defined(UFO_ALLOW_EXAMPLE_CONFIG)
#if UFO_USING_EXAMPLE_DEVICES
#error "config/devices.h has no MAC table for this UFO_SYSTEM_ID. Copy config/devices.h from the PC that set up this system (see README: building on another PC)."
#endif
#if UFO_USING_EXAMPLE_KEYS
#error "config/secrets.h has no ESP-NOW keys for this UFO_SYSTEM_ID. Copy config/secrets.h from the PC that set up this system (see README: building on another PC). New system: python3 tests/tools/gen_secrets.py"
#endif
#endif

// ---- hardware ----
#if __has_include("hardware.h")
#include "hardware.h"
#define UFO_USING_EXAMPLE_HARDWARE 0
#else
#include "hardware.example.h"
#define UFO_USING_EXAMPLE_HARDWARE 1
#endif

#include "link_config.h"
