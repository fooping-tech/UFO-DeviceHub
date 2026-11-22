#include "messages.h"

namespace ufo {

const char* commandName(uint8_t code) {
  switch (static_cast<CommandCode>(code)) {
    case CommandCode::SessionBegin: return "SESSION_BEGIN";
    case CommandCode::SessionEnd: return "SESSION_END";
    case CommandCode::GetStatus: return "GET_STATUS";
    case CommandCode::StopAll: return "STOP_ALL";
    case CommandCode::SetMode: return "SET_MODE";
    case CommandCode::SetLightMode: return "SET_LIGHT_MODE";
    case CommandCode::TriggerEffect: return "TRIGGER_EFFECT";
    case CommandCode::RelayPulse: return "RELAY_PULSE";
    case CommandCode::RelayOff: return "RELAY_OFF";
    case CommandCode::SetMist: return "SET_MIST";
    case CommandCode::ArmReceiver: return "ARM_RECEIVER";
    case CommandCode::DisarmReceiver: return "DISARM_RECEIVER";
    case CommandCode::ReleaseBall: return "RELEASE_BALL";
    case CommandCode::SetDialEffect: return "SET_DIAL_EFFECT";
    case CommandCode::SetDialColor: return "SET_DIAL_COLOR";
  }
  return nullptr;
}

const char* eventName(uint8_t code) {
  switch (static_cast<EventCode>(code)) {
    case EventCode::BallReceived: return "BALL_RECEIVED";
    case EventCode::ReleaseStarted: return "RELEASE_STARTED";
    case EventCode::ReleaseDone: return "RELEASE_DONE";
    case EventCode::ReleaseFailed: return "RELEASE_FAILED";
    case EventCode::DialRequest: return "DIAL_REQUEST";
    case EventCode::RelayDone: return "RELAY_DONE";
    case EventCode::MistDone: return "MIST_DONE";
    case EventCode::DeviceError: return "ERROR";
    case EventCode::StaffAction: return "STAFF_ACTION";
    case EventCode::RemoteInput: return "REMOTE_INPUT";
  }
  return nullptr;
}

const char* ackResultName(uint8_t code) {
  switch (static_cast<AckResult>(code)) {
    case AckResult::Accepted: return "ACCEPTED";
    case AckResult::Busy: return "BUSY";
    case AckResult::RejectedState: return "REJECTED_STATE";
    case AckResult::RejectedSession: return "REJECTED_SESSION";
    case AckResult::InvalidParams: return "INVALID_PARAMS";
    case AckResult::Unsupported: return "UNSUPPORTED";
    case AckResult::Cooldown: return "COOLDOWN";
    case AckResult::IgnoredPriority: return "IGNORED_PRIORITY";
    case AckResult::NotConfigured: return "NOT_CONFIGURED";
    case AckResult::Timeout: return "TIMEOUT";
    case AckResult::Offline: return "OFFLINE";
    case AckResult::QueueFull: return "QUEUE_FULL";
    case AckResult::UnknownDevice: return "UNKNOWN_DEVICE";
    case AckResult::NoSession: return "NO_SESSION";
    case AckResult::BadRequest: return "BAD_REQUEST";
    case AckResult::SessionMismatch: return "SESSION_MISMATCH";
    case AckResult::Superseded: return "SUPERSEDED";
  }
  return "UNKNOWN";
}

bool commandRequiresSession(uint8_t code) {
  switch (static_cast<CommandCode>(code)) {
    case CommandCode::SessionBegin:
    case CommandCode::SessionEnd:
    case CommandCode::GetStatus:
    case CommandCode::StopAll:
    case CommandCode::RelayOff:
    case CommandCode::DisarmReceiver:
      return false;
    default:
      return true;
  }
}

int commandParamLength(uint8_t code) {
  switch (static_cast<CommandCode>(code)) {
    case CommandCode::SessionBegin:
    case CommandCode::SessionEnd:
    case CommandCode::GetStatus:
    case CommandCode::StopAll:
    case CommandCode::ArmReceiver:
    case CommandCode::DisarmReceiver:
    case CommandCode::ReleaseBall:
      return 0;
    case CommandCode::SetMode: return 1;
    case CommandCode::SetLightMode: return 8;
    case CommandCode::TriggerEffect: return 4;
    case CommandCode::RelayPulse: return 3;
    case CommandCode::RelayOff: return 1;
    case CommandCode::SetMist: return 3;
    case CommandCode::SetDialEffect: return 9;
    case CommandCode::SetDialColor: return 3;
  }
  return -1;
}

size_t encodeAck(const AckPayload& a, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(a.acked_type).u8(a.result).u8(a.flags).u16(a.acked_seq).u8(a.detail);
  return w.ok() ? w.size() : 0;
}
bool decodeAck(const uint8_t* in, size_t len, AckPayload& a) {
  if (len != kAckPayloadSize) return false;
  Reader r(in, len);
  a.acked_type = r.u8();
  a.result = r.u8();
  a.flags = r.u8();
  a.acked_seq = r.u16();
  a.detail = r.u8();
  return r.ok() && r.atEnd();
}

size_t encodeCommand(CommandCode code, uint8_t* out, size_t cap) {
  if (commandParamLength(static_cast<uint8_t>(code)) != 0) return 0;
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(code));
  return w.ok() ? w.size() : 0;
}
size_t encodeSetMode(OperationMode m, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(CommandCode::SetMode)).u8(static_cast<uint8_t>(m));
  return w.ok() ? w.size() : 0;
}
size_t encodeSetLightMode(const SetLightModeParams& p, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(CommandCode::SetLightMode))
      .u8(p.mode)
      .u8(p.brightness)
      .u16(p.transition_ms)
      .u8(p.speed)
      .u8(p.r)
      .u8(p.g)
      .u8(p.b);
  return w.ok() ? w.size() : 0;
}
size_t encodeTriggerEffect(const TriggerEffectParams& p, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(CommandCode::TriggerEffect)).u8(p.effect).u16(p.duration_ms).u8(p.intensity);
  return w.ok() ? w.size() : 0;
}
size_t encodeRelayPulse(const RelayPulseParams& p, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(CommandCode::RelayPulse)).u8(p.relay_id).u16(p.duration_ms);
  return w.ok() ? w.size() : 0;
}
size_t encodeRelayOff(uint8_t relay_id, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(CommandCode::RelayOff)).u8(relay_id);
  return w.ok() ? w.size() : 0;
}

