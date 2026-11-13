#include "dispenser.h"

namespace ufo {

void Dispenser::begin(uint32_t now) {
  phase_ = Phase::Idle;
  phase_at_ = now;
  if (cfg_.configured) write(cfg_.standby_deg);
}

bool Dispenser::start(uint32_t command_id, uint32_t now) {
  if (!cfg_.configured || phase_ != Phase::Idle) return false;
  command_id_ = command_id;
  started_at_ = now;
  phase_at_ = now;
  aborted_ = false;
  abort_reason_ = 0;
  phase_ = Phase::Releasing;
  write(cfg_.release_deg);
  return true;
}

void Dispenser::abort(uint32_t now, uint8_t reason) {
  if (phase_ == Phase::Idle) return;
  aborted_ = true;
  abort_reason_ = reason;
  if (phase_ == Phase::Releasing) {
    phase_ = Phase::Returning;
    phase_at_ = now;
    write(cfg_.standby_deg);
  }
}

Dispenser::Result Dispenser::tick(uint32_t now) {
  switch (phase_) {
    case Phase::Idle: return Result::None;
    case Phase::Releasing:
      if (now - phase_at_ >= cfg_.hold_ms) {
        phase_ = Phase::Returning;
        phase_at_ = now;
        write(cfg_.standby_deg);
      }
      return Result::None;
    case Phase::Returning:
      if (now - phase_at_ >= cfg_.return_settle_ms) {
        phase_ = Phase::Idle;
        phase_at_ = now;
        return aborted_ ? Result::Aborted : Result::Finished;
      }
      return Result::None;
  }
  return Result::None;
}

}  // namespace ufo
