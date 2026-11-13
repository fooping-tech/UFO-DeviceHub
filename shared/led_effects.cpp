#if defined(ARDUINO)

#include "led_effects.h"

namespace ufo {
namespace fx {

namespace {
// Pacifica palettes (FastLED example by Mark Kriegsman, MIT; used by NeoGacha)
CRGBPalette16 kPacifica1 = {0x000507, 0x000409, 0x00030B, 0x00030D, 0x000210, 0x000212, 0x000114, 0x000117,
                            0x000019, 0x00001C, 0x000026, 0x000031, 0x00003B, 0x000046, 0x14554B, 0x28AA50};
CRGBPalette16 kPacifica2 = {0x000507, 0x000409, 0x00030B, 0x00030D, 0x000210, 0x000212, 0x000114, 0x000117,
                            0x000019, 0x00001C, 0x000026, 0x000031, 0x00003B, 0x000046, 0x0C5F52, 0x19BE5F};
CRGBPalette16 kPacifica3 = {0x000208, 0x00030E, 0x000514, 0x00061A, 0x000820, 0x000927, 0x000B2D, 0x000C33,
                            0x000E39, 0x001040, 0x001450, 0x001860, 0x001C70, 0x002080, 0x1040BF, 0x2060FF};

void pacificaLayer(CRGB* leds, uint16_t n, CRGBPalette16& p, uint16_t cistart, uint16_t wavescale, uint8_t bri,
                   uint16_t ioff) {
  uint16_t ci = cistart;
  uint16_t waveangle = ioff;
  uint16_t wavescale_half = (wavescale / 2) + 20;
  for (uint16_t i = 0; i < n; i++) {
    waveangle += 250;
    uint16_t s16 = sin16(waveangle) + 32768;
    uint16_t cs = scale16(s16, wavescale_half) + wavescale_half;
    ci += cs;
    uint16_t sindex16 = sin16(ci) + 32768;
    uint8_t sindex8 = scale16(sindex16, 240);
    leds[i] += ColorFromPalette(p, sindex8, bri, LINEARBLEND);
  }
}
}  // namespace

void pacifica(CRGB* leds, uint16_t n, PacificaState& s, uint32_t ms) {
  uint32_t deltams = ms - s.last_ms;
  s.last_ms = ms;
  uint16_t sf1 = beatsin16(3, 179, 269);
  uint16_t sf2 = beatsin16(4, 179, 269);
  uint32_t d1 = (deltams * sf1) / 256;
  uint32_t d2 = (deltams * sf2) / 256;
  uint32_t d21 = (d1 + d2) / 2;
  s.c1 += (d1 * beatsin88(1011, 10, 13));
  s.c2 -= (d21 * beatsin88(777, 8, 11));
  s.c3 -= (d1 * beatsin88(501, 5, 7));
  s.c4 -= (d2 * beatsin88(257, 4, 6));
  fill_solid(leds, n, CRGB(2, 6, 10));
  pacificaLayer(leds, n, kPacifica1, s.c1, beatsin16(3, 11 * 256, 14 * 256), beatsin8(10, 70, 130), 0 - beat16(301));
  pacificaLayer(leds, n, kPacifica2, s.c2, beatsin16(4, 6 * 256, 9 * 256), beatsin8(17, 40, 80), beat16(401));
  pacificaLayer(leds, n, kPacifica3, s.c3, 6 * 256, beatsin8(9, 10, 38), 0 - beat16(503));
  pacificaLayer(leds, n, kPacifica3, s.c4, 5 * 256, beatsin8(8, 10, 28), beat16(601));
  uint8_t basethreshold = beatsin8(9, 55, 65);
  uint8_t wave = beat8(7);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t threshold = scale8(sin8(wave), 20) + basethreshold;
    wave += 7;
    uint8_t l = leds[i].getAverageLight();
    if (l > threshold) {
      uint8_t overage = l - threshold;
      uint8_t overage2 = qadd8(overage, overage);
      leds[i] += CRGB(overage, overage2, qadd8(overage2, overage2));
    }
  }
  for (uint16_t i = 0; i < n; i++) {
    leds[i].blue = scale8(leds[i].blue, 145);
    leds[i].green = scale8(leds[i].green, 200);
    leds[i] |= CRGB(2, 5, 7);
  }
}

void fire(CRGB* leds, uint16_t n, FireState& s, uint8_t cooling, uint8_t sparking) {
  if (n > FireState::kMax) n = FireState::kMax;
  if (n < 3) {
    fill_solid(leds, n, CRGB::OrangeRed);
    return;
  }
  for (uint16_t i = 0; i < n; i++) s.heat[i] = qsub8(s.heat[i], random8(0, ((cooling * 10) / n) + 2));
  for (uint16_t k = n - 1; k >= 2; k--) s.heat[k] = (s.heat[k - 1] + s.heat[k - 2] + s.heat[k - 2]) / 3;
  if (random8() < sparking) {
    uint8_t y = random8(n < 7 ? n : 7);
    s.heat[y] = qadd8(s.heat[y], random8(160, 255));
  }
  for (uint16_t j = 0; j < n; j++) leds[j] = HeatColor(s.heat[j]);
}

void fadeAll(CRGB* leds, uint16_t n, uint8_t scale) {
  for (uint16_t i = 0; i < n; i++) leds[i].nscale8(scale);
}

void cylon(CRGB* leds, uint16_t n, uint32_t ms, uint8_t speed) {
  if (n == 0) return;
  fadeAll(leds, n, 235);
  uint32_t step_ms = 2 + (255 - speed) / 8;
  uint32_t pos = (ms / step_ms) % (2 * n);
  uint16_t i = pos < n ? pos : 2 * n - 1 - pos;
  leds[i] = CHSV(static_cast<uint8_t>(ms / 8), 255, 255);
}

void rainbow(CRGB* leds, uint16_t n, uint32_t ms, uint8_t speed) {
  uint8_t hue = static_cast<uint8_t>((ms * (speed / 16 + 1)) / 32);
  fill_rainbow(leds, n, hue, n > 0 ? 255 / n + 1 : 1);
}

void breathe(CRGB* leds, uint16_t n, uint32_t ms, const CRGB& c, uint16_t period_ms) {
  if (period_ms == 0) period_ms = 1;
  uint8_t phase = static_cast<uint8_t>((ms % period_ms) * 256 / period_ms);
  uint8_t v = scale8(sin8(phase), 200) + 55;
  CRGB col = c;
  col.nscale8_video(v);
  fill_solid(leds, n, col);
}

void chase(CRGB* leds, uint16_t n, uint32_t ms, const CRGB& c, uint8_t speed) {
  uint32_t step_ms = 20 + (255 - speed);
  uint32_t off = ms / step_ms;
  for (uint16_t i = 0; i < n; i++) {
    leds[i] = ((i + off) % 4 == 0) ? c : CRGB(c).nscale8_video(20);
  }
}

void comet(CRGB* leds, uint16_t n, uint32_t ms, const CRGB& c, uint16_t period_ms, bool reverse) {
  if (n == 0) return;
  fadeAll(leds, n, 200);
  if (period_ms == 0) period_ms = 1;
  uint16_t i = static_cast<uint16_t>((ms % period_ms) * n / period_ms);
  if (reverse) i = n - 1 - i;
  leds[i] = c;
}

void sparkle(CRGB* leds, uint16_t n, const CRGB& c, uint8_t density) {
  fadeAll(leds, n, 220);
  if (n && random8() < density) leds[random16(n)] = c;
}

void blink(CRGB* leds, uint16_t n, uint32_t ms, const CRGB& c, uint16_t period_ms) {
  if (period_ms == 0) period_ms = 1;
  bool on = (ms % period_ms) < period_ms / 2;
  fill_solid(leds, n, on ? c : CRGB::Black);
}

void wipe(CRGB* leds, uint16_t n, const CRGB& c, uint8_t progress) {
  uint16_t lit = static_cast<uint16_t>((static_cast<uint32_t>(n) * progress) / 255);
  for (uint16_t i = 0; i < n; i++) leds[i] = i < lit ? c : CRGB::Black;
}

void level(CRGB* leds, uint16_t n, uint16_t count) {
  for (uint16_t i = 0; i < n; i++) leds[i] = i < count ? CRGB(CHSV(i * 10, 255, 255)) : CRGB::Black;
}

}  // namespace fx
}  // namespace ufo

#endif