size_t encodeSetMist(const SetMistParams& p, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(CommandCode::SetMist)).u8(p.pattern).u16(p.duration_s);
  return w.ok() ? w.size() : 0;
}

size_t encodeSetDialEffect(const SetDialEffectParams& p, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(CommandCode::SetDialEffect))
      .u8(p.effect)
      .u8(p.direction)
      .u8(p.r)
      .u8(p.g)
      .u8(p.b)
      .u16(p.duration_ms)
      .u8(p.priority)
      .u8(p.target);
  return w.ok() ? w.size() : 0;
}
size_t encodeSetDialColor(const SetDialColorParams& p, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(static_cast<uint8_t>(CommandCode::SetDialColor)).u8(p.r).u8(p.g).u8(p.b);
  return w.ok() ? w.size() : 0;
}

bool dialEffectIsDirectional(uint8_t effect) {
  return effect == static_cast<uint8_t>(DialEffect::Chase) || effect == static_cast<uint8_t>(DialEffect::Comet) ||
         effect == static_cast<uint8_t>(DialEffect::Wipe);
}

bool decodeSetMode(const uint8_t* p, size_t n, OperationMode& m) {
  if (n != 1 || p[0] > static_cast<uint8_t>(OperationMode::Game)) return false;
  m = static_cast<OperationMode>(p[0]);
  return true;
}
bool decodeSetLightMode(const uint8_t* p, size_t n, SetLightModeParams& out) {
  if (n != 8) return false;
  Reader r(p, n);
  out.mode = r.u8();
  out.brightness = r.u8();
  out.transition_ms = r.u16();
  out.speed = r.u8();
  out.r = r.u8();
  out.g = r.u8();
  out.b = r.u8();
  if (!r.ok() || !r.atEnd()) return false;
  return out.mode <= static_cast<uint8_t>(LightMode::Night);
}
bool decodeTriggerEffect(const uint8_t* p, size_t n, TriggerEffectParams& out) {
  if (n != 4) return false;
  Reader r(p, n);
  out.effect = r.u8();
  out.duration_ms = r.u16();
  out.intensity = r.u8();
  if (!r.ok()) return false;
  return out.effect >= static_cast<uint8_t>(EffectId::GameStart) &&
         out.effect <= static_cast<uint8_t>(EffectId::GameOver);
}
bool decodeRelayPulse(const uint8_t* p, size_t n, RelayPulseParams& out) {
  if (n != 3) return false;
  Reader r(p, n);
  out.relay_id = r.u8();
  out.duration_ms = r.u16();
  return r.ok() && out.duration_ms > 0;
}
bool decodeRelayOff(const uint8_t* p, size_t n, uint8_t& relay_id) {
  if (n != 1) return false;
  relay_id = p[0];
  return true;
}

