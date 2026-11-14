#include "illumination_controller.h"

namespace ufo {

// Scenes of the former atom_led_mist sketch. Index 0 = everything off.
const LocalScene IlluminationController::kLocalScenes[] = {
    {LightMode::Off, MistPattern::Off, "off"},
    {LightMode::Aurora, MistPattern::Continuous, "aurora mist"},
    {LightMode::Storm, MistPattern::Continuous, "storm cloud"},
    {LightMode::Campfire, MistPattern::Continuous, "campfire smoke"},
    {LightMode::MistGlow, MistPattern::Breath, "breathing mist"},
    {LightMode::GamePlay, MistPattern::Continuous, "rainbow mist"},
    {LightMode::MagicPuff, MistPattern::Puff, "magic puff"},
    {LightMode::Night, MistPattern::Breath, "night"},
};
const uint8_t IlluminationController::kLocalSceneCount = sizeof(kLocalScenes) / sizeof(kLocalScenes[0]);

void IlluminationController::begin(uint32_t now) {
  now_ = now;
  relay_.setListener(this);
  relay_.begin();
  mist_.setListener(this);
  mist_.begin();
  lighting_.begin(LightMode::Idle, now);
  local_ = false;
  local_mist_ = local_cfg_.mist;
  scene_ = 0;
}

void IlluminationController::tick(uint32_t now) {
  now_ = now;
  relay_.tick(now);
  mist_.tick(now);
  lighting_.tick(now);
}

void IlluminationController::applyScene(uint32_t now) {
  const LocalScene& s = kLocalScenes[scene_];
  SetLightModeParams p;
  p.mode = static_cast<uint8_t>(s.mode);
  p.brightness = local_cfg_.brightness;
  p.transition_ms = 800;
  lighting_.stopEffects();
  lighting_.setMode(p, now);
  if (local_mist_ && s.mist != MistPattern::Off && mist_.configured())
    mist_.start(s.mist, 0, now);  // runs up to the device maximum
  else
    mist_.stop(now, RelayEndReason::LocalButton);
}

ButtonResult IlluminationController::onButtonShort(uint32_t now) {
  now_ = now;
  if (!local_cfg_.enabled || session_) return ButtonResult::Ignored;  // the PC is in control
  local_ = true;
  scene_ = static_cast<uint8_t>((scene_ + 1) % kLocalSceneCount);
  applyScene(now);
  return ButtonResult::SceneChanged;
}

ButtonResult IlluminationController::onButtonLong(uint32_t now) {
  now_ = now;
  if (mist_.active()) {  // physical stop, also while the PC is in control
    mist_.stop(now, RelayEndReason::LocalButton);
    if (local_) local_mist_ = false;
    return ButtonResult::MistOff;
  }
  if (!local_cfg_.enabled || !local_cfg_.mist || session_ || !mist_.configured()) return ButtonResult::Ignored;
  local_ = true;
  local_mist_ = true;
  MistPattern p = kLocalScenes[scene_].mist;
  if (p == MistPattern::Off) p = MistPattern::Continuous;  // e.g. scene "off": mist only
  mist_.start(p, 0, now);
  return ButtonResult::MistOn;
}

CommandOutcome IlluminationController::onCommand(CommandCode code, const uint8_t* params, size_t len,
                                                 uint32_t /*command_id*/, uint32_t now) {
  now_ = now;
  CommandOutcome o;
  switch (code) {
    case CommandCode::StopAll:
      relay_.allOff(now, RelayEndReason::Safety);
      mist_.stop(now, RelayEndReason::Safety);
      lighting_.stopEffects();
      return o;
    case CommandCode::SetMode: {
      OperationMode m;
      if (!decodeSetMode(params, len, m)) {
        o.result = AckResult::InvalidParams;
        return o;
      }
      op_mode_ = m;
      return o;
    }
    case CommandCode::SetLightMode: {
      SetLightModeParams p;
      if (!decodeSetLightMode(params, len, p)) {
        o.result = AckResult::InvalidParams;
        return o;
      }
      return lighting_.setMode(p, now);
    }
    case CommandCode::TriggerEffect: {
      TriggerEffectParams p;
      if (!decodeTriggerEffect(params, len, p)) {
        o.result = AckResult::InvalidParams;
        return o;
      }
      return lighting_.trigger(p, now);
    }
    case CommandCode::RelayPulse: {
      RelayPulseParams p;
      if (!decodeRelayPulse(params, len, p)) {
        o.result = AckResult::InvalidParams;
        return o;
      }
      if (!link_up_) {  // never start the steam without a live hub link
        o.result = AckResult::RejectedState;
        return o;
      }
      return relay_.pulse(p.relay_id, p.duration_ms, now);
    }
    case CommandCode::RelayOff: {
      uint8_t id;
      if (!decodeRelayOff(params, len, id)) {
        o.result = AckResult::InvalidParams;
        return o;
      }
      return relay_.off(id, now, RelayEndReason::OffCommand);
    }
    case CommandCode::SetMist: {
      SetMistParams p;
      if (!decodeSetMist(params, len, p)) {
        o.result = AckResult::InvalidParams;
        return o;
      }
      MistPattern mp = static_cast<MistPattern>(p.pattern);
      if (mp != MistPattern::Off && !link_up_) {  // never start the mist without a live hub link
        o.result = AckResult::RejectedState;
        return o;
      }
      return mist_.start(mp, p.duration_s, now);
    }
    default:
      o.result = AckResult::Unsupported;  // ARM/DISARM/RELEASE are M5Dial commands
      return o;
  }
}

void IlluminationController::goSafe(uint32_t now, bool reset_light) {
  relay_.allOff(now, RelayEndReason::Safety);
  mist_.stop(now, RelayEndReason::Safety);
  lighting_.stopEffects();
  if (reset_light) {
    SetLightModeParams p;
    p.mode = static_cast<uint8_t>(session_end_mode_);
    p.brightness = lighting_.mode().brightness;
    p.transition_ms = 500;
    lighting_.setMode(p, now);
  }
}

void IlluminationController::onSessionChanged(uint32_t session, uint32_t now) {
  op_mode_ = OperationMode::Standby;
  session_ = session != 0;
  goSafe(now, session == 0 || local_);  // while local_ is set, the stop is not reported
  local_ = false;  // the PC takes over (or the PC session ended)
  scene_ = 0;
  local_mist_ = local_cfg_.mist;
}

void IlluminationController::onHubLinkChanged(bool up, uint32_t now) {
  link_up_ = up;
  // relay / mist OFF; the LEDs keep their current mode. Local mode does not
  // depend on the hub and keeps running.
  if (!up && !local_) goSafe(now, false);
}

void IlluminationController::onRelayDone(uint8_t id, uint32_t on_ms, RelayEndReason reason, uint32_t now) {
  if (!emitter_) return;
  EventData e;
  e.code = static_cast<uint8_t>(EventCode::RelayDone);
  e.relay_id = id;
  e.elapsed_ms = on_ms > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(on_ms);
  e.reason = static_cast<uint8_t>(reason);
  emitter_->emit(e, now);
}

void IlluminationController::onMistDone(uint32_t on_ms, RelayEndReason reason, uint32_t now) {
  if (!emitter_ || local_) return;  // local runs are not reported (no PC session anyway)
  EventData e;
  e.code = static_cast<uint8_t>(EventCode::MistDone);
  uint32_t s = on_ms / 1000;
  e.on_s = s > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(s);
  e.reason = static_cast<uint8_t>(reason);
  emitter_->emit(e, now);
}

uint8_t IlluminationController::state() const {
  if (relay_.anyOn()) return static_cast<uint8_t>(IlluminationState::RelayActive);
  if (mist_.active()) return static_cast<uint8_t>(IlluminationState::MistActive);
  if (lighting_.effectActive()) return static_cast<uint8_t>(IlluminationState::Effect);
  return static_cast<uint8_t>(IlluminationState::Normal);
}

uint8_t IlluminationController::flags() const {
  uint8_t f = 0;
  if (relay_.anyOn()) f |= u8(NodeFlag::RelayOn) | u8(NodeFlag::Busy);
  if (mist_.outputOn()) f |= u8(NodeFlag::RelayOn);
  if (op_mode_ == OperationMode::Game) f |= u8(NodeFlag::GameMode);
  bool any_configured = mist_.configured();
  for (uint8_t i = 0; i < relay_.count(); ++i) any_configured |= relay_.configured(i);
  if (!any_configured) f |= u8(NodeFlag::ConfigError);
  return f;
}

void IlluminationController::fillStatus(IlluminationStatus& il, DialStatus& /*dl*/) {
  il.light_mode = static_cast<uint8_t>(lighting_.mode().mode);
  il.brightness = lighting_.mode().brightness;
  il.active_effect = lighting_.effectActive() ? static_cast<uint8_t>(lighting_.effect().id) : 0;
  il.effect_remaining_ms = lighting_.effectRemaining(now_);
  il.relay_on_mask = relay_.onMask();
  il.relay_remaining_ms = relay_.remainingMs(now_);
  il.relay_cooldown_ms = relay_.cooldownMs(now_);
  il.relay_pulses = relay_.pulses();
  il.mist_pattern = static_cast<uint8_t>(mist_.pattern());
  il.mist_flags = 0;
  if (mist_.outputOn()) il.mist_flags |= u8(MistStatusFlag::OutputOn);
  if (local_) il.mist_flags |= u8(MistStatusFlag::LocalMode);
  il.mist_remaining_s = mist_.remainingS(now_);
  il.mist_on_s = mist_.onS(now_);
}

}  // namespace ufo
