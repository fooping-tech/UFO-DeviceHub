// Non-blocking LED animations shared by the Illumination ATOM and the M5Dial
// front LED. Ported from NeoGacha NEOPIXELLED.cpp (pacifica, Fire2012, cylon,
// level) and reworked so that every call renders ONE frame for any strip length
// without delay()/FastLED.delay(). The caller calls FastLED.show().
#pragma once

#if defined(ARDUINO)

#include <FastLED.h>
#include <stdint.h>

namespace ufo {
namespace fx {

struct PacificaState {
  uint16_t c1 = 0, c2 = 0, c3 = 0, c4 = 0;
  uint32_t last_ms = 0;
};

struct FireState {
  static constexpr uint16_t kMax = 256;
  uint8_t heat[kMax] = {0};
};

void pacifica(CRGB* leds, uint16_t n, PacificaState& s, uint32_t ms);
void fire(CRGB* leds, uint16_t n, FireState& s, uint8_t cooling = 55, uint8_t sparking = 120);
void cylon(CRGB* leds, uint16_t n, uint32_t ms, uint8_t speed);          // NeoGacha clyon(), non-blocking
void rainbow(CRGB* leds, uint16_t n, uint32_t ms, uint8_t speed);
void breathe(CRGB* leds, uint16_t n, uint32_t ms, const CRGB& c, uint16_t period_ms);
void chase(CRGB* leds, uint16_t n, uint32_t ms, const CRGB& c, uint8_t speed);
void comet(CRGB* leds, uint16_t n, uint32_t ms, const CRGB& c, uint16_t period_ms, bool reverse);
void sparkle(CRGB* leds, uint16_t n, const CRGB& c, uint8_t density);
void blink(CRGB* leds, uint16_t n, uint32_t ms, const CRGB& c, uint16_t period_ms);
void wipe(CRGB* leds, uint16_t n, const CRGB& c, uint8_t progress);   // progress 0..255
void level(CRGB* leds, uint16_t n, uint16_t count);                    // NeoGacha level()
void fadeAll(CRGB* leds, uint16_t n, uint8_t scale);

}  // namespace fx
}  // namespace ufo

#endif