bool decodeSetMist(const uint8_t* p, size_t n, SetMistParams& out) {
  if (n != 3) return false;
  Reader r(p, n);
  out.pattern = r.u8();
  out.duration_s = r.u16();
  return r.ok() && out.pattern <= static_cast<uint8_t>(MistPattern::Puff);
}

bool decodeSetDialEffect(const uint8_t* p, size_t n, SetDialEffectParams& out) {
  if (n != 9) return false;
  Reader r(p, n);
  out.effect = r.u8();
  out.direction = r.u8();
  out.r = r.u8();
  out.g = r.u8();
  out.b = r.u8();
  out.duration_ms = r.u16();
  out.priority = r.u8();
  out.target = r.u8();
  if (!r.ok() || !r.atEnd()) return false;
  if (out.effect > static_cast<uint8_t>(DialEffect::Sparkle)) return false;
  if (out.direction > static_cast<uint8_t>(DialDirection::ToDial)) return false;
  if (out.priority > static_cast<uint8_t>(EffectPriority::High)) return false;
  if (out.target == 0 || out.target > static_cast<uint8_t>(DialLedTarget::Both)) return false;
  if (dialEffectIsDirectional(out.effect) && out.direction == static_cast<uint8_t>(DialDirection::None))
    return false;
  return true;
}
bool decodeSetDialColor(const uint8_t* p, size_t n, SetDialColorParams& out) {
  if (n != 3) return false;
  out.r = p[0];
  out.g = p[1];
  out.b = p[2];
  return true;
}

size_t encodeEvent(const EventData& e, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(e.code);
  switch (static_cast<EventCode>(e.code)) {
    case EventCode::BallReceived: w.u16(e.cycle); break;
    case EventCode::ReleaseStarted: w.u32(e.command_id); break;
    case EventCode::ReleaseDone: w.u32(e.command_id).u16(e.elapsed_ms); break;
    case EventCode::ReleaseFailed: w.u32(e.command_id).u8(e.reason); break;
    case EventCode::DialRequest: w.u8(e.reason); break;
    case EventCode::RelayDone: w.u8(e.relay_id).u16(e.elapsed_ms).u8(e.reason); break;
    case EventCode::MistDone: w.u16(e.on_s).u8(e.reason); break;
    case EventCode::DeviceError: w.u8(e.reason).u8(e.detail); break;
    case EventCode::StaffAction: w.u8(e.reason); break;
    case EventCode::RemoteInput:
      w.u8(e.buttons)
          .u8(static_cast<uint8_t>(e.tilt_x))
          .u8(static_cast<uint8_t>(e.tilt_y))
          .u8(e.pose)
          .u16(e.frame);
      break;
    default: return 0;
  }
  return w.ok() ? w.size() : 0;
}
const char* dialRequestName(uint8_t request) {
  switch (static_cast<DialRequest>(request)) {
    case DialRequest::GameRestart: return "GAME_RESTART";
    case DialRequest::GameEnd: return "GAME_END";
  }
  return nullptr;
}

