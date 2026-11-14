#include "mist.h"

namespace ufo {

void MistController::begin() {
  pattern_ = MistPattern::Off;
  output_ = false;
  on_ms_ = 0;
  if (configured_) out_.setMist(false);
}

bool MistController::patternOn(MistPattern p, uint32_t elapsed_ms) {
  switch (p) {
    case MistPattern::Continuous: return true;
    case MistPattern::Breath: return (elapsed_ms % kBreathPeriodMs) < kBreathOnMs;
    case MistPattern::Puff: return (elapsed_ms % kPuffPeriodMs) < kPuffOnMs;
    default: return false;
  }
}

CommandOutcome MistController::start(MistPattern p, uint16_t duration_s, uint32_t now) {
  CommandOutcome o;
  if (static_cast<uint8_t>(p) > static_cast<uint8_t>(MistPattern::Puff)) {
    o.result = AckResult::InvalidParams;
    return o;
  }
  if (!configured_) {
    o.result = AckResult::NotConfigured;
    return o;
  }
  if (p == MistPattern::Off) {
    stop(now, RelayEndReason::OffCommand);
    return o;
  }
  uint32_t run = duration_s == 0 ? max_run_ms_ : static_cast<uint32_t>(duration_s) * 1000u;
  if (run > max_run_ms_) {
    run = max_run_ms_;
    o.flags |= u8(AckFlag::Clamped);
  }
  // A new run replaces the running one silently (scene change, no MIST_DONE).
  accumulate(now);
  pattern_ = p;
  started_at_ = now;
  run_ms_ = run;
  on_ms_ = 0;
  setOutput(patternOn(p, 0), now);
  return o;
}

void MistController::stop(uint32_t now, RelayEndReason reason) {
  if (configured_) out_.setMist(false);  // always re-assert the OFF level
  if (!active()) return;
  setOutput(false, now);
  pattern_ = MistPattern::Off;
  if (listener_) listener_->onMistDone(on_ms_, reason, now);
}

void MistController::tick(uint32_t now) {
  if (!active()) return;
  uint32_t el = now - started_at_;
  if (el >= run_ms_) {
    stop(now, RelayEndReason::Completed);
    return;
  }
  setOutput(patternOn(pattern_, el), now);
}

void MistController::accumulate(uint32_t now) {
  if (output_) {
    on_ms_ += now - on_since_;
    on_since_ = now;
  }
}

void MistController::setOutput(bool on, uint32_t now) {
  if (on == output_) return;
  if (on) {
    on_since_ = now;
  } else {
    on_ms_ += now - on_since_;
  }
  output_ = on;
  out_.setMist(on);
}

uint16_t MistController::remainingS(uint32_t now) const {
  if (!active()) return 0;
  uint32_t el = now - started_at_;
  uint32_t rem = el >= run_ms_ ? 0 : run_ms_ - el;
  rem = (rem + 999) / 1000;
  return rem > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(rem);
}

uint16_t MistController::onS(uint32_t now) const {
  uint32_t s = (on_ms_ + (output_ ? now - on_since_ : 0)) / 1000;
  return s > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(s);
}

}  // namespace ufo
