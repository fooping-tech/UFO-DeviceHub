// SG90 ball dispenser sequence (pure, non-blocking).
//   start(): servo -> release angle, hold hold_ms (counted from arrival)
//   then   : servo -> standby angle, wait return_settle_ms (from arrival)
//   then   : Done
// Angles and timings are configuration (config/hardware.h), never wire params.
//
// Power (the SG90 stall/inrush current can brown out the M5Dial):
//   slew_deg_per_s   moves in small steps instead of one 180 deg jump, which
//                    keeps the motor current far below its stall peak
//   detach_after_ms  stops the servo pulses once the horn rests at standby
//                    (no holding current, no jitter); the next move re-attaches
// Settings mode can also drive the servo by hand (manualMove / endManual).
#pragma once

#include <stdint.h>

namespace ufo {

class ServoOutput {
 public:
  virtual ~ServoOutput() {}
  virtual void writeAngle(uint8_t deg) = 0;  // (re)starts the pulses if released
  virtual void release() {}                  // stop the pulses (servo goes limp)
};

struct DispenserConfig {
  bool configured = true;
  uint8_t standby_deg = 180;
  uint8_t release_deg = 0;
  uint32_t hold_ms = 1500;
  uint32_t return_settle_ms = 600;
  uint16_t slew_deg_per_s = 0;   // 0 = jump to the target at full servo speed
  uint32_t detach_after_ms = 0;  // 0 = keep driving the standby position
};

class Dispenser {
 public:
  enum class Phase : uint8_t { Idle, Releasing, Returning, Manual };
  enum class Result : uint8_t { None, Finished, Aborted };

  Dispenser(ServoOutput& servo, const DispenserConfig& cfg) : servo_(servo), cfg_(cfg) {}
  // Drives the servo to the standby position (boot / power-up safe position).
  void begin(uint32_t now);
  bool start(uint32_t command_id, uint32_t now);
  // Running sequence goes back to standby right away; tick() then reports Aborted.
  void abort(uint32_t now, uint8_t reason);
  // Settings mode: move to any angle (only when idle or already manual).
  bool manualMove(uint8_t deg, uint32_t now);
  // Settings mode: back to standby; tick() then reports Finished.
  void endManual(uint32_t now);
  // Advances the motion and the sequence; returns Finished/Aborted once when it ends.
  Result tick(uint32_t now);

  bool busy() const { return phase_ != Phase::Idle; }
  bool moving() const { return angle_ != target_; }
  bool attached() const { return attached_; }
  Phase phase() const { return phase_; }
  uint8_t angle() const { return angle_; }
  uint8_t target() const { return target_; }
  uint32_t commandId() const { return command_id_; }
  uint32_t startedAt() const { return started_at_; }
  uint8_t abortReason() const { return abort_reason_; }
  const DispenserConfig& config() const { return cfg_; }

 private:
  void write(uint8_t deg) {
    angle_ = deg;
    attached_ = true;
    servo_.writeAngle(deg);
  }
  void moveTo(uint8_t deg, uint32_t now);
  void stepMotion(uint32_t now);
  void toReturning(uint32_t now);

  ServoOutput& servo_;
  DispenserConfig cfg_;
  Phase phase_ = Phase::Idle;
  uint8_t angle_ = 0;
  uint8_t target_ = 0;
  uint8_t move_from_ = 0;
  uint32_t move_at_ = 0;
  bool arrived_ = true;      // the current phase's target has been reached
  bool attached_ = false;
  uint32_t phase_at_ = 0;    // arrival time (hold / settle are counted from here)
  uint32_t idle_at_ = 0;
  uint32_t started_at_ = 0;
  uint32_t command_id_ = 0;
  bool aborted_ = false;
  uint8_t abort_reason_ = 0;
};

}  // namespace ufo
