#include "illumination_controller.h"

namespace ufo {

void IlluminationController::begin(uint32_t now) {
  now_ = now;
  relay_.setListener(this);
  relay_.begin();
  lighting_.begin(LightMode::Idle, now);
}

void IlluminationController::tick(uint32_t now) {
  now_ = now;
  relay_.tick(now);
  lighting_.tick(now);
}

CommandOutcome IlluminationController::onCommand(CommandCode code, const uint8_t* params, size_t len,
                                                 uint32_t /*command_id*/, uint32_t now) {
  now_ = now;
  CommandOutcome o;
  switch (code) {
    case CommandCode::StopAll:
      relay_.allOff(now, RelayEndReason::Safety);
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
    default:
      o.result = AckResult::Unsupported;  // ARM/DISARM/RELEASE are M5Dial commands
      return o;
  }
}

void IlluminationController::goSafe(uint32_t now, bool reset_light) {
  relay_.allOff(now, RelayEndReason::Safety);
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
  goSafe(now, session == 0);
}

void IlluminationController::onHubLinkChanged(bool up, uint32_t now) {
  link_up_ = up;
  if (!up) goSafe(now, false);  // relay OFF; the LEDs keep their current mode
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

uint8_t IlluminationController::state() const {
  if (relay_.anyOn()) return static_cast<uint8_t>(IlluminationState::RelayActive);
  if (lighting_.effectActive()) return static_cast<uint8_t>(IlluminationState::Effect);
  return static_cast<uint8_t>(IlluminationState::Normal);
}

uint8_t IlluminationController::flags() const {
  uint8_t f = 0;
  if (relay_.anyOn()) f |= u8(NodeFlag::RelayOn) | u8(NodeFlag::Busy);
  if (op_mode_ == OperationMode::Game) f |= u8(NodeFlag::GameMode);
  bool any_configured = false;
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
}

}  // namespace ufo
