// M5Dial LED effect model (hardware independent, host-testable).
//
// Two LED outputs per dial (names from NeoGacha: frontLed on G1, topLed on G15):
//   Front     the 26 pixel LED on G1, always present
//   Transfer  the second LED on G15 (optional in the configuration)
//
// Every directional effect is computed on a "path": position 0 is the dial
// (real machine) end, the last position is the screen end.
//   * Linear: the pixels in order
//   * Folded: the NeoGacha arrangement - the first half of the pixels goes up
//             from the bottom, the second half comes down from the top, i.e.
//             two columns side by side (NEOPIXELLED::level2). Pixel i and
//             pixel count-1-i are at the same height, so both columns show
//             the same flow.
//   reversed swaps the two ends (the screen is at the bottom / at pixel 0).
// TO_SCREEN moves light from position 0 upwards, TO_DIAL the other way.
//
// docs/game-mode-interface.md is the contract for the effect ids and timing.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "messages.h"

namespace ufo {

constexpr uint16_t kChaseStepMs = 70;      // CHASE: one pixel per step
constexpr uint16_t kChaseSpacing = 4;      // CHASE: every 4th pixel lit
constexpr uint8_t kChaseDimLevel = 20;     // CHASE: level of the pixels between
constexpr uint16_t kCometPeriodMs = 700;   // COMET: one pass over the path
constexpr uint16_t kCometTail = 4;         // COMET: pixels behind the head
constexpr uint16_t kWipeDefaultMs = 1000;  // WIPE with duration_ms = 0
constexpr uint16_t kFlashPeriodMs = 200;   // FLASH: 5 Hz
constexpr uint16_t kBreathePeriodMs = 2000;
constexpr uint16_t kSparkleFrameMs = 60;

enum class LedLayout : uint8_t {
  Linear,  // one line: pixel 0 .. count-1
  Folded,  // up from the bottom, then down from the top (two columns)
};

struct LedGeometry {
  uint16_t count = 0;
  LedLayout layout = LedLayout::Linear;
  bool reversed = false;  // position 0 (dial end) is at the last pixel / at the top
};

// Number of positions on the path (folded: (count + 1) / 2).
uint16_t ledPathLength(const LedGeometry& g);
// Path position of pixel i (0 = dial end).
uint16_t ledPathPos(const LedGeometry& g, uint16_t i);

// Brightness (0..255) of the pixel at path position `pos` for an effect that
// started `elapsed_ms` ago. The caller scales the effect colour with it.
uint8_t dialEffectLevel(DialEffect effect, DialDirection direction, uint16_t pos, uint16_t len,
                        uint32_t elapsed_ms, uint16_t duration_ms);

// One PC-driven effect on one output.
struct DialLedSlot {
  DialEffect effect = DialEffect::None;
  DialDirection direction = DialDirection::None;
  uint8_t r = 0, g = 0, b = 0;  // 0,0,0 = unit colour
  uint16_t duration_ms = 0;     // 0 = until replaced / cleared
  uint8_t priority = 0;
  uint32_t started_at = 0;
  uint32_t command_id = 0;
  bool active(uint32_t now) const {
    return effect != DialEffect::None && (duration_ms == 0 || now - started_at < duration_ms);
  }
  void clear() { effect = DialEffect::None; }
};

// What one output shows right now.
enum class LedScene : uint8_t {
  Off,       // dark
  LinkLost,  // no hub link (red breathe)
  Error,     // configuration error (red blink)
  Settings,  // settings mode: the settings UI draws the front LED
  Idle,      // DISABLED (pacifica)
  Ready,     // READY (white breathe)
  Armed,     // ARMED (unit colour, waiting for a capsule)
  Complete,  // COMPLETE (fade out)
  Effect,    // `effect` below, from the PC or the device's own receive / release animation
};

struct LedPlan {
  LedScene scene = LedScene::Off;
  DialEffect effect = DialEffect::None;
  DialDirection direction = DialDirection::None;
  uint8_t r = 0, g = 0, b = 0;
  uint32_t elapsed_ms = 0;
  uint16_t duration_ms = 0;
  bool from_pc = false;  // false: the device's own (local) display
};

}  // namespace ufo
