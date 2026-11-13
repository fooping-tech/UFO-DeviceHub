// SG90 ball dispenser sequence (pure, non-blocking).
//   start(): servo -> release angle, hold hold_ms
//   then   : servo -> standby angle, wait return_settle_ms
//   then   : Done
// Angles and timings are configuration (config/hardware.h), never wire params.
#pragma once

#include <stdint.h>

namespace ufo {

class ServoOutput {
 public:
  virtual ~ServoOutput() {}
  virtual void writeAngle(uint8_t deg) = 0;
};

struct DispenserConfig {
  bool configured = true;
  uint8_t standby_deg = 180;
  uint8_t release_deg = 0;
  uint32_t hold_ms = 1500;
  uint32_t return_settle_ms = 600;
};

class Dispenser {
 public:
  enum class Phase : uint8_t { Idle, Releasing, Returning };
  enum class Result : uint8_t { None, Finished, Aborted };

  Dispenser(ServoOutput& servo, const DispenserConfig& cfg) : servo_(servo), cfg_(cfg) {}
  // Drives the servo to the standby position (boot / power-up safe position).
  void begin(uint32_t now);
  bool start(uint32_t command_id, uint32_t now);
  // Running sequence goes back to standby right away; tick() then reports Aborted.
  void abort(uint32_t now, uint8_t reason);
  // Advances the sequence; returns Finished/Aborted once when it ends.
  Result tick(uint32_t now);

  bool busy() const { return phase_ != Phase::Idle; }
  Phase phase() const { return phase_; }
  uint8_t angle() const { return angle_; }
  uint32_t commandId() const { return command_id_; }
  uint32_t startedAt() const { return started_at_; }
  uint8_t abortReason() const { return abort_reason_; }
  const DispenserConfig& config() const { return cfg_; }

 private:
  void write(uint8_t deg) {
    angle_ = deg;
    servo_.writeAngle(deg);
  }
  ServoOutput& servo_;
  DispenserConfig cfg_;
  Phase phase_ = Phase::Idle;
  uint8_t angle_ = 0;
  uint32_t phase_at_ = 0;
  uint32_t started_at_ = 0;
  uint32_t command_id_ = 0;
  bool aborted_ = false;
  uint8_t abort_reason_ = 0;
};

}  // namespace ufo