const char* staffActionName(uint8_t action) {
  switch (static_cast<StaffAction>(action)) {
    case StaffAction::Next: return "NEXT";
    case StaffAction::Skip: return "SKIP";
    case StaffAction::Menu: return "MENU";
    case StaffAction::Recover: return "RECOVER";
    case StaffAction::Switch: return "SWITCH";
    case StaffAction::Stop: return "STOP";
  }
  return nullptr;
}

const char* remotePoseName(uint8_t pose) {
  switch (static_cast<RemotePose>(pose)) {
    case RemotePose::Open: return "OPEN";
    case RemotePose::Fist: return "FIST";
    case RemotePose::Peace: return "PEACE";
    case RemotePose::Point: return "POINT";
  }
  return nullptr;
}

bool decodeEvent(const uint8_t* in, size_t len, EventData& e) {
  Reader r(in, len);
  e = EventData();
  e.code = r.u8();
  switch (static_cast<EventCode>(e.code)) {
    case EventCode::BallReceived: e.cycle = r.u16(); break;
    case EventCode::ReleaseStarted: e.command_id = r.u32(); break;
    case EventCode::ReleaseDone:
      e.command_id = r.u32();
      e.elapsed_ms = r.u16();
      break;
    case EventCode::ReleaseFailed:
      e.command_id = r.u32();
      e.reason = r.u8();
      break;
    case EventCode::DialRequest: e.reason = r.u8(); break;
    case EventCode::RelayDone:
      e.relay_id = r.u8();
      e.elapsed_ms = r.u16();
      e.reason = r.u8();
      break;
    case EventCode::MistDone:
      e.on_s = r.u16();
      e.reason = r.u8();
      break;
    case EventCode::DeviceError:
      e.reason = r.u8();
      e.detail = r.u8();
      break;
    case EventCode::StaffAction: e.reason = r.u8(); break;
    case EventCode::RemoteInput:
      e.buttons = r.u8();
      e.tilt_x = static_cast<int8_t>(r.u8());
      e.tilt_y = static_cast<int8_t>(r.u8());
      e.pose = r.u8();
      e.frame = r.u16();
      break;
    default: return false;
  }
  return r.ok() && r.atEnd();
}

size_t encodeHubHeartbeat(const HubHeartbeat& h, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u32(h.uptime_ms).u8(h.flags);
  return w.ok() ? w.size() : 0;
}
bool decodeHubHeartbeat(const uint8_t* in, size_t len, HubHeartbeat& h) {
  if (len != kHubHeartbeatSize) return false;
  Reader r(in, len);
  h.uptime_ms = r.u32();
  h.flags = r.u8();
  return r.ok();
}

size_t encodeNodeHeartbeat(const NodeHeartbeat& h, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u16(h.echo_seq)
      .u32(h.boot_id)
      .u32(h.uptime_ms)
      .u8(h.device_type)
      .u8(h.state)
      .u8(h.flags)
      .i8(h.rssi)
      .u16(h.rx_rejected)
      .u8(h.fw_major)
      .u8(h.fw_minor);
  return w.ok() ? w.size() : 0;
}
bool decodeNodeHeartbeat(const uint8_t* in, size_t len, NodeHeartbeat& h) {
  if (len != kNodeHeartbeatSize) return false;
  Reader r(in, len);
  h.echo_seq = r.u16();
  h.boot_id = r.u32();
  h.uptime_ms = r.u32();
  h.device_type = r.u8();
  h.state = r.u8();
  h.flags = r.u8();
  h.rssi = r.i8();
  h.rx_rejected = r.u16();
  h.fw_major = r.u8();
  h.fw_minor = r.u8();
  return r.ok() && r.atEnd();
}

