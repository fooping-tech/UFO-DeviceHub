// LED renderer for the Illumination ATOM: draws the persistent mode, the
// temporary effect on top of it and cross-fades on mode changes, on two outputs:
//   * the addressable strip (WS2811, FastLED), drawn from where each pixel is on
//     the shelf (shelf_layout.h), not from its place on the data line
//   * the COB tape (single channel, PWM through the Atomic PWM Base)
// Non-blocking: render() draws at most one frame per call (~60 fps).
#pragma once

#if defined(ARDUINO)

#include <FastLED.h>

#include "lighting.h"

namespace ufo {

class LedRenderer {
 public:
  // cob_pin < 0: no COB tape.
  void begin(uint8_t max_brightness, int cob_pin, uint8_t cob_max);
  // Boot check (blocking, ~1.5 s, setup() only): red -> green -> blue means
  // ILLUM_LED_COLOR_ORDER is right.
  void colorOrderTest();
  // Boot check of the shelf layout (blocking, ~2 s, setup() only): the pillars
  // light one pixel at a time from the top down (front-left red, front-right
  // green, back-right blue, back-left yellow), then the front face from left to
  // right in white. Anything else: fix shelf_layout.h (kRuns).
  void layoutTest();
  // mist_on: the mist relay is ON right now (MIST_GLOW / MAGIC_PUFF follow it).
  void render(const LightingController& lc, bool mist_on, uint32_t now);
  uint32_t frames() const { return frames_; }

 private:
  void drawMode(const LightModeState& m, CRGB* out, uint32_t now);
  void drawEffect(const EffectState& e, CRGB* out, uint32_t now);
  uint8_t cobMode(const LightModeState& m, uint32_t now);
  uint8_t cobEffect(const EffectState& e, uint8_t base, uint32_t now);
  void writeCob(uint8_t v);
  void updateLightning(uint32_t now);

  uint8_t max_brightness_ = 200;
  int cob_pin_ = -1;
  uint8_t cob_max_ = 255;
  uint32_t last_frame_ = 0;
  uint32_t generation_ = 0;
  uint32_t fade_start_ = 0;
  uint16_t fade_ms_ = 0;
  uint8_t cob_prev_ = 0;  // COB level at the start of a cross-fade
  uint8_t cob_now_ = 0;
  uint8_t mist_glow_ = 0;  // 0..255, rises ~1 s while the mist is ON, falls ~2 s
  uint8_t bolt_ = 0;       // lightning strength (STORM), shared by COB and strip
  uint32_t frames_ = 0;
};

}  // namespace ufo

#endif
