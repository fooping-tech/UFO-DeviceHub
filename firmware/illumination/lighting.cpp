#include "lighting.h"

namespace ufo {

uint8_t LightingController::priority(EffectId e) {
  switch (e) {
    case EffectId::GameClear: return 3;
    case EffectId::Score: return 2;
    case EffectId::None: return 0;
    default: return 1;
  }
}

uint16_t LightingController::defaultDuration(EffectId e) {
  switch (e) {
    case EffectId::GameStart: return 1500;
    case EffectId::ArmMove: return 600;
    case EffectId::ArmDescend: return 1200;
    case EffectId::Catch: return 800;
    case EffectId::BallReceived: return 1000;
    case EffectId::BallReleased: return 1000;
    case EffectId::Score: return 1500;
    case EffectId::GameClear: return 4000;
    case EffectId::GameOver: return 2500;
    default: return 1000;
  }
}

void LightingController::begin(LightMode initial, uint32_t now) {
  mode_ = LightModeState();
  mode_.mode = initial;
  mode_.changed_at = now;
  effect_ = EffectState();
}

CommandOutcome LightingController::setMode(const SetLightModeParams& p, uint32_t now) {
  CommandOutcome o;
  if (p.mode > static_cast<uint8_t>(LightMode::Custom)) {
    o.result = AckResult::InvalidParams;
    return o;
  }
  mode_.mode = static_cast<LightMode>(p.mode);
  mode_.brightness = p.brightness;
  mode_.transition_ms = p.transition_ms;
  mode_.speed = p.speed;
  mode_.r = p.r;
  mode_.g = p.g;
  mode_.b = p.b;
  mode_.changed_at = now;
  ++mode_.generation;
  if (mode_.mode == LightMode::Error) effect_.active = false;  // ERROR has the highest priority
  return o;
}

CommandOutcome LightingController::trigger(const TriggerEffectParams& p, uint32_t now) {
  CommandOutcome o;
  if (p.effect < static_cast<uint8_t>(EffectId::GameStart) || p.effect > static_cast<uint8_t>(EffectId::GameOver)) {
    o.result = AckResult::InvalidParams;
    return o;
  }
  EffectId id = static_cast<EffectId>(p.effect);
  if (mode_.mode == LightMode::Error) {
    o.result = AckResult::IgnoredPriority;
    return o;
  }
  tick(now);
  if (effect_.active && priority(id) < priority(effect_.id)) {
    o.result = AckResult::IgnoredPriority;
    return o;
  }
  uint16_t d = p.duration_ms == 0 ? defaultDuration(id) : p.duration_ms;
  if (d > kMaxEffectMs) {
    d = kMaxEffectMs;
    o.flags |= u8(AckFlag::Clamped);
  }
  effect_.active = true;
  effect_.id = id;
  effect_.started_at = now;
  effect_.duration_ms = d;
  effect_.intensity = p.intensity;
  return o;
}

void LightingController::tick(uint32_t now) {
  if (effect_.active && now - effect_.started_at >= effect_.duration_ms) effect_.active = false;
}

uint16_t LightingController::effectRemaining(uint32_t now) const {
  if (!effect_.active) return 0;
  uint32_t el = now - effect_.started_at;
  return el >= effect_.duration_ms ? 0 : static_cast<uint16_t>(effect_.duration_ms - el);
}

}  // namespace ufo
