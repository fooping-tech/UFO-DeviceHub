// Ball receiver switch: debounce + OFF->ON edge detection (pure logic).
// The caller converts the GPIO level (and the optional simulation button) into
// "ball present" (raw_on) before calling update().
#pragma once

#include <stdint.h>

namespace ufo {

class BallReceiver {
 public:
  explicit BallReceiver(uint32_t debounce_ms = 30) : debounce_ms_(debounce_ms) {}
  // Takes the current level as the stable state without producing an edge.
  void reset(bool raw_on, uint32_t now);
  // Returns true exactly once per debounced OFF->ON transition. Holding the
  // switch ON never produces a second edge; it must go OFF (debounced) first.
  bool update(bool raw_on, uint32_t now);
  bool stableOn() const { return stable_; }
  uint32_t debounceMs() const { return debounce_ms_; }

 private:
  uint32_t debounce_ms_;
  bool stable_ = false;
  bool candidate_ = false;
  uint32_t changed_at_ = 0;
};

}  // namespace ufo
