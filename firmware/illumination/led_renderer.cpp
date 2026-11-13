#if defined(ARDUINO)

#include "led_renderer.h"

#include "led_effects.h"
#include "ufo_config.h"

namespace ufo {

namespace {
constexpr uint16_t kN = ILLUM_LED_COUNT;
CRGB g_leds[kN];   // what goes to the strip
CRGB g_prev[kN];   // snapshot at the start of a cross-fade
CRGB g_mode[kN];   // persistent mode layer
CRGB g_fx[kN];     // effect layer
fx::PacificaState g_pacifica;
CLEDController* g_strip = nullptr;
}  // namespace

void LedRenderer::begin(uint8_t max_brightness) {
  max_brightness_ = max_brightness;
  // Each strip is shown through its own controller (the status LED has another one).
  g_strip = &FastLED.addLeds<WS2812B, ILLUM_LED_PIN, GRB>(g_leds, kN);
  fill_solid(g_leds, kN, CRGB::Black);
  g_strip->showLeds(0);
}

void LedRenderer::drawMode(const LightModeState& m, CRGB* out, uint32_t now) {
  const CRGB custom(m.r, m.g, m.b);
  const uint16_t period = 600 + (255 - m.speed) * 16;
  switch (m.mode) {
    case LightMode::Off: fill_solid(out, kN, CRGB::Black); break;
    case LightMode::Idle: fx::pacifica(out, kN, g_pacifica, now); break;
    case LightMode::Study: fx::breathe(out, kN, now, CRGB(255, 170, 110), period * 2); break;
    case LightMode::GameReady: fx::chase(out, kN, now, CRGB(0, 200, 255), m.speed); break;
    case LightMode::GamePlay: fx::rainbow(out, kN, now, m.speed); break;
    case LightMode::Result:
      fx::sparkle(out, kN, CRGB(255, 190, 40), 120);
      for (uint16_t i = 0; i < kN; i++) out[i] |= CRGB(40, 25, 0);
      break;
    case LightMode::Error: fx::blink(out, kN, now, CRGB::Red, 1000); break;
    case LightMode::Custom:
      if (m.speed == 0)
        fill_solid(out, kN, custom);
      else
        fx::breathe(out, kN, now, custom, period);
      break;
  }
}

void LedRenderer::drawEffect(const EffectState& e, CRGB* out, uint32_t now) {
  uint32_t el = now - e.started_at;
  uint8_t progress = e.duration_ms ? static_cast<uint8_t>(min<uint32_t>(255, el * 255 / e.duration_ms)) : 255;
  switch (e.id) {
    case EffectId::GameStart:
      if (progress < 170)
        fx::wipe(out, kN, CRGB::White, progress * 3 / 2);
      else
        fx::blink(out, kN, now, CRGB::White, 200);
      break;
    case EffectId::ArmMove: fx::comet(out, kN, now, CRGB(0, 80, 255), 600, false); break;
    case EffectId::ArmDescend: fx::comet(out, kN, now, CRGB(160, 0, 255), 800, true); break;
    case EffectId::Catch: fx::blink(out, kN, now, CRGB(255, 200, 0), 200); break;
    case EffectId::BallReceived: fx::breathe(out, kN, now, CRGB(0, 255, 60), 400); break;
    case EffectId::BallReleased: fx::comet(out, kN, now, CRGB(0, 255, 255), 400, false); break;
    case EffectId::Score:
      fx::sparkle(out, kN, CRGB(255, 200, 40), 255);
      fx::sparkle(out, kN, CRGB::White, 200);
      break;
    case EffectId::GameClear:
      fx::rainbow(out, kN, now * 4, 255);
      if (random8() < 120) out[random16(kN)] = CRGB::White;
      break;
    case EffectId::GameOver: fill_solid(out, kN, CRGB(255 - progress, 0, 0)); break;
    default: break;
  }
  if (e.intensity < 255) fx::fadeAll(out, kN, e.intensity);
}

void LedRenderer::render(const LightingController& lc, uint32_t now) {
  if (now - last_frame_ < 16) return;
  last_frame_ = now;
  const LightModeState& m = lc.mode();
  if (m.generation != generation_) {
    generation_ = m.generation;
    memcpy(g_prev, g_leds, sizeof(g_leds));
    fade_start_ = now;
    fade_ms_ = m.transition_ms;
  }
  drawMode(m, g_mode, now);
  if (lc.effectActive()) {
    drawEffect(lc.effect(), g_fx, now);
    memcpy(g_leds, g_fx, sizeof(g_leds));
  } else {
    memcpy(g_leds, g_mode, sizeof(g_leds));
  }
  if (fade_ms_ > 0 && now - fade_start_ < fade_ms_) {
    uint8_t amt = static_cast<uint8_t>((now - fade_start_) * 255 / fade_ms_);
    for (uint16_t i = 0; i < kN; i++) g_leds[i] = blend(g_prev[i], g_leds[i], amt);
  }
  uint8_t b = m.mode == LightMode::Off && !lc.effectActive() ? 0 : m.brightness;
  if (lc.effectActive() || m.mode == LightMode::Error) b = max<uint8_t>(b, 128);  // effects stay visible
  g_strip->showLeds(min(b, max_brightness_));
  ++frames_;
}

}  // namespace ufo

#endif
