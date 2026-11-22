#if defined(ARDUINO)

#include "led_renderer.h"

#include "led_effects.h"
#include "shelf_layout.h"
#include "ufo_config.h"

// Arduino-ESP32 3.x attaches LEDC by pin, 2.x (espressif32@6.x) by channel.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#define UFO_LEDC_V3 1
#else
#define UFO_LEDC_V3 0
#endif

namespace ufo {

namespace {
// The pixel count comes from the shelf layout (shelf_layout.h); ILLUM_LED_COUNT in
// config/hardware.h must agree, so a stale config is a build error, not a dark shelf.
constexpr uint16_t kN = shelf::kLedCount;
static_assert(ILLUM_LED_COUNT == shelf::kLedCount,
              "ILLUM_LED_COUNT (config/hardware.h) differs from the shelf layout (firmware/illumination/shelf_layout.h)");
CRGB g_leds[kN];   // what goes to the strip
CRGB g_prev[kN];   // snapshot at the start of a cross-fade
CRGB g_mode[kN];   // persistent mode layer
CRGB g_fx[kN];     // effect layer
CRGB g_confetti[kN];  // the title show's confetti (keeps fading between frames)
shelf::Pixel g_px[kN];  // where each pixel is (filled once in begin())
fx::FireState g_fire;
CLEDController* g_strip = nullptr;
constexpr uint32_t kCobMaxDuty = (1u << ILLUM_COB_PWM_BITS) - 1;


// ---- shelf geometry helpers ----
// 255 at distance 0, falling linearly to 0 at `width`.
uint8_t near8(int d, int width) {
  d = d < 0 ? -d : d;
  return d >= width ? 0 : static_cast<uint8_t>(255 - d * 255 / width);
}
// Distance between two places around the shelf (wraps: 250 and 5 are 11 apart).
int aroundDist(uint8_t a, uint8_t b) {
  uint8_t d = static_cast<uint8_t>(a - b);
  return d > 128 ? 256 - d : d;
}
// 0..255 position inside a repeating period.
uint8_t cycle8(uint32_t t, uint16_t period_ms) {
  if (period_ms == 0) period_ms = 1;
  return static_cast<uint8_t>((t % period_ms) * 256 / period_ms);
}
CRGB scaled(CRGB c, uint8_t v) { return c.nscale8_video(v); }
// Pixels that are not part of the show stay dark whatever the effect draws.
void blankUnused(CRGB* out) {
  for (uint16_t i = 0; i < kN; i++)
    if (g_px[i].part == shelf::Part::Unused) out[i] = CRGB::Black;
}
// A band of light running up (rising) or down the pillars, with a tail behind it.
// head: 0..255 (where the band is now, as a height); tail: how long the trail is.
uint8_t verticalBand(const shelf::Pixel& p, uint8_t head, bool rising, int tail) {
  int d = rising ? head - p.height : p.height - head;  // >= 0 behind the head
  if (d < -24) return 0;
  if (d < 0) return near8(d, 24);
  return d >= tail ? 0 : static_cast<uint8_t>(255 - d * 255 / tail);
}

// ---- the title show (IDLE): lively, many colours, changes scene every few seconds ----
// A: a rainbow spinning around the shelf, ripples running up the pillars.
CRGB showSpin(const shelf::Pixel& p, uint32_t now) {
  uint8_t hue = static_cast<uint8_t>(p.around + now / 12);
  uint8_t v = 120 + scale8(sin8(static_cast<uint8_t>(p.height / 2 - now / 5)), 135);
  return CHSV(hue, 240, v);
}
// B: every pillar its own colour, light bands climbing them; a colour chase along the front.
CRGB showRise(const shelf::Pixel& p, uint16_t /*i*/, uint32_t now) {
  if (p.part == shelf::Part::Front) {
    uint8_t hue = static_cast<uint8_t>(p.pos * 36 + now / 8);
    return CHSV(hue, 255, 60 + scale8(sin8(static_cast<uint8_t>(p.pos * 48 - now / 3)), 195));
  }
  uint8_t hue = static_cast<uint8_t>(p.around + now / 25);
  uint8_t band = verticalBand(p, cycle8(now, 900), true, 140);
  return CHSV(hue, 255, 50 + scale8(band, 205));
}
// C: confetti popping all over a slowly drifting pastel.
void showConfetti(uint32_t now) {
  fadeToBlackBy(g_confetti, kN, 18);
  for (uint8_t k = 0; k < 2; k++)
    if (random8() < 140) g_confetti[random16(kN)] = CHSV(random8(), 200, 255);
  (void)now;
}
CRGB showConfettiAt(const shelf::Pixel& p, uint16_t i, uint32_t now) {
  CRGB base = CHSV(static_cast<uint8_t>(now / 30 + p.around), 160, 40);
  return base + g_confetti[i];
}
CRGB showScene(uint8_t scene, const shelf::Pixel& p, uint16_t i, uint32_t now) {
  switch (scene) {
    case 0: return showSpin(p, now);
    case 1: return showRise(p, i, now);
    default: return showConfettiAt(p, i, now);
  }
}
void titleShow(CRGB* out, uint32_t now) {
  constexpr uint32_t kSceneMs = 7000, kBlendMs = 900;
  constexpr uint8_t kScenes = 3;
  showConfetti(now);
  uint8_t scene = static_cast<uint8_t>((now / kSceneMs) % kScenes);
  uint32_t in_scene = now % kSceneMs;
  uint8_t next = static_cast<uint8_t>((scene + 1) % kScenes);
  uint8_t amt = in_scene > kSceneMs - kBlendMs ? static_cast<uint8_t>((in_scene - (kSceneMs - kBlendMs)) * 255 / kBlendMs) : 0;
  for (uint16_t i = 0; i < kN; i++) {
    CRGB c = showScene(scene, g_px[i], i, now);
    if (amt > 0) c = blend(c, showScene(next, g_px[i], i, now), amt);
    out[i] = c;
  }
}

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
  for (uint16_t i = 0; i < kN; i++) g_px[i] = shelf::pixel(i);
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

void LedRenderer::layoutTest() {
  // Each pillar from its top down, then the front face from left to right, one
  // pixel at a time, in the pillar's own colour: the order on the shelf shows
  // whether shelf_layout.h matches the wiring.
  const CRGB colors[shelf::kPartCount] = {CRGB::Red, CRGB::White, CRGB::Green, CRGB::Blue, CRGB::Yellow};
  const shelf::Part order[] = {shelf::Part::FrontLeft, shelf::Part::FrontRight, shelf::Part::BackRight,
                               shelf::Part::BackLeft, shelf::Part::Front};
  fill_solid(g_leds, kN, CRGB::Black);
  for (shelf::Part part : order) {
    bool pillar = shelf::isPillar(part);
    for (uint8_t k = 0; k < shelf::partCount(part); k++) {
      int16_t i = shelf::index(part, k, pillar ? shelf::From::Top : shelf::From::Left);
      if (i < 0) continue;
      g_leds[i] = colors[static_cast<uint8_t>(part)];
      g_strip->showLeds(min<uint8_t>(100, max_brightness_));
      delay(70);
    }
  }
  delay(400);
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
    // Title screens (and no PC): a lively show of many colours.
    case LightMode::Idle: titleShow(out, now); break;
    // Study: a steady, bright, slightly warm white (the COB tape is the desk light).
    case LightMode::Study: fill_solid(out, kN, CRGB(255, 236, 205)); break;
    // Countdown: cyan energy climbing the pillars once a second, the front face
    // filling from both ends towards the middle.
    case LightMode::GameReady: {
      uint8_t ph = cycle8(now, 1000);
      for (uint16_t i = 0; i < kN; i++) {
        const shelf::Pixel& p = g_px[i];
        if (p.part == shelf::Part::Front) {
          int from_end = 32 - abs(static_cast<int>(p.around) - 32);  // 0 at the ends, 32 in the middle
          out[i] = scaled(CRGB(0, 190, 255), from_end * 8 <= ph ? 255 : 30);
        } else {
          bool lit = p.height <= ph;
          bool edge = lit && ph - p.height < 50;  // the bright head of the rise
          out[i] = scaled(CRGB(0, 190, 255), edge ? 255 : lit ? 200 : 25);
          if (edge) out[i] += CRGB(60, 60, 60);
        }
      }
      break;
    }
    // Play: a rainbow turning around the shelf, waves running up the pillars.
    // speed sets the pace; the last stretch (speed >= 200) adds white flashes.
    case LightMode::GamePlay: {
      uint32_t spin = now * (2 + m.speed / 32) / 16;
      uint32_t rise = now * (2 + m.speed / 48) / 8;
      for (uint16_t i = 0; i < kN; i++) {
        const shelf::Pixel& p = g_px[i];
        uint8_t hue = static_cast<uint8_t>(p.around + spin);
        uint8_t v = 110 + scale8(sin8(static_cast<uint8_t>(p.height / 2 + p.around - rise)), 145);
        out[i] = CHSV(hue, 255, v);
      }
      if (m.speed >= 200)
        for (uint8_t k = 0; k < 2; k++)
          if (random8() < 90) out[random16(kN)] = CRGB::White;
      break;
    }
    // Result: gold rising up the pillars like a fountain, glints everywhere.
    case LightMode::Result: {
      uint8_t head = cycle8(now, 1400);
      for (uint16_t i = 0; i < kN; i++) {
        const shelf::Pixel& p = g_px[i];
        uint8_t band = p.part == shelf::Part::Front ? beatsin8(40, 90, 255, 0, p.pos * 36)
                                                    : verticalBand(p, head, true, 160);
        out[i] = scaled(CRGB(255, 170, 30), 70 + scale8(band, 185));
      }
      if (random8() < 110) out[random16(kN)] = CRGB::White;
      break;
    }
    case LightMode::Error: fx::blink(out, kN, now, CRGB::Red, 1000); break;
    // Custom colour: speed 0 = steady; otherwise the colour swells up the pillars
    // (a period set by speed), with white glints when it is fast (the x2 gold).
    case LightMode::Custom:
      if (m.speed == 0) {
        fill_solid(out, kN, custom);
      } else {
        uint8_t head = cycle8(now, period);
        for (uint16_t i = 0; i < kN; i++)
          out[i] = scaled(custom, 90 + scale8(sin8(static_cast<uint8_t>(g_px[i].height / 2 - head)), 165));
        if (m.speed >= 160 && random8() < 70) out[random16(kN)] = CRGB::White;
      }
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
    case LightMode::Idle: return beatsin8(14, 30, 130);  // the title show: a lively pulse
    case LightMode::Study: return 255;  // desk light, full (ILLUM_COB_MAX_BRIGHTNESS caps it)
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
    // GO!: white light shoots up every pillar (the front face at the foot first),
    // then the whole shelf flashes.
    case EffectId::GameStart:
      if (progress < 170) {
        uint8_t level = progress * 3 / 2;
        for (uint16_t i = 0; i < kN; i++) {
          uint8_t v = verticalBand(g_px[i], level, true, 255);
          out[i] = scaled(CRGB::White, v);
        }
      } else {
        fx::blink(out, kN, now, CRGB::White, 200);
      }
      break;
    // The arm travels: a blue comet runs around the shelf.
    case EffectId::ArmMove: {
      uint8_t head = cycle8(el, 900);
      for (uint16_t i = 0; i < kN; i++) {
        int d = static_cast<uint8_t>(head - g_px[i].around);  // behind the head, 0..255
        uint8_t v = d < 90 ? static_cast<uint8_t>(255 - d * 255 / 90) : 0;
        out[i] = scaled(CRGB(0, 80, 255), v);
      }
      break;
    }
    // The arm goes down: violet light falls down the pillars.
    case EffectId::ArmDescend: {
      uint8_t head = static_cast<uint8_t>(255 - cycle8(el, 700));
      for (uint16_t i = 0; i < kN; i++) out[i] = scaled(CRGB(160, 0, 255), verticalBand(g_px[i], head, false, 150));
      break;
    }
    // Caught: yellow, the pillars and the front face flashing in turn.
    case EffectId::Catch: {
      bool odd = (el / 100) % 2;
      for (uint16_t i = 0; i < kN; i++) {
        bool front = g_px[i].part == shelf::Part::Front;
        out[i] = front == odd ? CRGB(255, 200, 0) : CRGB(40, 25, 0);
      }
      break;
    }
    // A capsule is taken in (on the screen it rises into UFO-kun): green rising.
    case EffectId::BallReceived: {
      uint8_t head = cycle8(el, 500);
      for (uint16_t i = 0; i < kN; i++)
        out[i] = scaled(CRGB(0, 255, 60), 40 + scale8(verticalBand(g_px[i], head, true, 130), 215));
      break;
    }
    // A capsule goes back out to the Dial: cyan falling down.
    case EffectId::BallReleased: {
      uint8_t head = static_cast<uint8_t>(255 - cycle8(el, 500));
      for (uint16_t i = 0; i < kN; i++)
        out[i] = scaled(CRGB(0, 255, 255), 30 + scale8(verticalBand(g_px[i], head, false, 130), 225));
      break;
    }
    // Jackpot / bonus: a white flash, then gold bursting out from the middle of the
    // front face around the shelf, white glints.
    case EffectId::Score: {
      int radius = static_cast<int>(min<uint32_t>(el, 600) * 160 / 600);
      for (uint16_t i = 0; i < kN; i++) {
        int d = aroundDist(g_px[i].around, 32) - radius;
        uint8_t ring = near8(d, 50);
        CRGB c = scaled(CRGB(255, 180, 30), 90 + scale8(ring, 165));
        if (el < 160) c = blend(CRGB::White, c, static_cast<uint8_t>(el * 255 / 160));
        out[i] = c;
      }
      for (uint8_t k = 0; k < 2; k++)
        if (random8() < 160) out[random16(kN)] = CRGB::White;
      break;
    }
    // The round ends (TIME UP): a fast rainbow whirling around, white sparkles.
    case EffectId::GameClear:
      for (uint16_t i = 0; i < kN; i++) {
        const shelf::Pixel& p = g_px[i];
        out[i] = CHSV(static_cast<uint8_t>(p.around * 2 + p.height / 2 + now / 3), 255, 255);
      }
      for (uint8_t k = 0; k < 2; k++)
        if (random8() < 140) out[random16(kN)] = CRGB::White;
      break;
    // Game over: red draining down the pillars and fading out.
    case EffectId::GameOver: {
      uint8_t level = 255 - progress;
      for (uint16_t i = 0; i < kN; i++)
        out[i] = g_px[i].height <= level ? CRGB(255 - progress, 0, 0) : CRGB::Black;
      break;
    }
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
  blankUnused(g_leds);
  cob_now_ = cob;
  writeCob(cob);

  uint8_t b = m.mode == LightMode::Off && !lc.effectActive() ? 0 : m.brightness;
  if (lc.effectActive() || m.mode == LightMode::Error) b = max<uint8_t>(b, 128);  // effects stay visible
  g_strip->showLeds(min(b, max_brightness_));
  ++frames_;
}

}  // namespace ufo

#endif
