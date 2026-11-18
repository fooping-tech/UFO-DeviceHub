#include "dial_controller.h"

namespace ufo {

void DialController::begin(bool raw_switch_on, uint32_t now) {
  receiver_.reset(raw_switch_on, now);
  dispenser_.begin(now);  // servo to standby
  if (!dispenser_.config().configured) setConfigError(ErrorCode::ServoNotConfigured);
  if (state_ != DialState::Error) state_ = DialState::Disabled;
  op_mode_ = OperationMode::Standby;
  clearEffects();
  resetUnitColor();
}

void DialController::clearEffects() {
  for (auto& s : slots_) s.clear();
}

void DialController::resetUnitColor() {
  color_[0] = cfg_.color_r;
  color_[1] = cfg_.color_g;
  color_[2] = cfg_.color_b;
}

uint32_t DialController::emit(EventData& e, uint32_t now) {
  return emitter_ ? emitter_->emit(e, now) : 0;
}

void DialController::toIdleState() {
  if (state_ == DialState::Error || state_ == DialState::Settings) return;
  state_ = op_mode_ == OperationMode::Game ? DialState::Ready : DialState::Disabled;
}

bool DialController::enterSettings(uint32_t now) {
  (void)now;
  if (state_ == DialState::Settings) return true;
  if (dispenser_.busy()) return false;
  if (state_ != DialState::Disabled && state_ != DialState::Ready && state_ != DialState::Complete &&
      state_ != DialState::Error)
    return false;  // ARMED / BALL_DETECTED: a game cycle is running
  op_mode_ = OperationMode::Standby;
  wait_clear_ = false;
  settings_switch_count_ = 0;
  clearEffects();  // the settings UI owns the LEDs
  state_ = DialState::Settings;
  return true;
}

void DialController::exitSettings(uint32_t now) {
  if (state_ != DialState::Settings) return;
  if (dispenser_.phase() == Dispenser::Phase::Manual) dispenser_.endManual(now);
  state_ = error_ != ErrorCode::None ? DialState::Error : DialState::Disabled;
  op_mode_ = OperationMode::Standby;
  wait_clear_ = false;
}

bool DialController::settingsReleaseTest(uint32_t now) {
  if (state_ != DialState::Settings || !dispenser_.start(0, now)) return false;
  quiet_motion_ = true;
  return true;
}

bool DialController::settingsServoAngle(uint8_t deg, uint32_t now) {
  if (state_ != DialState::Settings || !dispenser_.manualMove(deg, now)) return false;
  quiet_motion_ = true;
  return true;
}

void DialController::settingsServoHome(uint32_t now) {
  if (state_ == DialState::Settings) dispenser_.endManual(now);
}

bool DialController::localRelease(uint32_t now) {
  if (state_ == DialState::Settings || !dispenser_.config().configured) return false;
  if (!dispenser_.start(0, now)) return false;  // busy
  quiet_motion_ = true;  // a person at the device: no RELEASE_* events, the game state stays
  return true;
}

bool DialController::requestGame(DialRequest r, uint32_t now) {
  if (state_ == DialState::Settings || !link_up_) return false;
  EventData e;
  e.code = static_cast<uint8_t>(EventCode::DialRequest);
  e.reason = static_cast<uint8_t>(r);
  return emit(e, now) != 0;  // 0: no session / outbox full
}

void DialController::tick(bool raw_switch_on, uint32_t now) {
  bool edge = receiver_.update(raw_switch_on, now);
  if (state_ == DialState::Settings) {
    if (edge) ++settings_switch_count_;
    if (dispenser_.tick(now) != Dispenser::Result::None) quiet_motion_ = false;  // test motions: no events
    return;
  }
  if (state_ == DialState::Armed) {
    if (wait_clear_ && !receiver_.stableOn()) wait_clear_ = false;
    if (edge && !wait_clear_) {
      state_ = DialState::BallDetected;
      detected_at_ = now;
      clearEffects();  // the local receive animation starts at once, whatever the PC was showing
      EventData e;
      e.code = static_cast<uint8_t>(EventCode::BallReceived);
      e.cycle = cycle_;
      emit(e, now);
    }
  }

  Dispenser::Result r = dispenser_.tick(now);
  if (quiet_motion_) {  // a menu release, or a settings test motion finishing after SETTINGS was left
    if (r != Dispenser::Result::None) {
      quiet_motion_ = false;
      // a game cycle (ARMED / BALL_DETECTED / COMPLETE) is not touched by a manual release;
      // after STOP_ALL (standby) the dial goes back to DISABLED
      if (op_mode_ != OperationMode::Game) toIdleState();
    }
    return;
  }
  for (auto& s : slots_)
    if (s.effect != DialEffect::None && !s.active(now)) s.clear();  // duration over
  if (dispenser_.phase() == Dispenser::Phase::Returning && state_ == DialState::Releasing)
    state_ = DialState::Returning;
  if (r != Dispenser::Result::None) clearEffects();  // the release animation ends with the sequence
  if (r == Dispenser::Result::Finished) {
    EventData e;
    e.code = static_cast<uint8_t>(EventCode::ReleaseDone);
    e.command_id = dispenser_.commandId();
    uint32_t el = now - dispenser_.startedAt();
    e.elapsed_ms = el > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(el);
    emit(e, now);
    if (op_mode_ == OperationMode::Game && state_ != DialState::Error)
      state_ = DialState::Complete;
    else
      toIdleState();
  } else if (r == Dispenser::Result::Aborted) {
    EventData e;
    e.code = static_cast<uint8_t>(EventCode::ReleaseFailed);
    e.command_id = dispenser_.commandId();
    e.reason = dispenser_.abortReason();
    emit(e, now);
    toIdleState();
  }
}

CommandOutcome DialController::onCommand(CommandCode code, const uint8_t* params, size_t len,
                                         uint32_t command_id, uint32_t now) {
  CommandOutcome o;
  const bool busy = dispenser_.busy();
  if (state_ == DialState::Settings && code != CommandCode::StopAll) {
    o.result = AckResult::RejectedState;  // someone is testing the hardware on the device
    return o;
  }
  switch (code) {
    case CommandCode::StopAll:
      if (state_ == DialState::Settings) {
        if (busy) dispenser_.abort(now, static_cast<uint8_t>(ReleaseFailReason::Aborted));
        exitSettings(now);
        return o;
      }
      op_mode_ = OperationMode::Standby;
      wait_clear_ = false;
      clearEffects();
      if (busy)
        dispenser_.abort(now, static_cast<uint8_t>(ReleaseFailReason::Aborted));  // back to standby
      else
        toIdleState();
      return o;

    case CommandCode::SetMode: {
      OperationMode m;
      if (!decodeSetMode(params, len, m)) {
        o.result = AckResult::InvalidParams;
        return o;
      }
      if (state_ == DialState::Error) {
        o.result = AckResult::RejectedState;
        return o;
      }
      if (m == OperationMode::Standby) {
        if (busy) {
          o.result = AckResult::Busy;
          return o;
        }
        op_mode_ = m;
        wait_clear_ = false;
        state_ = DialState::Disabled;
        return o;
      }
      op_mode_ = m;
      if (state_ == DialState::Disabled) state_ = DialState::Ready;
      return o;
    }

    case CommandCode::ArmReceiver:
      if (busy) {
        o.result = AckResult::Busy;
        return o;
      }
      if (op_mode_ != OperationMode::Game) {
        o.result = AckResult::RejectedState;
        return o;
      }
      if (state_ == DialState::Armed) return o;  // idempotent
      if (state_ != DialState::Ready && state_ != DialState::Complete) {
        o.result = AckResult::RejectedState;
        return o;
      }
      ++cycle_;
      wait_clear_ = receiver_.stableOn();  // switch held ON: wait for OFF before a new edge counts
      state_ = DialState::Armed;
      return o;

    case CommandCode::DisarmReceiver:
      if (busy) {
        o.result = AckResult::Busy;
        return o;
      }
      if (state_ == DialState::BallDetected) {
        o.result = AckResult::RejectedState;  // a ball is held: only RELEASE_BALL moves on
        return o;
      }
      if (state_ == DialState::Armed) state_ = DialState::Ready;
      wait_clear_ = false;
      return o;

    case CommandCode::ReleaseBall:
      if (!dispenser_.config().configured) {
        o.result = AckResult::NotConfigured;
        return o;
      }
      if (busy) {
        o.result = AckResult::Busy;
        return o;
      }
      // A release is for a ball this Dial holds: one just detected, or (0.4) one that is
      // still on the switch after a finished release (it did not leave: the PC may try
      // again, with a new command_id). Never with an empty Dial.
      if (op_mode_ != OperationMode::Game || !link_up_ ||
          !(state_ == DialState::BallDetected || (state_ == DialState::Complete && receiver_.stableOn()))) {
        o.result = AckResult::RejectedState;
        return o;
      }
      if (!dispenser_.start(command_id, now)) {
        o.result = AckResult::Busy;
        return o;
      }
      state_ = DialState::Releasing;
      release_at_ = now;
      ++releases_;
      last_release_cmd_ = command_id;
      {
        EventData e;
        e.code = static_cast<uint8_t>(EventCode::ReleaseStarted);
        e.command_id = command_id;
        emit(e, now);
      }
      return o;

    case CommandCode::SetDialEffect: return setDialEffect(params, len, command_id, now);

    case CommandCode::SetDialColor: {
      SetDialColorParams p;
      if (!decodeSetDialColor(params, len, p)) {
        o.result = AckResult::InvalidParams;
        return o;
      }
      if (p.r == 0 && p.g == 0 && p.b == 0) {
        resetUnitColor();
      } else {
        color_[0] = p.r;
        color_[1] = p.g;
        color_[2] = p.b;
      }
      return o;
    }

    default:
      o.result = AckResult::Unsupported;  // light / relay commands are for the Illumination ATOM
      return o;
  }
}

CommandOutcome DialController::setDialEffect(const uint8_t* params, size_t len, uint32_t command_id,
                                             uint32_t now) {
  CommandOutcome o;
  SetDialEffectParams p;
  if (!decodeSetDialEffect(params, len, p)) {
    o.result = AckResult::InvalidParams;
    return o;
  }
  if (state_ == DialState::Error) {
    o.result = AckResult::RejectedState;  // the error display is never covered
    return o;
  }
  uint8_t targets = p.target;
  if (!cfg_.transfer_led) targets &= static_cast<uint8_t>(~static_cast<uint8_t>(DialLedTarget::Transfer));
  if (targets == 0) {
    o.result = AckResult::NotConfigured;  // only the G15 LED was addressed and it is not configured
    return o;
  }
  // All addressed outputs or none: a higher priority effect on any of them wins.
  for (int i = 0; i < 2; ++i)
    if ((targets & (1 << i)) && slots_[i].active(now) && p.priority < slots_[i].priority) {
      o.result = AckResult::IgnoredPriority;
      return o;
    }
  for (int i = 0; i < 2; ++i) {
    if (!(targets & (1 << i))) continue;
    DialLedSlot& s = slots_[i];
    s.effect = static_cast<DialEffect>(p.effect);  // None = clear
    s.direction = static_cast<DialDirection>(p.direction);
    s.r = p.r;
    s.g = p.g;
    s.b = p.b;
    s.duration_ms = p.duration_ms;
    s.priority = p.priority;
    s.started_at = now;
    s.command_id = command_id;
  }
  return o;
}

LedPlan DialController::ledPlan(DialLedTarget target, uint32_t now) const {
  LedPlan plan;
  const bool ring = target != DialLedTarget::Transfer;
  if (!ring && !cfg_.transfer_led) return plan;  // Off
  if (state_ == DialState::Settings) {
    plan.scene = ring ? LedScene::Settings : LedScene::Off;
    return plan;
  }
  if (!link_up_) {
    plan.scene = ring ? LedScene::LinkLost : LedScene::Off;
    return plan;
  }
  if (state_ == DialState::Error) {
    plan.scene = ring ? LedScene::Error : LedScene::Off;
    return plan;
  }
  plan.r = color_[0];
  plan.g = color_[1];
  plan.b = color_[2];
  const uint32_t since_rx = now - detected_at_;
  if (state_ == DialState::BallDetected && since_rx < cfg_.rx_flash_ms) {
    plan.scene = LedScene::Effect;  // local receive flash: before anything the PC sends
    plan.effect = DialEffect::Solid;
    plan.r = plan.g = plan.b = 255;
    plan.elapsed_ms = since_rx;
    plan.duration_ms = cfg_.rx_flash_ms;
    return plan;
  }
  const DialLedSlot& s = slots_[ring ? 0 : 1];
  if (s.active(now)) {
    plan.scene = LedScene::Effect;
    plan.from_pc = true;
    plan.effect = s.effect;
    plan.direction = s.direction;
    if (s.r || s.g || s.b) {
      plan.r = s.r;
      plan.g = s.g;
      plan.b = s.b;
    }
    plan.elapsed_ms = now - s.started_at;
    plan.duration_ms = s.duration_ms;
    return plan;
  }
  switch (state_) {
    case DialState::BallDetected:
      plan.scene = LedScene::Effect;
      if (since_rx < static_cast<uint32_t>(cfg_.rx_flash_ms) + cfg_.rx_chase_ms) {
        plan.effect = DialEffect::Chase;  // capsule on its way to the screen
        plan.direction = DialDirection::ToScreen;
        plan.elapsed_ms = since_rx - cfg_.rx_flash_ms;
        plan.duration_ms = cfg_.rx_chase_ms;
      } else {
        plan.effect = DialEffect::Breathe;  // transfer shown: holding until RELEASE_BALL
        plan.elapsed_ms = since_rx;
      }
      break;
    case DialState::Releasing:
    case DialState::Returning:
      plan.scene = LedScene::Effect;
      plan.effect = DialEffect::Chase;  // capsule coming back from the screen
      plan.direction = DialDirection::ToDial;
      plan.elapsed_ms = now - release_at_;
      break;
    case DialState::Disabled: plan.scene = ring ? LedScene::Idle : LedScene::Off; break;
    case DialState::Ready: plan.scene = ring ? LedScene::Ready : LedScene::Off; break;
    case DialState::Armed: plan.scene = ring ? LedScene::Armed : LedScene::Off; break;
    case DialState::Complete: plan.scene = ring ? LedScene::Complete : LedScene::Off; break;
    default: break;
  }
  return plan;
}

void DialController::onSessionChanged(uint32_t /*session*/, uint32_t /*now*/) {
  // Old-session state never carries over: no game mode, receiver disarmed.
  // A running servo sequence is allowed to finish (it ends at the standby position).
  // SETTINGS stays (a person is at the device); the PC sees it and may STOP_ALL.
  clearEffects();   // effects and the unit colour belong to the session that set them
  resetUnitColor();
  if (state_ == DialState::Settings) return;
  op_mode_ = OperationMode::Standby;
  wait_clear_ = false;
  if (!dispenser_.busy()) toIdleState();
}

void DialController::onHubLinkChanged(bool up, uint32_t now) {
  link_up_ = up;
  if (!up) clearEffects();  // nobody can stop them without the link
  if (up || state_ == DialState::Settings) return;
  // No new release can start without the link (RELEASE_BALL is rejected), the
  // receiver is disarmed and the next ARM_RECEIVER is required after recovery.
  if (state_ == DialState::Armed) state_ = DialState::Ready;
  wait_clear_ = false;
  if (dispenser_.busy() && cfg_.abort_release_on_link_loss)
    dispenser_.abort(now, static_cast<uint8_t>(ReleaseFailReason::LinkLost));
}

uint8_t DialController::flags() const {
  uint8_t f = 0;
  if (dispenser_.busy()) f |= u8(NodeFlag::Busy);
  if (receiver_.stableOn()) f |= u8(NodeFlag::SwitchOn);
  if (op_mode_ == OperationMode::Game) f |= u8(NodeFlag::GameMode);
  if (error_ != ErrorCode::None) f |= u8(NodeFlag::ConfigError);
  return f;
}

void DialController::fillStatus(IlluminationStatus& /*il*/, DialStatus& dl) {
  dl.dial_state = static_cast<uint8_t>(state_);
  dl.cycle = cycle_;
  dl.switch_on = receiver_.stableOn() ? 1 : 0;
  dl.servo_deg = dispenser_.angle();
  dl.releases = releases_;
  dl.last_release_command = last_release_cmd_;
  dl.sim_button = cfg_.sim_button ? 1 : 0;
  // `now` is not available here: tick() clears finished effects every loop.
  dl.front_effect = static_cast<uint8_t>(slots_[0].effect);
  dl.transfer_effect = static_cast<uint8_t>(slots_[1].effect);
  dl.led_flags = cfg_.transfer_led ? u8(DialLedFlag::TransferFitted) : 0;
  dl.color_r = color_[0];
  dl.color_g = color_[1];
  dl.color_b = color_[2];
}

}  // namespace ufo
