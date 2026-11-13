// Illumination ATOM application (NodeApp): LED modes/effects + steam relay.
// Pure logic; hardware is reached through RelayOutput and the LED renderer
// reads the LightingController state.
#pragma once

#include "lighting.h"
#include "node_core.h"
#include "relay.h"

namespace ufo {

class IlluminationController : public NodeApp, public RelayListener {
 public:
  IlluminationController(RelayController& relay, LightMode session_end_mode)
      : relay_(relay), session_end_mode_(session_end_mode) {}
  void setEmitter(EventEmitter* e) { emitter_ = e; }
  void begin(uint32_t now);
  void tick(uint32_t now);

  // NodeApp
  CommandOutcome onCommand(CommandCode code, const uint8_t* params, size_t len, uint32_t command_id,
                           uint32_t now) override;
  void onSessionChanged(uint32_t session, uint32_t now) override;
  void onHubLinkChanged(bool up, uint32_t now) override;
  uint8_t state() const override;
  uint8_t flags() const override;
  uint8_t opMode() const override { return static_cast<uint8_t>(op_mode_); }
  void fillStatus(IlluminationStatus& il, DialStatus& dl) override;

  // RelayListener
  void onRelayDone(uint8_t id, uint32_t on_ms, RelayEndReason reason, uint32_t now) override;

  LightingController& lighting() { return lighting_; }
  const LightingController& lighting() const { return lighting_; }
  bool hubLinkUp() const { return link_up_; }

 private:
  void goSafe(uint32_t now, bool reset_light);

  RelayController& relay_;
  LightingController lighting_;
  EventEmitter* emitter_ = nullptr;
  LightMode session_end_mode_;
  OperationMode op_mode_ = OperationMode::Standby;
  bool link_up_ = false;
  uint32_t now_ = 0;
};

}  // namespace ufo
