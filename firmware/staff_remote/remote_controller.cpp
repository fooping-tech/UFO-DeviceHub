#include "remote_controller.h"

#include <math.h>

namespace ufo {

namespace {
constexpr float kRadToDeg = 57.2957795f;
float clamp1(float v) { return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v); }
}  // namespace

int8_t RemoteController::shape(float deg, float dead_deg, float full_deg) {
  float a = fabsf(deg);
  if (!(a > dead_deg)) return 0;  // also NaN
  float span = full_deg - dead_deg;
  float t = span > 0.0f ? (a - dead_deg) / span : 1.0f;
  if (t > 1.0f) t = 1.0f;
  int v = static_cast<int>(t * 100.0f + 0.5f);
  if (v < 1) v = 1;  // out of the dead zone: never 0
  return static_cast<int8_t>(deg < 0.0f ? -v : v);
}

void RemoteController::tiltDeg(const RemoteConfig& cfg, float ax, float ay, float az, float& right, float& fwd) {
  right = 0.0f;
  fwd = 0.0f;
  float n = sqrtf(ax * ax + ay * ay + az * az);
  if (!(n > 0.3f)) return;  // free fall / no IMU: no tilt
  // At rest the accelerometer reads "up". The right edge tipped down by a: x reads -sin(a).
  float sx = -ax / n, sy = -ay / n;
  if (cfg.swap_xy) {
    float t = sx;
    sx = sy;
    sy = t;
  }
  if (cfg.invert_x) sx = -sx;
  if (cfg.invert_y) sy = -sy;
  right = asinf(clamp1(sx)) * kRadToDeg;
  fwd = asinf(clamp1(sy)) * kRadToDeg;
}

RemotePose RemoteController::shownPose(uint32_t now) const {
  (void)now;
  return cur_.pose;
}

void RemoteController::tick(bool move_held, float ax, float ay, float az, uint32_t now) {
  float right = 0.0f, fwd = 0.0f;
  tiltDeg(cfg_, ax, ay, az, right, fwd);
  if (!move_held) move_latched_ = false;
  const bool move = move_held && !move_latched_;
  if (move && !move_was_) {
    ref_right_ = right;  // the stick's centre: where the button went down
    ref_fwd_ = fwd;
  }
  move_was_ = move;

  RemoteFrame f;
  f.move = move;
  f.z = move && z_mode_;
  if (move) {
    f.x = shape(right - ref_right_, cfg_.dead_deg, cfg_.full_deg);
    f.y = shape(fwd - ref_fwd_, cfg_.dead_deg, cfg_.full_deg);
  }
  bool posing = false, neutral = false;
  if (pose_on_) {
    uint32_t t = now - pose_start_;
    posing = t < cfg_.pose_ms;
    neutral = !posing && t < static_cast<uint32_t>(cfg_.pose_ms) + cfg_.neutral_ms;
    if (!posing && !neutral) pose_on_ = false;
  }
  f.pose = posing ? pose_ : RemotePose::Open;
  cur_ = f;

  if (move || posing || neutral) {
    if (!streaming_) {
      streaming_ = true;
      sent_once_ = false;
    }
    closing_ = true;  // when it ends: one frame with nothing held
    if (!sent_once_ || now - last_sent_ >= cfg_.frame_ms) {
      last_sent_ = now;
      sent_once_ = true;
      send(f, now);
    }
  } else {
    streaming_ = false;
    if (closing_ && send(RemoteFrame(), now)) closing_ = false;  // retried every loop until it is out
  }
}

bool RemoteController::send(const RemoteFrame& f, uint32_t now) {
  if (!emitter_) return false;
  // latest wins: an older frame still waiting for the hub's ACK is dropped, never sent late
  dropped_ += emitter_->dropPending(EventCode::RemoteInput);
  if (emitter_->pending() >= 4) {  // the link is backed up behind other events: wait
    ++skipped_;
    return false;
  }
  EventData e;
  e.code = static_cast<uint8_t>(EventCode::RemoteInput);
  e.buttons = (f.move ? u8(RemoteButton::Move) : 0) | (f.z ? u8(RemoteButton::AxisZ) : 0);
  e.tilt_x = f.x;
  e.tilt_y = f.y;
  e.pose = static_cast<uint8_t>(f.pose);
  e.frame = static_cast<uint16_t>(frame_no_ + 1);
  if (emitter_->emit(e, now) == 0) return false;
  ++frame_no_;
  return true;
}

uint32_t RemoteController::staffAction(StaffAction a, uint32_t now) {
  if (a == StaffAction::Stop) halt();
  if (!emitter_) return 0;
  EventData e;
  e.code = static_cast<uint8_t>(EventCode::StaffAction);
  e.reason = static_cast<uint8_t>(a);
  return emitter_->emit(e, now);
}

void RemoteController::showPose(RemotePose p, uint32_t now) {
  pose_ = p;
  pose_on_ = p != RemotePose::Open;
  pose_start_ = now;
}

void RemoteController::halt() {
  // the input frames still queued go too: none may arrive after a stop (review on #14)
  if (emitter_) dropped_ += emitter_->dropPending(EventCode::RemoteInput);
  streaming_ = false;
  closing_ = false;
  pose_on_ = false;
  move_latched_ = true;  // cleared once the move button is let go
  move_was_ = false;
  cur_ = RemoteFrame();
}

CommandOutcome RemoteController::onCommand(CommandCode code, const uint8_t* /*params*/, size_t /*len*/,
                                           uint32_t /*command_id*/, uint32_t /*now*/) {
  CommandOutcome out;
  if (code == CommandCode::StopAll) {
    halt();
    return out;
  }
  out.result = AckResult::Unsupported;
  return out;
}

void RemoteController::onSessionChanged(uint32_t /*session*/, uint32_t /*now*/) { halt(); }

void RemoteController::onHubLinkChanged(bool up, uint32_t /*now*/) {
  if (!up) halt();
}

}  // namespace ufo
