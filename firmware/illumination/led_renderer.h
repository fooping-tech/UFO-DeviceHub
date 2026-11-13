// FastLED renderer for the Illumination ATOM: draws the persistent mode, the
// temporary effect on top of it and cross-fades on mode changes. Non-blocking:
// render() draws at most one frame per call (~60 fps).
#pragma once

#if defined(ARDUINO)

#include <FastLED.h>

#include "lighting.h"

namespace ufo {

class LedRenderer {
 public:
  void begin(uint8_t max_brightness);
  void render(const LightingController& lc, uint32_t now);
  uint32_t frames() const { return frames_; }

 private:
  void drawMode(const LightModeState& m, CRGB* out, uint32_t now);
  void drawEffect(const EffectState& e, CRGB* out, uint32_t now);
  uint8_t max_brightness_ = 200;
  uint32_t last_frame_ = 0;
  uint32_t generation_ = 0;
  uint32_t fade_start_ = 0;
  uint16_t fade_ms_ = 0;
  uint32_t frames_ = 0;
};

}  // namespace ufo

#endif
