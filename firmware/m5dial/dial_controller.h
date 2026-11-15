// M5Dial application (NodeApp): ball receiver + SG90 dispenser state machine.
//
//   DISABLED --SET_MODE GAME--> READY --ARM_RECEIVER--> ARMED
//   ARMED --switch OFF->ON--> BALL_DETECTED (BALL_RECEIVED event, once per cycle)
//   BALL_DETECTED --RELEASE_BALL--> RELEASING --hold--> RETURNING --settle--> COMPLETE
//   COMPLETE --ARM_RECEIVER--> ARMED ...
//   SET_MODE STANDBY / STOP_ALL / session change --> DISABLED (after any running
//   servo sequence has returned to standby)
//
// SETTINGS (hardware test, entered on the device by a long button press while
// nothing is running): servo / LED / switch tests. No events are sent, every
// PC command except STOP_ALL is RejectedState; STOP_ALL leaves SETTINGS.
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

  // Settings mode (device button)
  bool enterSettings(uint32_t now);  // false while a game cycle / servo motion is running
  void exitSettings(uint32_t now);   // servo back to standby, state DISABLED (or ERROR)
  bool inSettings() const { return state_ == DialState::Settings; }
  bool settingsReleaseTest(uint32_t now);           // full release sequence, no events
  bool settingsServoAngle(uint8_t deg, uint32_t now);  // hold the horn at an angle
  void settingsServoHome(uint32_t now);             // manual angle -> standby
  uint16_t settingsSwitchCount() const { return settings_switch_count_; }
  void resetSettingsSwitchCount() { settings_switch_count_ = 0; }

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
  uint16_t settings_switch_count_ = 0;
  bool quiet_motion_ = false;  // the running servo motion is a settings test: no events
};

}  // namespace ufo
