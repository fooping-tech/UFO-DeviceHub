#if defined(ARDUINO)

#include <Arduino.h>  // delay(), ledc*: FastLED 3.10+ no longer pulls in Arduino.h

#include "led_renderer.h"

#include "led_effects.h"
#include "ufo_config.h"

// Arduino-ESP32 3.x attaches LEDC by pin, 2.x (espressif32@6.x) by channel.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#define UFO_LEDC_V3 1
#else
#define UFO_LEDC_V3 0
#endif

namespace ufo {

namespace {
constexpr uint16_t kN = ILLUM_LED_COUNT;
CRGB g_leds[kN];   // what goes to the strip
CRGB g_prev[kN];   // snapshot at the start of a cross-fade
CRGB g_mode[kN];   // persistent mode layer
CRGB g_fx[kN];     // effect layer
fx::PacificaState g_pacifica;
fx::FireState g_fire;
CLEDController* g_strip = nullptr;
constexpr uint32_t kCobMaxDuty = (1u << ILLUM_COB_PWM_BITS) - 1;

// ---- mist scenes (ported from the former atom_led_mist sketch) ----

// Blue-green .. violet light drifting slowly.
void aurora(CRGB* out, uint32_t now) {
  for (uint16_t i = 0; i < kN; i++) {
    uint8_t w1 = sin8(static_cast<uint8_t>(i * 7 + now / 10));
    uint8_t w2 = sin8(static_cast<uint8_t>(i * 3 - now / 17));
    out[i] = CHSV(130 + (w1 >> 2), 255, 50 + scale8(w2, 205));
  }
}

// Dark blue-grey clouds, a lightning bolt lights up around a random spot.
void storm(CRGB* out, uint8_t bolt, uint32_t now) {
  static uint16_t center = kN / 2;
  static uint8_t last_bolt = 0;
  if (bolt > 0 && last_bolt == 0) center = random16(kN);
  last_bolt = bolt;
  for (uint16_t i = 0; i < kN; i++) {
    uint8_t cloud = 12 + scale8(sin8(static_cast<uint8_t>(i * 5 + now / 25)), 38);
    out[i] = CHSV(165, 180, cloud);
    if (bolt > 0) {
      int d = abs(static_cast<int>(i) - static_cast<int>(center));
      int fall = 255 - (d * 4 < 235 ? d * 4 : 235);
      nblend(out[i], CRGB(230, 230, 255), static_cast<uint8_t>(static_cast<uint16_t>(bolt) * fall / 255));
    }
  }
}

// Brightness follows the mist, the hue wanders slowly.
void mistGlow(CRGB* out, uint8_t glow) {
  uint8_t v = 40 + scale8(glow, 215);
  uint8_t hue = beatsin8(5, 130, 230);
  for (uint16_t i = 0; i < kN; i++) out[i] = CHSV(static_cast<uint8_t>(hue + i / 2), 255, v);
}

// Sparkles pop while the mist is on and fade out when it stops.
void magicPuff(CRGB* out, uint8_t glow) {
  fadeToBlackBy(out, kN, 25);
  for (uint8_t k = 0; k < 2; k++)
    if (random8() < scale8(glow, 160)) out[random16(kN)] = CHSV(random8(), 200, 255);
}

// Candle-like flicker for the COB tape.
uint8_t cobFlicker(uint32_t now) {
  static uint32_t next = 0;
  static uint8_t target = 200;
  static int cur = 200;
  if (static_cast<int32_t>(now - next) >= 0) {
    target = random8(90, 255);
    next = now + random8(40, 120);
  }
  cur += (static_cast<int>(target) - cur) / 4;
  return static_cast<uint8_t>(cur);
}
}  // namespace

void LedRenderer::begin(uint8_t max_brightness, int cob_pin, uint8_t cob_max) {
  max_brightness_ = max_brightness;
  cob_pin_ = cob_pin;
  cob_max_ = cob_max;
  if (cob_pin_ >= 0) {
#if UFO_LEDC_V3
    ledcAttach(cob_pin_, ILLUM_COB_PWM_FREQ, ILLUM_COB_PWM_BITS);
#else
    ledcSetup(ILLUM_COB_LEDC_CHANNEL, ILLUM_COB_PWM_FREQ, ILLUM_COB_PWM_BITS);
    ledcAttachPin(cob_pin_, ILLUM_COB_LEDC_CHANNEL);
#endif
    writeCob(0);
  }
  // Each strip is shown through its own controller (the status LED has another one).
  g_strip = &FastLED.addLeds<ILLUM_LED_CHIPSET, ILLUM_LED_PIN, ILLUM_LED_COLOR_ORDER>(g_leds, kN);
  fill_solid(g_leds, kN, CRGB::Black);
  g_strip->showLeds(0);
}

void LedRenderer::colorOrderTest() {
  const CRGB colors[] = {CRGB::Red, CRGB::Green, CRGB::Blue};
  for (const CRGB& c : colors) {
    fill_solid(g_leds, kN, c);
    g_strip->showLeds(min<uint8_t>(100, max_brightness_));
    delay(500);
  }
  fill_solid(g_leds, kN, CRGB::Black);
  g_strip->showLeds(0);
}

void LedRenderer::writeCob(uint8_t v) {
  if (cob_pin_ < 0) return;
  v = scale8(v, cob_max_);
  uint32_t duty = static_cast<uint32_t>(v) * v * kCobMaxDuty / 65025;  // square curve: smooth at low levels
#if UFO_LEDC_V3
  ledcWrite(cob_pin_, duty);
#else
  ledcWrite(ILLUM_COB_LEDC_CHANNEL, duty);
#endif
}

void LedRenderer::updateLightning(uint32_t now) {
  static uint32_t burst_start = 0, burst_end = 0, next = 2500;
  if (static_cast<int32_t>(now - next) >= 0) {
    burst_start = now;
    burst_end = now + random16(180, 520);
    next = burst_end + random16(2500, 8000);
  }
  if (static_cast<int32_t>(now - burst_start) >= 0 && static_cast<int32_t>(now - burst_end) < 0) {
    uint8_t phase = ((now - burst_start) / 45) % 3;  // flickers every 45 ms
    bolt_ = phase == 1 ? 70 : 255;
  } else {
    bolt_ = 0;
  }
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
    case LightMode::Aurora: aurora(out, now); break;
    case LightMode::Storm: storm(out, bolt_, now); break;
    case LightMode::Campfire: fx::fire(out, kN, g_fire); break;
    case LightMode::MistGlow: mistGlow(out, mist_glow_); break;
    case LightMode::MagicPuff: magicPuff(out, mist_glow_); break;
    case LightMode::Night: fill_solid(out, kN, CRGB::Black); break;  // COB only
  }
}

