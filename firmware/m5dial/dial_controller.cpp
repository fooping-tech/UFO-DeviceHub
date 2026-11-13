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
  if (state_ == DialState::Error) return;
  state_ = op_mode_ == OperationMode::Game ? DialState::Ready : DialState::Disabled;
}

void DialController::tick(bool raw_switch_on, uint32_t now) {
  bool edge = receiver_.update(raw_switch_on, now);
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
  switch (code) {
    case CommandCode::StopAll:
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
  op_mode_ = OperationMode::Standby;
  wait_clear_ = false;
  if (!dispenser_.busy()) toIdleState();
}

void DialController::onHubLinkChanged(bool up, uint32_t now) {
  link_up_ = up;
  if (up) return;
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
