// Loads the project configuration from config/ (added to the include path by
// platformio.ini: -I config). Real values live in the git-ignored files
// devices.h / hardware.h / secrets.h; when one is missing the *.example.h file
// is used so that CI builds work, and the firmware reports it loudly.
#pragma once

#if __has_include("devices.h")
#include "devices.h"
#define UFO_USING_EXAMPLE_DEVICES 0
#else
#include "devices.example.h"
#define UFO_USING_EXAMPLE_DEVICES 1
#endif

#if __has_include("hardware.h")
#include "hardware.h"
#define UFO_USING_EXAMPLE_HARDWARE 0
#else
#include "hardware.example.h"
#define UFO_USING_EXAMPLE_HARDWARE 1
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#define UFO_USING_EXAMPLE_KEYS 0
#else
#if defined(UFO_REQUIRE_SECRETS)
#error "config/secrets.h is missing. Run: python3 tests/tools/gen_secrets.py"
#endif
#include "secrets.example.h"
#define UFO_USING_EXAMPLE_KEYS 1
#endif

#include "link_config.h"
