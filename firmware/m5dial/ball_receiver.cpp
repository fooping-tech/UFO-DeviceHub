#include "ball_receiver.h"

namespace ufo {

void BallReceiver::reset(bool raw_on, uint32_t now) {
  stable_ = raw_on;
  candidate_ = raw_on;
  changed_at_ = now;
}

bool BallReceiver::update(bool raw_on, uint32_t now) {
  if (raw_on != candidate_) {
    candidate_ = raw_on;
    changed_at_ = now;
    return false;
  }
  if (candidate_ != stable_ && now - changed_at_ >= debounce_ms_) {
    stable_ = candidate_;
    return stable_;
  }
  return false;
}

}  // namespace ufo