// COB level (0-255, before ILLUM_COB_MAX_BRIGHTNESS) of the persistent mode.
uint8_t LedRenderer::cobMode(const LightModeState& m, uint32_t now) {
  switch (m.mode) {
    case LightMode::Idle: return beatsin8(5, 20, 90);
    case LightMode::Study: return 220;  // desk light
    case LightMode::GameReady: return 60;
    case LightMode::GamePlay: return 80;  // warm base under the rainbow
    case LightMode::Result: return 150;
    case LightMode::Custom: return m.brightness;
    case LightMode::Aurora: return beatsin8(7, 25, 150);
    case LightMode::Storm: {
      uint8_t b = scale8(bolt_, 220);
      return b > 25 ? b : 25;
    }
    case LightMode::Campfire: return cobFlicker(now);
    case LightMode::MistGlow:
    case LightMode::MagicPuff: return 20 + scale8(mist_glow_, 150);
    case LightMode::Night: return beatsin8(6, 10, 110);
    default: return 0;  // OFF, ERROR (red blink on the strip only)
  }
}

uint8_t LedRenderer::cobEffect(const EffectState& e, uint8_t base, uint32_t now) {
  uint32_t el = now - e.started_at;
  uint8_t progress = e.duration_ms ? static_cast<uint8_t>(min<uint32_t>(255, el * 255 / e.duration_ms)) : 255;
  uint8_t v;
  switch (e.id) {
    case EffectId::GameStart: v = progress < 170 ? progress * 3 / 2 : (((now / 100) % 2) ? 255 : 0); break;
    case EffectId::Catch: v = ((now / 100) % 2) ? 255 : 0; break;
    case EffectId::Score:
    case EffectId::GameClear: v = 255; break;
    case EffectId::GameOver: v = 255 - progress; break;
    default: v = base; break;
  }
  return e.intensity < 255 ? scale8(v, e.intensity) : v;
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

void LedRenderer::render(const LightingController& lc, bool mist_on, uint32_t now) {
  if (now - last_frame_ < 16) return;
  last_frame_ = now;
  // ~1 s up while the mist is ON, ~2 s down after it stops
  mist_glow_ = mist_on ? qadd8(mist_glow_, 4) : qsub8(mist_glow_, 2);
  updateLightning(now);

  const LightModeState& m = lc.mode();
  if (m.generation != generation_) {
    generation_ = m.generation;
    memcpy(g_prev, g_leds, sizeof(g_leds));
    cob_prev_ = cob_now_;
    fade_start_ = now;
    fade_ms_ = m.transition_ms;
  }
  drawMode(m, g_mode, now);
  uint8_t cob = cobMode(m, now);
  if (lc.effectActive()) {
    drawEffect(lc.effect(), g_fx, now);
    memcpy(g_leds, g_fx, sizeof(g_leds));
    cob = cobEffect(lc.effect(), cob, now);
  } else {
    memcpy(g_leds, g_mode, sizeof(g_leds));
  }
  if (fade_ms_ > 0 && now - fade_start_ < fade_ms_) {
    uint8_t amt = static_cast<uint8_t>((now - fade_start_) * 255 / fade_ms_);
    for (uint16_t i = 0; i < kN; i++) g_leds[i] = blend(g_prev[i], g_leds[i], amt);
    cob = lerp8by8(cob_prev_, cob, amt);
  }
  cob_now_ = cob;
  writeCob(cob);

  uint8_t b = m.mode == LightMode::Off && !lc.effectActive() ? 0 : m.brightness;
  if (lc.effectActive() || m.mode == LightMode::Error) b = max<uint8_t>(b, 128);  // effects stay visible
  g_strip->showLeds(min(b, max_brightness_));
  ++frames_;
}

}  // namespace ufo

#endif
