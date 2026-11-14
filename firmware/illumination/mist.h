// Mist module control (pure logic; GPIO access through MistOutput).
//
// The mist module (ultrasonic humidifier on Unit 2Relay) runs a timed pattern
// instead of a single pulse:
//   CONTINUOUS  always ON
//   BREATH      8 s ON / 6 s OFF
//   PUFF        3 s ON / 9 s OFF
//
// Safety rules (docs/hardware.md):
//   * OFF at boot
//   * one run lasts at most max_run_ms (wall clock); longer requests are
//     clamped (ACK flag CLAMPED), the run then stops by itself (MIST_DONE COMPLETED)
//   * OFF on SET_MIST OFF / STOP_ALL / session change / hub link loss
//     (the controller decides; see IlluminationController)
//   * a new run replaces the running one (scene changes)
// The steam relay (relay.*) is a separate output with its own short-pulse rules.
#pragma once

#include <stdint.h>

#include "messages.h"
#include "node_core.h"

namespace ufo {

class MistOutput {
 public:
  virtual ~MistOutput() {}
  virtual void setMist(bool on) = 0;
};

class MistListener {
 public:
  virtual ~MistListener() {}
  virtual void onMistDone(uint32_t on_ms, RelayEndReason reason, uint32_t now) = 0;
};

class MistController {
 public:
  static constexpr uint32_t kBreathPeriodMs = 14000, kBreathOnMs = 8000;
  static constexpr uint32_t kPuffPeriodMs = 12000, kPuffOnMs = 3000;

  MistController(MistOutput& out, bool configured, uint32_t max_run_ms)
      : out_(out), configured_(configured), max_run_ms_(max_run_ms) {}
  void setListener(MistListener* l) { listener_ = l; }
  void begin();  // forces the output OFF

  // Pattern OFF stops a running mist (reason OFF_COMMAND). duration_s 0 = maximum.
  CommandOutcome start(MistPattern p, uint16_t duration_s, uint32_t now);
  void stop(uint32_t now, RelayEndReason reason);
  void tick(uint32_t now);

  // Whether the pattern wants mist `elapsed_ms` after the run started.
  static bool patternOn(MistPattern p, uint32_t elapsed_ms);

  bool configured() const { return configured_; }
  bool active() const { return pattern_ != MistPattern::Off; }
  bool outputOn() const { return output_; }
  MistPattern pattern() const { return pattern_; }
  uint16_t remainingS(uint32_t now) const;
  uint16_t onS(uint32_t now) const;  // ON time of the running / last run
  uint32_t maxRunMs() const { return max_run_ms_; }

 private:
  void setOutput(bool on, uint32_t now);
  void accumulate(uint32_t now);

  MistOutput& out_;
  MistListener* listener_ = nullptr;
  bool configured_;
  uint32_t max_run_ms_;
  MistPattern pattern_ = MistPattern::Off;
  uint32_t started_at_ = 0;
  uint32_t run_ms_ = 0;
  bool output_ = false;
  uint32_t on_since_ = 0;
  uint32_t on_ms_ = 0;  // finished ON intervals of the current run
};

}  // namespace ufo
