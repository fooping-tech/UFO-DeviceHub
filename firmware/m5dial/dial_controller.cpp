#include "dial_controller.h"

namespace ufo {

void DialController::begin(bool raw_switch_on, uint32_t now) {
  receiver_.reset(raw_switch_on, now);
  dispenser_.begin(now);  // servo to standby
  if (!dispenser_.config().configured) setConfigError(ErrorCode::ServoNotConfigured);
  if (state_ != DialState::Error) state_ = DialState::Disabled;
  op_mode_ = OperationMode::Standby;
}

void DialController::emit(EventData& e, uint32_t now) {
  if (emitter_) emitter_->emit(e, now);
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
      EventData e;
      e.code = static_cast<uint8_t>(EventCode::BallReceived);
      e.cycle = cycle_;
      emit(e, now);
    }
  }

  Dispenser::Result r = dispenser_.tick(now);
  if (quiet_motion_) {  // a settings test motion still finishing after SETTINGS was left
    if (r != Dispenser::Result::None) {
      quiet_motion_ = false;
      toIdleState();
    }
    return;
  }
  if (dispenser_.phase() == Dispenser::Phase::Returning && state_ == DialState::Releasing)
    state_ = DialState::Returning;
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
      if (op_mode_ != OperationMode::Game || state_ != DialState::BallDetected || !link_up_) {
        o.result = AckResult::RejectedState;
        return o;
      }
      if (!dispenser_.start(command_id, now)) {
        o.result = AckResult::Busy;
        return o;
      }
      state_ = DialState::Releasing;
      ++releases_;
      last_release_cmd_ = command_id;
      {
        EventData e;
        e.code = static_cast<uint8_t>(EventCode::ReleaseStarted);
        e.command_id = command_id;
        emit(e, now);
      }
      return o;

    default:
      o.result = AckResult::Unsupported;  // light / relay commands are for the Illumination ATOM
      return o;
  }
}

void DialController::onSessionChanged(uint32_t /*session*/, uint32_t /*now*/) {
  // Old-session state never carries over: no game mode, receiver disarmed.
  // A running servo sequence is allowed to finish (it ends at the standby position).
  // SETTINGS stays (a person is at the device); the PC sees it and may STOP_ALL.
  if (state_ == DialState::Settings) return;
  op_mode_ = OperationMode::Standby;
  wait_clear_ = false;
  if (!dispenser_.busy()) toIdleState();
}

void DialController::onHubLinkChanged(bool up, uint32_t now) {
  link_up_ = up;
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
}

}  // namespace ufo
