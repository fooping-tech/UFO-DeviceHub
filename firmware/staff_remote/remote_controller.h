// Staff remote (M5StopWatch, Device ID 0x30): the logic, without hardware (host tested).
//
// The remote only reports; the game decides. Two kinds of events go to the PC:
//
//   STAFF_ACTION  one of the game's named staff operations (CoreXY_UFO_Game
//                 docs/staff_operations.md): NEXT / SKIP / MENU / RECOVER / SWITCH / STOP.
//                 Reliable (the hub ACKs, the PC de-duplicates), one event per press.
//   REMOTE_INPUT  a hand-like input stream: the move button held (like a pinch) + the
//                 tilt as a stick (-100..100), the Z switch (like a palm-down pinch) and a
//                 hand shape (fist / peace / point). Sent every frame_ms while anything is
//                 held or shown, then ONE closing frame (nothing held, open hand), then
//                 nothing. The game stops by itself when the frames stop coming (stale).
//
// Tilt = a relative stick: the orientation when the move button went down is the
// centre (like the pinch joystick's anchor). Nothing moves until it is pressed.
//
// Safety:
//   - latest wins: before a frame is queued, the older input frames still waiting for the
//     hub's ACK are dropped from the outbox, so a stalled link never delivers old tilt later
//     (behind other queued events a frame waits instead);
//   - STOP (the staff action), STOP_ALL from the hub, a session change or the hub link
//     lost end the stream, drop the queued input frames and latch the move button: it must
//     be let go and pressed again.
#pragma once

#include <stdint.h>

#include "node_core.h"

namespace ufo {

struct RemoteConfig {
  uint16_t frame_ms = 100;    // REMOTE_INPUT period while active (the game's stale time is ~350 ms)
  uint16_t pose_ms = 1200;    // a tapped hand shape is shown this long (the game needs ~0.6 s held)
  uint16_t neutral_ms = 400;  // then the open hand, so the next shape counts again
  float dead_deg = 4.0f;      // tilt dead zone
  float full_deg = 25.0f;     // tilt for full speed
  bool swap_xy = false;       // IMU axes -> the holder's right / forward
  bool invert_x = false;
  bool invert_y = false;
};

// One frame of the stream (what REMOTE_INPUT carries).
struct RemoteFrame {
  bool move = false;
  bool z = false;
  int8_t x = 0;  // + = right
  int8_t y = 0;  // + = forward (away from the holder)
  RemotePose pose = RemotePose::Open;
};

class RemoteController : public NodeApp {
 public:
  explicit RemoteController(const RemoteConfig& cfg) : cfg_(cfg) {}
  void setEmitter(EventEmitter* e) { emitter_ = e; }

  // Every loop: the move button level and the accelerometer (g, the IMU's own axes).
  void tick(bool move_held, float ax, float ay, float az, uint32_t now);

  // A staff operation. Returns the event id (0 = not sent: no session / link).
  // STOP also ends the stream and latches the move button.
  uint32_t staffAction(StaffAction a, uint32_t now);
  // Show a hand shape for pose_ms (then the open hand for neutral_ms).
  void showPose(RemotePose p, uint32_t now);
  void setZMode(bool on) { z_mode_ = on; }
  bool zMode() const { return z_mode_; }

  // For the screen
  bool streaming() const { return streaming_; }
  bool moveLatched() const { return move_latched_; }
  const RemoteFrame& frame() const { return cur_; }
  RemotePose shownPose(uint32_t now) const;
  uint16_t framesSent() const { return frame_no_; }
  uint32_t framesSkipped() const { return skipped_; }
  uint32_t framesDropped() const { return dropped_; }  // queued frames replaced / cancelled

  // Tilt (degrees from level, holder's axes) -> stick value (-100..100). Pure.
  static int8_t shape(float deg, float dead_deg, float full_deg);
  // Accelerometer -> (right, forward) tilt in degrees, with the axis mapping of cfg.
  static void tiltDeg(const RemoteConfig& cfg, float ax, float ay, float az, float& right, float& fwd);

  // NodeApp
  CommandOutcome onCommand(CommandCode code, const uint8_t* params, size_t len, uint32_t command_id,
                           uint32_t now) override;
  void onSessionChanged(uint32_t session, uint32_t now) override;
  void onHubLinkChanged(bool up, uint32_t now) override;
  uint8_t state() const override {
    return static_cast<uint8_t>(streaming_ ? RemoteState::Active : RemoteState::Idle);
  }
  uint8_t flags() const override { return streaming_ ? u8(NodeFlag::Busy) : 0; }

 private:
  void halt();  // end the stream now, latch the move button
  bool send(const RemoteFrame& f, uint32_t now);

  RemoteConfig cfg_;
  EventEmitter* emitter_ = nullptr;
  RemoteFrame cur_;
  bool z_mode_ = false;
  bool move_was_ = false;
  bool move_latched_ = false;
  float ref_right_ = 0.0f, ref_fwd_ = 0.0f;  // the tilt when the move button went down
  RemotePose pose_ = RemotePose::Open;
  bool pose_on_ = false;     // a shape (then the open hand) is being shown
  uint32_t pose_start_ = 0;
  bool streaming_ = false;
  bool closing_ = false;        // the closing frame still has to go out
  uint32_t last_sent_ = 0;
  bool sent_once_ = false;
  uint16_t frame_no_ = 0;
  uint32_t skipped_ = 0;
  uint32_t dropped_ = 0;
};

}  // namespace ufo
