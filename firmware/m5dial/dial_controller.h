// M5Dial application (NodeApp): ball receiver + SG90 dispenser state machine.
//
//   DISABLED --SET_MODE GAME--> READY --ARM_RECEIVER--> ARMED
//   ARMED --switch OFF->ON--> BALL_DETECTED (BALL_RECEIVED event, once per cycle)
//   BALL_DETECTED --RELEASE_BALL--> RELEASING --hold--> RETURNING --settle--> COMPLETE
//   COMPLETE --ARM_RECEIVER--> ARMED ...
//   SET_MODE STANDBY / STOP_ALL / session change --> DISABLED (after any running
//   servo sequence has returned to standby)
//
// The device never decides game outcomes: it only reports and executes.
#pragma once

#include "ball_receiver.h"
#include "dispenser.h"
#include "node_core.h"

namespace ufo {

struct DialConfig {
  bool abort_release_on_link_loss = false;
  bool sim_button = false;
};

class DialController : public NodeApp {
 public:
  DialController(Dispenser& dispenser, BallReceiver& receiver, const DialConfig& cfg)
      : dispenser_(dispenser), receiver_(receiver), cfg_(cfg) {}
  void setEmitter(EventEmitter* e) { emitter_ = e; }
  void begin(bool raw_switch_on, uint32_t now);
  // Call every loop with the current "ball present" level.
  void tick(bool raw_switch_on, uint32_t now);
  void setConfigError(ErrorCode e) {
    error_ = e;
    state_ = DialState::Error;
  }

  // NodeApp
  CommandOutcome onCommand(CommandCode code, const uint8_t* params, size_t len, uint32_t command_id,
                           uint32_t now) override;
  void onSessionChanged(uint32_t session, uint32_t now) override;
  void onHubLinkChanged(bool up, uint32_t now) override;
  uint8_t state() const override { return static_cast<uint8_t>(state_); }
  uint8_t flags() const override;
  uint8_t opMode() const override { return static_cast<uint8_t>(op_mode_); }
  uint8_t errorCode() const override { return static_cast<uint8_t>(error_); }
  void fillStatus(IlluminationStatus& il, DialStatus& dl) override;

  DialState dialState() const { return state_; }
  OperationMode operationMode() const { return op_mode_; }
  uint16_t cycle() const { return cycle_; }
  bool hubLinkUp() const { return link_up_; }
  bool waitingForSwitchClear() const { return wait_clear_; }

 private:
  void toIdleState();
  void emit(EventData& e, uint32_t now);

  Dispenser& dispenser_;
  BallReceiver& receiver_;
  DialConfig cfg_;
  EventEmitter* emitter_ = nullptr;
  DialState state_ = DialState::Disabled;
  OperationMode op_mode_ = OperationMode::Standby;
  ErrorCode error_ = ErrorCode::None;
  uint16_t cycle_ = 0;
  uint16_t releases_ = 0;
  uint32_t last_release_cmd_ = 0;
  bool wait_clear_ = false;  // armed while the switch was already ON
  bool link_up_ = false;
};

}  // namespace ufo
