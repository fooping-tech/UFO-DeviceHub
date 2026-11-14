// Illumination ATOM application (NodeApp): LED modes/effects (WS2811 + COB
// tape), the mist module, the optional steam relay and the local button.
// Pure logic; hardware is reached through RelayOutput / MistOutput and the LED
// renderer reads the LightingController / MistController state.
//
// Local mode (device button, no PC session):
//   short press  next scene (light mode + mist pattern, see kLocalScenes)
//   long press   mist ON/OFF for the current scene
// A long press also stops a running mist while the PC is in control (physical
// stop). A PC session start leaves local mode and goes to the safe state.
#pragma once

#include "lighting.h"
#include "mist.h"
#include "node_core.h"
#include "relay.h"

namespace ufo {

struct LocalScene {
  LightMode mode;
  MistPattern mist;
  const char* name;
};

struct LocalConfig {
  bool enabled = true;      // button scenes allowed when no PC session
  bool mist = true;         // scenes may start the mist
  uint8_t brightness = 160; // LED brightness of the local scenes
};

enum class ButtonResult : uint8_t { Ignored, SceneChanged, MistOn, MistOff };

class IlluminationController : public NodeApp, public RelayListener, public MistListener {
 public:
  static const LocalScene kLocalScenes[];
  static const uint8_t kLocalSceneCount;

  IlluminationController(RelayController& relay, MistController& mist, LightMode session_end_mode)
      : relay_(relay), mist_(mist), session_end_mode_(session_end_mode) {}
  void setEmitter(EventEmitter* e) { emitter_ = e; }
  void setLocalConfig(const LocalConfig& c) { local_cfg_ = c; }
  void begin(uint32_t now);
  void tick(uint32_t now);

  // Device button
  ButtonResult onButtonShort(uint32_t now);
  ButtonResult onButtonLong(uint32_t now);

  // NodeApp
  CommandOutcome onCommand(CommandCode code, const uint8_t* params, size_t len, uint32_t command_id,
                           uint32_t now) override;
  void onSessionChanged(uint32_t session, uint32_t now) override;
  void onHubLinkChanged(bool up, uint32_t now) override;
  uint8_t state() const override;
  uint8_t flags() const override;
  uint8_t opMode() const override { return static_cast<uint8_t>(op_mode_); }
  void fillStatus(IlluminationStatus& il, DialStatus& dl) override;

  // RelayListener / MistListener
  void onRelayDone(uint8_t id, uint32_t on_ms, RelayEndReason reason, uint32_t now) override;
  void onMistDone(uint32_t on_ms, RelayEndReason reason, uint32_t now) override;

  LightingController& lighting() { return lighting_; }
  const LightingController& lighting() const { return lighting_; }
  const MistController& mist() const { return mist_; }
  bool hubLinkUp() const { return link_up_; }
  bool localMode() const { return local_; }
  uint8_t localScene() const { return scene_; }

 private:
  void goSafe(uint32_t now, bool reset_light);
  void applyScene(uint32_t now);

  RelayController& relay_;
  MistController& mist_;
  LightingController lighting_;
  EventEmitter* emitter_ = nullptr;
  LightMode session_end_mode_;
  LocalConfig local_cfg_;
  OperationMode op_mode_ = OperationMode::Standby;
  bool link_up_ = false;
  bool session_ = false;
  bool local_ = false;       // scenes driven by the button
  bool local_mist_ = true;   // long press toggles
  uint8_t scene_ = 0;
  uint32_t now_ = 0;
};

}  // namespace ufo