size_t encodeStatus(const StatusCommon& c, const IlluminationStatus* il, const DialStatus* dl,
                    uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(c.device_type)
      .u8(c.state)
      .u8(c.flags)
      .u8(c.error)
      .u8(c.op_mode)
      .u32(c.uptime_ms)
      .u32(c.boot_id)
      .u8(c.fw_major)
      .u8(c.fw_minor);
  if (c.device_type == static_cast<uint8_t>(DeviceType::Illumination)) {
    if (!il) return 0;
    w.u8(il->light_mode)
        .u8(il->brightness)
        .u8(il->active_effect)
        .u16(il->effect_remaining_ms)
        .u8(il->relay_on_mask)
        .u16(il->relay_remaining_ms)
        .u16(il->relay_cooldown_ms)
        .u16(il->relay_pulses)
        .u8(il->mist_pattern)
        .u8(il->mist_flags)
        .u16(il->mist_remaining_s)
        .u16(il->mist_on_s);
  } else if (c.device_type == static_cast<uint8_t>(DeviceType::Dial)) {
    if (!dl) return 0;
    w.u8(dl->dial_state)
        .u16(dl->cycle)
        .u8(dl->switch_on)
        .u8(dl->servo_deg)
        .u16(dl->releases)
        .u32(dl->last_release_command)
        .u8(dl->sim_button)
        .u8(dl->front_effect)
        .u8(dl->transfer_effect)
        .u8(dl->led_flags)
        .u8(dl->color_r)
        .u8(dl->color_g)
        .u8(dl->color_b);
  } else if (c.device_type != static_cast<uint8_t>(DeviceType::StaffRemote)) {
    return 0;  // the staff remote has the common part only
  }
  return w.ok() ? w.size() : 0;
}

bool decodeStatus(const uint8_t* in, size_t len, StatusCommon& c, IlluminationStatus& il, DialStatus& dl) {
  Reader r(in, len);
  c.device_type = r.u8();
  c.state = r.u8();
  c.flags = r.u8();
  c.error = r.u8();
  c.op_mode = r.u8();
  c.uptime_ms = r.u32();
  c.boot_id = r.u32();
  c.fw_major = r.u8();
  c.fw_minor = r.u8();
  if (!r.ok()) return false;
  if (c.device_type == static_cast<uint8_t>(DeviceType::Illumination)) {
    il.light_mode = r.u8();
    il.brightness = r.u8();
    il.active_effect = r.u8();
    il.effect_remaining_ms = r.u16();
    il.relay_on_mask = r.u8();
    il.relay_remaining_ms = r.u16();
    il.relay_cooldown_ms = r.u16();
    il.relay_pulses = r.u16();
    il.mist_pattern = r.u8();
    il.mist_flags = r.u8();
    il.mist_remaining_s = r.u16();
    il.mist_on_s = r.u16();
  } else if (c.device_type == static_cast<uint8_t>(DeviceType::Dial)) {
    dl.dial_state = r.u8();
    dl.cycle = r.u16();
    dl.switch_on = r.u8();
    dl.servo_deg = r.u8();
    dl.releases = r.u16();
    dl.last_release_command = r.u32();
    dl.sim_button = r.u8();
    if (len != kStatusCommonSize + kDialStatusSizeV1) {  // firmware >= 0.3
      dl.front_effect = r.u8();
      dl.transfer_effect = r.u8();
      dl.led_flags = r.u8();
      dl.color_r = r.u8();
      dl.color_g = r.u8();
      dl.color_b = r.u8();
    }
  } else if (c.device_type != static_cast<uint8_t>(DeviceType::StaffRemote)) {
    return false;
  }
  return r.ok() && r.atEnd();
}

size_t encodeError(const ErrorPayload& e, uint8_t* out, size_t cap) {
  Writer w(out, cap);
  w.u8(e.code).u16(e.ref_seq).u8(e.detail);
  return w.ok() ? w.size() : 0;
}
bool decodeError(const uint8_t* in, size_t len, ErrorPayload& e) {
  if (len != 4) return false;
  Reader r(in, len);
  e.code = r.u8();
  e.ref_seq = r.u16();
  e.detail = r.u8();
  return r.ok();
}

}  // namespace ufo
