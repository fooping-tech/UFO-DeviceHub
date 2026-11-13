// Lighting state: persistent LED mode + temporary effects with priorities
// (pure logic; the FastLED drawing lives in led_renderer.*).
//
// Priority (high -> low):  ERROR mode > GAME_CLEAR > SCORE > other effects > normal mode
//  * while the base mode is ERROR, effects are refused (IGNORED_PRIORITY)
//  * a new effect replaces the running one if its priority is >= the running one
//  * when an effect ends, the renderer falls back to the base mode
#pragma once

#include <stdint.h>

#include "messages.h"
#include "node_core.h"

namespace ufo {

struct LightModeState {
  LightMode mode = LightMode::Idle;
  uint8_t brightness = 128;
  uint16_t transition_ms = 0;
  uint8_t speed = 128;
  uint8_t r = 0, g = 0, b = 0;
  uint32_t changed_at = 0;
  uint32_t generation = 0;  // increments on every mode change (renderer cross-fade)
};

struct EffectState {
  bool active = false;
  EffectId id = EffectId::None;
  uint32_t started_at = 0;
  uint16_t duration_ms = 0;
  uint8_t intensity = 255;
};

class LightingController {
 public:
  static uint8_t priority(EffectId e);
  static uint16_t defaultDuration(EffectId e);
  static constexpr uint16_t kMaxEffectMs = 10000;

  void begin(LightMode initial, uint32_t now);
  CommandOutcome setMode(const SetLightModeParams& p, uint32_t now);
  CommandOutcome trigger(const TriggerEffectParams& p, uint32_t now);
  void stopEffects() { effect_.active = false; }
  void tick(uint32_t now);

  const LightModeState& mode() const { return mode_; }
  const EffectState& effect() const { return effect_; }
  bool effectActive() const { return effect_.active; }
  uint16_t effectRemaining(uint32_t now) const;

 private:
  LightModeState mode_;
  EffectState effect_;
};

}  // namespace ufo
