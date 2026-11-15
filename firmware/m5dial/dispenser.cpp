#include "dispenser.h"

namespace ufo {

void Dispenser::begin(uint32_t now) {
  phase_ = Phase::Idle;
  phase_at_ = idle_at_ = now;
  arrived_ = true;
  if (cfg_.configured) {
    target_ = move_from_ = cfg_.standby_deg;
    write(cfg_.standby_deg);  // position unknown at power up: no ramp possible
  }
}

void Dispenser::moveTo(uint8_t deg, uint32_t now) {
  target_ = deg;
  move_from_ = angle_;
  move_at_ = now;
  arrived_ = false;
  if (cfg_.slew_deg_per_s == 0 || deg == angle_) {
    write(deg);
    arrived_ = true;
    phase_at_ = now;
  }
}

void Dispenser::stepMotion(uint32_t now) {
  if (angle_ == target_) return;
  uint32_t dist = move_from_ > target_ ? move_from_ - target_ : target_ - move_from_;
  uint32_t moved = static_cast<uint32_t>((uint64_t)(now - move_at_) * cfg_.slew_deg_per_s / 1000);
  if (moved > dist) moved = dist;
  uint8_t a = move_from_ > target_ ? static_cast<uint8_t>(move_from_ - moved) : static_cast<uint8_t>(move_from_ + moved);
  if (a != angle_ || !attached_) write(a);
}

bool Dispenser::start(uint32_t command_id, uint32_t now) {
  if (!cfg_.configured || phase_ != Phase::Idle) return false;
  command_id_ = command_id;
  started_at_ = now;
  aborted_ = false;
  abort_reason_ = 0;
  phase_ = Phase::Releasing;
  moveTo(cfg_.release_deg, now);
  return true;
}

void Dispenser::toReturning(uint32_t now) {
  phase_ = Phase::Returning;
  moveTo(cfg_.standby_deg, now);
}

void Dispenser::abort(uint32_t now, uint8_t reason) {
  if (phase_ == Phase::Idle) return;
  aborted_ = true;
  abort_reason_ = reason;
  if (phase_ == Phase::Releasing || phase_ == Phase::Manual) toReturning(now);
}

bool Dispenser::manualMove(uint8_t deg, uint32_t now) {
  if (!cfg_.configured || (phase_ != Phase::Idle && phase_ != Phase::Manual)) return false;
  if (phase_ == Phase::Idle) {
    command_id_ = 0;
    started_at_ = now;
    aborted_ = false;
    abort_reason_ = 0;
  }
  phase_ = Phase::Manual;
  if (deg > 180) deg = 180;
  stepMotion(now);  // continue from where the horn is now
  moveTo(deg, now);
  return true;
}

void Dispenser::endManual(uint32_t now) {
  if (phase_ == Phase::Manual) toReturning(now);
}

Dispenser::Result Dispenser::tick(uint32_t now) {
  if (!arrived_) {
    stepMotion(now);
    if (angle_ == target_) {
      arrived_ = true;
      phase_at_ = now;
    }
  }
  switch (phase_) {
    case Phase::Idle:
      if (cfg_.detach_after_ms && attached_ && now - idle_at_ >= cfg_.detach_after_ms) {
        attached_ = false;
        servo_.release();
      }
      return Result::None;
    case Phase::Manual: return Result::None;
    case Phase::Releasing:
      if (arrived_ && now - phase_at_ >= cfg_.hold_ms) toReturning(now);
      return Result::None;
    case Phase::Returning:
      if (arrived_ && now - phase_at_ >= cfg_.return_settle_ms) {
        phase_ = Phase::Idle;
        idle_at_ = now;
        return aborted_ ? Result::Aborted : Result::Finished;
      }
      return Result::None;
  }
  return Result::None;
}

}  // namespace ufo
