#include "json_bridge.h"

#include <stdio.h>
#include <string.h>

namespace ufo {

namespace {

const char* const kLightModes[] = {"OFF",    "IDLE",  "STUDY",    "GAME_READY", "GAME_PLAY",  "RESULT", "ERROR",
                                   "CUSTOM", "AURORA", "STORM", "CAMPFIRE", "MIST_GLOW", "MAGIC_PUFF", "NIGHT"};
const char* const kMistPatterns[] = {"OFF", "CONTINUOUS", "BREATH", "PUFF"};
const char* const kEffects[] = {"NONE",          "GAME_START", "ARM_MOVE", "ARM_DESCEND", "CATCH",
                                "BALL_RECEIVED", "BALL_RELEASED", "SCORE", "GAME_CLEAR", "GAME_OVER"};
const char* const kDialStates[] = {"DISABLED",  "READY",     "ARMED",    "BALL_DETECTED",
                                   "RELEASING", "RETURNING", "COMPLETE", "ERROR",    "SETTINGS"};
const char* const kOpModes[] = {"STANDBY", "GAME"};
const char* const kDialEffects[] = {"NONE", "SOLID", "BREATHE", "CHASE", "COMET", "WIPE", "FLASH", "SPARKLE"};
const char* const kDialDirections[] = {"NONE", "TO_SCREEN", "TO_DIAL"};
const char* const kEffectPriorities[] = {"BACKGROUND", "NORMAL", "HIGH"};
const char* const kDialLedTargets[] = {"", "FRONT", "TRANSFER", "BOTH"};  // bit mask 1..3

template <size_t N>
bool parseNamed(JsonVariantConst v, const char* const (&names)[N], uint8_t min, uint8_t& out) {
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    for (size_t i = min; i < N; ++i)
      if (strcmp(s, names[i]) == 0) {
        out = static_cast<uint8_t>(i);
        return true;
      }
    return false;
  }
  if (v.is<uint32_t>()) {
    uint32_t n = v.as<uint32_t>();
    if (n < min || n >= N) return false;
    out = static_cast<uint8_t>(n);
    return true;
  }
  return false;
}

// Reads an optional unsigned integer param with range check.
bool getUint(JsonVariantConst params, const char* key, uint32_t max, uint32_t def, bool required,
             uint32_t& out) {
  JsonVariantConst v = params[key];
  if (v.isNull()) {
    if (required) return false;
    out = def;
    return true;
  }
  if (!v.is<uint32_t>()) return false;
  uint32_t n = v.as<uint32_t>();
  if (n > max) return false;
  out = n;
  return true;
}

bool parseColor(JsonVariantConst v, uint8_t& r, uint8_t& g, uint8_t& b) {
  if (v.isNull()) return true;
  if (v.is<JsonArrayConst>()) {
    JsonArrayConst a = v.as<JsonArrayConst>();
    if (a.size() != 3) return false;
    uint8_t c[3];
    for (size_t i = 0; i < 3; ++i) {
      if (!a[i].is<uint32_t>() || a[i].as<uint32_t>() > 255) return false;
      c[i] = static_cast<uint8_t>(a[i].as<uint32_t>());
    }
    r = c[0];
    g = c[1];
    b = c[2];
    return true;
  }
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    if (strlen(s) != 7 || s[0] != '#') return false;
    unsigned int rr, gg, bb;
    if (sscanf(s + 1, "%2x%2x%2x", &rr, &gg, &bb) != 3) return false;
    r = static_cast<uint8_t>(rr);
    g = static_cast<uint8_t>(gg);
    b = static_cast<uint8_t>(bb);
    return true;
  }
  return false;
}

const char* closeReasonName(SessionCloseReason r) {
  switch (r) {
    case SessionCloseReason::PcTimeout: return "pc_timeout";
    case SessionCloseReason::PcRequest: return "pc_request";
    case SessionCloseReason::Replaced: return "replaced";
  }
  return "unknown";
}

void flagsToJson(JsonObject o, uint8_t f) {
  o["session_valid"] = (f & u8(NodeFlag::SessionValid)) != 0;
  o["hub_link"] = (f & u8(NodeFlag::HubLinkUp)) != 0;
  o["busy"] = (f & u8(NodeFlag::Busy)) != 0;
  o["example_keys"] = (f & u8(NodeFlag::ExampleKeys)) != 0;
  o["config_error"] = (f & u8(NodeFlag::ConfigError)) != 0;
  o["relay_on"] = (f & u8(NodeFlag::RelayOn)) != 0;
  o["switch_on"] = (f & u8(NodeFlag::SwitchOn)) != 0;
  o["game_mode"] = (f & u8(NodeFlag::GameMode)) != 0;
}

const char* stateName(uint8_t device_type, uint8_t s) {
  if (device_type == static_cast<uint8_t>(DeviceType::Dial)) return dialStateName(s);
  if (device_type == static_cast<uint8_t>(DeviceType::Illumination)) return illuminationStateName(s);
  if (device_type == static_cast<uint8_t>(DeviceType::StaffRemote)) return s ? "ACTIVE" : "IDLE";
  return "UNKNOWN";
}

}  // namespace

const char* lightModeName(uint8_t m) {
  return m < sizeof(kLightModes) / sizeof(kLightModes[0]) ? kLightModes[m] : "UNKNOWN";
}
const char* mistPatternName(uint8_t p) {
  return p < sizeof(kMistPatterns) / sizeof(kMistPatterns[0]) ? kMistPatterns[p] : "UNKNOWN";
}
const char* effectName(uint8_t e) { return e < 10 ? kEffects[e] : "UNKNOWN"; }
const char* dialStateName(uint8_t s) { return s < 9 ? kDialStates[s] : "UNKNOWN"; }
const char* dialEffectName(uint8_t e) {
  return e < sizeof(kDialEffects) / sizeof(kDialEffects[0]) ? kDialEffects[e] : "UNKNOWN";
}
const char* illuminationStateName(uint8_t s) {
  switch (static_cast<IlluminationState>(s)) {
    case IlluminationState::Normal: return "NORMAL";
    case IlluminationState::Effect: return "EFFECT";
    case IlluminationState::RelayActive: return "RELAY_ACTIVE";
    case IlluminationState::MistActive: return "MIST_ACTIVE";
    case IlluminationState::Error: return "ERROR";
  }
  return "UNKNOWN";
}
const char* deviceTypeName(uint8_t t) {
  switch (static_cast<DeviceType>(t)) {
    case DeviceType::Hub: return "HUB";
    case DeviceType::Illumination: return "ILLUMINATION";
    case DeviceType::Dial: return "DIAL";
    case DeviceType::StaffRemote: return "STAFF_REMOTE";
  }
  return "UNKNOWN";
}
bool parseLightMode(JsonVariantConst v, uint8_t& out) { return parseNamed(v, kLightModes, 0, out); }
bool parseEffect(JsonVariantConst v, uint8_t& out) { return parseNamed(v, kEffects, 1, out); }
bool parseOperationMode(JsonVariantConst v, uint8_t& out) { return parseNamed(v, kOpModes, 0, out); }
bool parseMistPattern(JsonVariantConst v, uint8_t& out) { return parseNamed(v, kMistPatterns, 0, out); }

void formatMac(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

size_t buildCommandPayload(const char* name, JsonVariantConst params, uint8_t* out, size_t cap,
                           const char** error) {
  const char* dummy;
  if (!error) error = &dummy;
  *error = nullptr;
  if (!name) {
    *error = "missing name";
    return 0;
  }
  if (!params.isNull() && !params.is<JsonObjectConst>()) {
    *error = "params must be an object";
    return 0;
  }
  struct Simple {
    const char* name;
    CommandCode code;
  };
  static const Simple kSimple[] = {
      {"GET_STATUS", CommandCode::GetStatus},         {"STOP_ALL", CommandCode::StopAll},
      {"ARM_RECEIVER", CommandCode::ArmReceiver},     {"DISARM_RECEIVER", CommandCode::DisarmReceiver},
      {"RELEASE_BALL", CommandCode::ReleaseBall},
  };
  for (const auto& s : kSimple)
    if (strcmp(name, s.name) == 0) return encodeCommand(s.code, out, cap);

  if (strcmp(name, "SET_MODE") == 0) {
    uint8_t m;
    if (!parseOperationMode(params["mode"], m)) {
      *error = "mode must be STANDBY or GAME";
      return 0;
    }
    return encodeSetMode(static_cast<OperationMode>(m), out, cap);
  }
  if (strcmp(name, "SET_LIGHT_MODE") == 0) {
    SetLightModeParams p;
    uint32_t v;
    if (!parseLightMode(params["mode"], p.mode)) {
      *error = "invalid mode";
      return 0;
    }
    if (!getUint(params, "brightness", 255, 128, false, v)) {
      *error = "brightness 0-255";
      return 0;
    }
    p.brightness = static_cast<uint8_t>(v);
    if (!getUint(params, "transition_ms", 65535, 0, false, v)) {
      *error = "transition_ms 0-65535";
      return 0;
    }
    p.transition_ms = static_cast<uint16_t>(v);
    if (!getUint(params, "speed", 255, 128, false, v)) {
      *error = "speed 0-255";
      return 0;
    }
    p.speed = static_cast<uint8_t>(v);
    if (!parseColor(params["color"], p.r, p.g, p.b)) {
      *error = "color must be [r,g,b] or #RRGGBB";
      return 0;
    }
    return encodeSetLightMode(p, out, cap);
  }
  if (strcmp(name, "TRIGGER_EFFECT") == 0) {
    TriggerEffectParams p;
    uint32_t v;
    if (!parseEffect(params["effect"], p.effect)) {
      // accept "effect_id" as an alias
      if (!parseEffect(params["effect_id"], p.effect)) {
        *error = "invalid effect";
        return 0;
      }
    }
    if (!getUint(params, "duration_ms", 65535, 0, false, v)) {
      *error = "duration_ms 0-65535";
      return 0;
    }
    p.duration_ms = static_cast<uint16_t>(v);
    if (!getUint(params, "intensity", 255, 255, false, v)) {
      *error = "intensity 0-255";
      return 0;
    }
    p.intensity = static_cast<uint8_t>(v);
    return encodeTriggerEffect(p, out, cap);
  }
  if (strcmp(name, "RELAY_PULSE") == 0) {
    RelayPulseParams p;
    uint32_t v;
    if (!getUint(params, "relay_id", 254, 0, false, v)) {
      *error = "relay_id 0-254";
      return 0;
    }
    p.relay_id = static_cast<uint8_t>(v);
    if (!getUint(params, "duration_ms", 65535, 0, true, v) || v == 0) {
      *error = "duration_ms 1-65535 required";
      return 0;
    }
    p.duration_ms = static_cast<uint16_t>(v);
    return encodeRelayPulse(p, out, cap);
  }
  if (strcmp(name, "RELAY_OFF") == 0) {
    uint32_t v;
    if (!getUint(params, "relay_id", 255, 255, false, v)) {
      *error = "relay_id 0-255 (255 = all)";
      return 0;
    }
    return encodeRelayOff(static_cast<uint8_t>(v), out, cap);
  }
  if (strcmp(name, "SET_MIST") == 0) {
    SetMistParams p;
    uint32_t v;
    if (!parseMistPattern(params["pattern"], p.pattern)) {
      *error = "pattern must be OFF, CONTINUOUS, BREATH or PUFF";
      return 0;
    }
    if (!getUint(params, "duration_s", 65535, 0, false, v)) {
      *error = "duration_s 0-65535 (0 = device max)";
      return 0;
    }
    p.duration_s = static_cast<uint16_t>(v);
    return encodeSetMist(p, out, cap);
  }
  if (strcmp(name, "SET_DIAL_EFFECT") == 0) {
    SetDialEffectParams p;
    uint32_t v;
    if (!parseNamed(params["effect"], kDialEffects, 0, p.effect)) {
      *error = "effect must be NONE, SOLID, BREATHE, CHASE, COMET, WIPE, FLASH or SPARKLE";
      return 0;
    }
    if (!params["direction"].isNull() && !parseNamed(params["direction"], kDialDirections, 0, p.direction)) {
      *error = "direction must be NONE, TO_SCREEN or TO_DIAL";
      return 0;
    }
    if (dialEffectIsDirectional(p.effect) && p.direction == static_cast<uint8_t>(DialDirection::None)) {
      *error = "CHASE / COMET / WIPE need direction TO_SCREEN or TO_DIAL";
      return 0;
    }
    if (!parseColor(params["color"], p.r, p.g, p.b)) {
      *error = "color must be [r,g,b] or #RRGGBB";
      return 0;
    }
    if (!getUint(params, "duration_ms", 65535, 0, false, v)) {
      *error = "duration_ms 0-65535 (0 = until replaced)";
      return 0;
    }
    p.duration_ms = static_cast<uint16_t>(v);
    if (!params["priority"].isNull() && !parseNamed(params["priority"], kEffectPriorities, 0, p.priority)) {
      *error = "priority must be BACKGROUND, NORMAL or HIGH";
      return 0;
    }
    if (!params["target"].isNull() && !parseNamed(params["target"], kDialLedTargets, 1, p.target)) {
      *error = "target must be FRONT, TRANSFER or BOTH";
      return 0;
    }
    return encodeSetDialEffect(p, out, cap);
  }
  if (strcmp(name, "SET_DIAL_COLOR") == 0) {
    SetDialColorParams p;
    if (!parseColor(params["color"], p.r, p.g, p.b)) {
      *error = "color must be [r,g,b] or #RRGGBB";
      return 0;
    }
    return encodeSetDialColor(p, out, cap);
  }
  *error = "unknown command";
  return 0;
}

HubJsonBridge::HubJsonBridge(LineOutput& out, RadioOutput& radio, const HubInfo& info)
    : out_(out), radio_(radio), info_(info) {}

void HubJsonBridge::emit(JsonDocument& doc) {
  static char buf[2048];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf) - 1) {
    const char* msg = "{\"type\":\"ERROR\",\"code\":\"OUTPUT_TOO_LONG\"}";
    out_.writeLine(msg, strlen(msg));
    return;
  }
  out_.writeLine(buf, n);
}

void HubJsonBridge::error(const char* code, const char* detail) {
  JsonDocument d;
  d["type"] = "ERROR";
  d["code"] = code;
  if (detail) d["detail"] = detail;
  emit(d);
}

void HubJsonBridge::log(const char* level, const char* msg) {
  JsonDocument d;
  d["type"] = "LOG";
  d["level"] = level;
  d["msg"] = msg;
  emit(d);
}

void HubJsonBridge::reportLineTooLong(size_t len) {
  ++bad_lines_;
  char detail[32];
  snprintf(detail, sizeof(detail), "len>%u", static_cast<unsigned>(len));
  error("LINE_TOO_LONG", detail);
}

void HubJsonBridge::emitBoot() {
  JsonDocument d;
  char mac[18];
  formatMac(info_.mac, mac);
  d["type"] = "BOOT";
  d["protocol_version"] = kProtocolVersion;
  d["fw"] = info_.fw;
  d["hub_mac"] = mac;
  d["channel"] = info_.channel;
  d["system_id"] = info_.system_id;
  d["system_check"] = info_.system_check;
  d["example_keys"] = info_.example_keys;
  d["example_devices"] = info_.example_devices;
  emit(d);
}

void HubJsonBridge::hubAck(uint8_t dest, uint32_t command_id, AckResult r, const char* reason) {
  JsonDocument d;
  d["type"] = "ACK";
  d["destination"] = dest;
  d["command_id"] = command_id;
  d["result"] = ackResultName(u8(r));
  d["origin"] = "hub";
  d["duplicate"] = false;
  d["clamped"] = false;
  if (reason) d["reason"] = reason;
  emit(d);
}

void HubJsonBridge::handleLine(const char* line, size_t len, uint32_t now) {
  now_ = now;
  if (!core_) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line, len);
  if (err || !doc.is<JsonObject>()) {
    ++bad_lines_;
    error("BAD_JSON", err ? err.c_str() : "not an object");
    return;
  }
  JsonVariantConst ver = doc["protocol_version"];
  if (!ver.isNull() && (!ver.is<uint32_t>() || ver.as<uint32_t>() != kProtocolVersion)) {
    ++bad_lines_;
    error("UNSUPPORTED_VERSION", nullptr);
    return;
  }
  const char* type = doc["type"] | "";
  core_->onPcActivity(now);

  if (strcmp(type, "HELLO") == 0) {
    JsonVariantConst s = doc["session_id"];
    if (!s.is<uint32_t>() || s.as<uint32_t>() == 0) {
      error("BAD_REQUEST", "session_id must be a non-zero uint32");
      return;
    }
    // A PC that names its system is never attached to the hub of another system.
    JsonVariantConst sys = doc["system_id"];
    if (!sys.isNull() && (!sys.is<uint32_t>() || sys.as<uint32_t>() != info_.system_id)) {
      char detail[48];
      snprintf(detail, sizeof(detail), "this hub is system %u", info_.system_id);
      error("SYSTEM_MISMATCH", detail);
      return;
    }
    core_->openSession(s.as<uint32_t>(), now);
    JsonDocument d;
    char mac[18];
    formatMac(info_.mac, mac);
    d["type"] = "HELLO_ACK";
    d["protocol_version"] = kProtocolVersion;
    d["session_id"] = core_->session();
    d["fw"] = info_.fw;
    d["hub_id"] = kHubId;
    d["hub_mac"] = mac;
    d["channel"] = info_.channel;
    d["system_id"] = info_.system_id;
    d["system_check"] = info_.system_check;
    d["example_keys"] = info_.example_keys;
    d["example_devices"] = info_.example_devices;
    emit(d);
    return;
  }
  if (strcmp(type, "BYE") == 0) {
    core_->closeSession(now, SessionCloseReason::PcRequest);
    return;
  }
  if (strcmp(type, "HEARTBEAT") == 0) {
    JsonDocument d;
    d["type"] = "HEARTBEAT";
    d["session_id"] = core_->session();
    d["uptime_ms"] = core_->uptime(now);
    emit(d);
    return;
  }
  if (strcmp(type, "COMMAND") == 0) {
    handleCommand(doc, now);
    return;
  }
  if (strcmp(type, "EVENT_ACK") == 0) {
    JsonVariantConst src = doc["source"];
    JsonVariantConst id = doc["event_id"];
    if (!src.is<uint32_t>() || !id.is<uint32_t>()) {
      error("BAD_REQUEST", "EVENT_ACK needs source and event_id");
      return;
    }
    core_->pcEventAck(static_cast<uint8_t>(src.as<uint32_t>()), id.as<uint32_t>());
    return;
  }
  if (strcmp(type, "GET_DEVICES") == 0) {
    emitDevices();
    return;
  }
  if (strcmp(type, "GET_DIAG") == 0) {
    emitDiag();
    return;
  }
  ++bad_lines_;
  error("UNKNOWN_TYPE", type);
}

void HubJsonBridge::handleCommand(JsonDocument& doc, uint32_t now) {
  JsonVariantConst dest = doc["destination"];
  JsonVariantConst cid = doc["command_id"];
  JsonVariantConst sid = doc["session_id"];
  uint32_t command_id = cid.is<uint32_t>() ? cid.as<uint32_t>() : 0;
  uint8_t dest_id = dest.is<uint32_t>() && dest.as<uint32_t>() < 256 ? static_cast<uint8_t>(dest.as<uint32_t>()) : 0;
  if (!dest.is<uint32_t>() || dest.as<uint32_t>() > 255 || !cid.is<uint32_t>() || command_id == 0 ||
      !sid.is<uint32_t>()) {
    hubAck(dest_id, command_id, AckResult::BadRequest,
           "destination, command_id (non-zero) and session_id are required");
    return;
  }
  uint8_t payload[kMaxPayload];
  const char* why = nullptr;
  size_t n = buildCommandPayload(doc["name"].as<const char*>(), doc["params"], payload, sizeof(payload), &why);
  if (n == 0) {
    hubAck(dest_id, command_id, AckResult::BadRequest, why ? why : "bad command");
    return;
  }
  AckResult r = core_->submitCommand(dest_id, sid.as<uint32_t>(), command_id, payload, n, now);
  if (r != AckResult::Accepted) hubAck(dest_id, command_id, r, nullptr);
}

bool HubJsonBridge::sendFrame(const uint8_t mac[6], const uint8_t* data, size_t len) {
  return radio_.sendRadio(mac, data, len);
}

void HubJsonBridge::commandResult(const CommandResultInfo& r) {
  JsonDocument d;
  d["type"] = "ACK";
  d["destination"] = r.dest;
  d["command_id"] = r.command_id;
  d["result"] = ackResultName(r.result);
  d["origin"] = r.from_device ? "device" : "hub";
  d["duplicate"] = (r.flags & u8(AckFlag::Duplicate)) != 0;
  d["clamped"] = (r.flags & u8(AckFlag::Clamped)) != 0;
  d["attempts"] = r.attempts;
  emit(d);
}

void HubJsonBridge::eventToPc(const PcEvent& e) {
  JsonDocument d;
  const char* name = eventName(e.data.code);
  d["type"] = "EVENT";
  d["source"] = e.source;
  d["session_id"] = e.session;
  d["event_id"] = e.event_id;
  d["name"] = name ? name : "UNKNOWN";
  d["attempt"] = e.attempts;
  JsonObject data = d["data"].to<JsonObject>();
  switch (static_cast<EventCode>(e.data.code)) {
    case EventCode::BallReceived: data["cycle"] = e.data.cycle; break;
    case EventCode::ReleaseStarted: data["command_id"] = e.data.command_id; break;
    case EventCode::ReleaseDone:
      data["command_id"] = e.data.command_id;
      data["elapsed_ms"] = e.data.elapsed_ms;
      break;
    case EventCode::ReleaseFailed:
      data["command_id"] = e.data.command_id;
      data["reason"] = e.data.reason;
      break;
    case EventCode::DialRequest: {
      const char* req = dialRequestName(e.data.reason);
      data["request"] = req ? req : "UNKNOWN";
      data["code"] = e.data.reason;
      break;
    }
    case EventCode::RelayDone:
      data["relay_id"] = e.data.relay_id;
      data["on_ms"] = e.data.elapsed_ms;
      data["reason"] = e.data.reason;
      break;
    case EventCode::MistDone:
      data["on_s"] = e.data.on_s;
      data["reason"] = e.data.reason;
      break;
    case EventCode::DeviceError:
      data["code"] = e.data.reason;
      data["detail"] = e.data.detail;
      break;
    case EventCode::StaffAction: {
      const char* act = staffActionName(e.data.reason);
      data["action"] = act ? act : "UNKNOWN";
      data["code"] = e.data.reason;
      break;
    }
    case EventCode::RemoteInput: {
      const char* pose = remotePoseName(e.data.pose);
      data["move"] = (e.data.buttons & u8(RemoteButton::Move)) != 0;
      data["z"] = (e.data.buttons & u8(RemoteButton::AxisZ)) != 0;
      data["x"] = e.data.tilt_x;
      data["y"] = e.data.tilt_y;
      data["pose"] = pose ? pose : "OPEN";
      data["frame"] = e.data.frame;
      break;
    }
  }
  emit(d);
}

void HubJsonBridge::statusToPc(uint8_t source, const StatusCommon& c, const IlluminationStatus& il,
                               const DialStatus& dl) {
  JsonDocument d;
  d["type"] = "STATUS";
  d["source"] = source;
  d["device_type"] = deviceTypeName(c.device_type);
  d["state"] = stateName(c.device_type, c.state);
  d["state_code"] = c.state;
  flagsToJson(d["flags"].to<JsonObject>(), c.flags);
  d["error"] = c.error;
  d["op_mode"] = c.op_mode < 2 ? kOpModes[c.op_mode] : "UNKNOWN";
  d["uptime_ms"] = c.uptime_ms;
  d["boot_id"] = c.boot_id;
  char fw[12];
  snprintf(fw, sizeof(fw), "%u.%u", c.fw_major, c.fw_minor);
  d["fw"] = fw;
  if (c.device_type == static_cast<uint8_t>(DeviceType::Illumination)) {
    d["light_mode"] = lightModeName(il.light_mode);
    d["brightness"] = il.brightness;
    d["active_effect"] = effectName(il.active_effect);
    d["effect_remaining_ms"] = il.effect_remaining_ms;
    d["relay_on_mask"] = il.relay_on_mask;
    d["relay_remaining_ms"] = il.relay_remaining_ms;
    d["relay_cooldown_ms"] = il.relay_cooldown_ms;
    d["relay_pulses"] = il.relay_pulses;
    d["mist_pattern"] = mistPatternName(il.mist_pattern);
    d["mist_on"] = (il.mist_flags & u8(MistStatusFlag::OutputOn)) != 0;
    d["local_mode"] = (il.mist_flags & u8(MistStatusFlag::LocalMode)) != 0;
    d["mist_remaining_s"] = il.mist_remaining_s;
    d["mist_on_s"] = il.mist_on_s;
  } else if (c.device_type == static_cast<uint8_t>(DeviceType::Dial)) {
    d["dial_state"] = dialStateName(dl.dial_state);
    d["cycle"] = dl.cycle;
    d["switch_on"] = dl.switch_on != 0;
    d["servo_deg"] = dl.servo_deg;
    d["releases"] = dl.releases;
    d["last_release_command"] = dl.last_release_command;
    d["sim_button"] = dl.sim_button != 0;
    d["front_effect"] = dialEffectName(dl.front_effect);
    d["transfer_effect"] = dialEffectName(dl.transfer_effect);
    d["transfer_led"] = (dl.led_flags & u8(DialLedFlag::TransferFitted)) != 0;
    JsonArray col = d["unit_color"].to<JsonArray>();
    col.add(dl.color_r);
    col.add(dl.color_g);
    col.add(dl.color_b);
  }
  emit(d);
}

void HubJsonBridge::linkChanged(uint8_t id, bool online) {
  JsonDocument d;
  d["type"] = "LINK";
  d["source"] = id;
  d["online"] = online;
  emit(d);
}

void HubJsonBridge::deviceRebooted(uint8_t id, uint32_t boot_id) {
  JsonDocument d;
  d["type"] = "DEVICE_REBOOTED";
  d["source"] = id;
  d["boot_id"] = boot_id;
  emit(d);
}

void HubJsonBridge::sessionClosed(uint32_t session, SessionCloseReason reason) {
  JsonDocument d;
  d["type"] = "SESSION_CLOSED";
  d["session_id"] = session;
  d["reason"] = closeReasonName(reason);
  emit(d);
}

void HubJsonBridge::deviceError(uint8_t source, const ErrorPayload& e) {
  JsonDocument d;
  d["type"] = "DEVICE_ERROR";
  d["source"] = source;
  d["code"] = e.code;
  d["ref_seq"] = e.ref_seq;
  d["detail"] = e.detail;
  emit(d);
}

void HubJsonBridge::emitDevices() {
  JsonDocument d;
  d["type"] = "DEVICES";
  d["session_id"] = core_->session();
  JsonArray arr = d["devices"].to<JsonArray>();
  for (size_t i = 0; i < core_->childCount(); ++i) {
    const HubChild& ch = core_->childAt(i);
    JsonObject o = arr.add<JsonObject>();
    char mac[18];
    formatMac(ch.entry->mac, mac);
    o["id"] = ch.entry->id;
    o["name"] = ch.entry->name;
    o["type"] = deviceTypeName(static_cast<uint8_t>(ch.entry->type));
    o["mac"] = mac;
    o["online"] = ch.link.up();
    if (ch.link.everSeen())
      o["last_seen_ms"] = now_ - ch.link.lastRx();
    else
      o["last_seen_ms"] = nullptr;
    if (ch.hb_valid) {
      o["state"] = stateName(ch.last_hb.device_type, ch.last_hb.state);
      flagsToJson(o["flags"].to<JsonObject>(), ch.last_hb.flags);
      o["session_ok"] = core_->session() != 0 && ch.reported_session == core_->session();
      o["boot_id"] = ch.last_hb.boot_id;
      o["uptime_ms"] = ch.last_hb.uptime_ms;
      char fw[12];
      snprintf(fw, sizeof(fw), "%u.%u", ch.last_hb.fw_major, ch.last_hb.fw_minor);
      o["fw"] = fw;
      o["rssi"] = ch.last_hb.rssi;
    }
    o["rtt_ms"] = ch.rtt_last_ms;
  }
  emit(d);
}

void HubJsonBridge::emitDiag() {
  JsonDocument d;
  char mac[18];
  formatMac(info_.mac, mac);
  d["type"] = "DIAG";
  d["fw"] = info_.fw;
  d["hub_mac"] = mac;
  d["channel"] = info_.channel;
  d["system_id"] = info_.system_id;
  d["system_check"] = info_.system_check;
  d["uptime_ms"] = core_->uptime(now_);
  d["session_id"] = core_->session();
  d["example_keys"] = info_.example_keys;
  d["example_devices"] = info_.example_devices;
  d["example_hardware"] = info_.example_hardware;
  const HubCounters& c = core_->counters();
  JsonObject co = d["counters"].to<JsonObject>();
  co["rx_frames"] = c.rx_frames;
  co["invalid_packets"] = c.invalidTotal();
  co["rx_unknown_mac"] = c.rx_unknown_mac;
  co["rx_bad_magic"] = c.rx_bad_magic;
  co["rx_bad_version"] = c.rx_bad_version;
  co["rx_bad_length"] = c.rx_bad_length;
  co["rx_bad_type"] = c.rx_bad_type;
  co["rx_bad_id"] = c.rx_bad_id;
  co["rx_bad_payload"] = c.rx_bad_payload;
  co["late_acks"] = c.late_acks;
  co["commands_rejected"] = c.commands_rejected;
  co["pc_events_queued"] = c.pc_events_queued;
  co["pc_events_dropped"] = c.pc_events_dropped;
  co["pc_event_resends"] = c.pc_event_resends;
  co["pc_outbox"] = core_->pcOutboxSize();
  co["command_queue"] = core_->queuedCommands();
  co["sessions_opened"] = c.sessions_opened;
  co["sessions_closed"] = c.sessions_closed;
  co["serial_bad_lines"] = bad_lines_;
  if (diag_) diag_(d["transport"].to<JsonObject>(), diag_ctx_);
  JsonArray arr = d["devices"].to<JsonArray>();
  for (size_t i = 0; i < core_->childCount(); ++i) {
    const HubChild& ch = core_->childAt(i);
    JsonObject o = arr.add<JsonObject>();
    char m[18];
    formatMac(ch.entry->mac, m);
    o["id"] = ch.entry->id;
    o["name"] = ch.entry->name;
    o["mac"] = m;
    o["online"] = ch.link.up();
    o["last_seen_ms"] = ch.link.everSeen() ? static_cast<int32_t>(now_ - ch.link.lastRx()) : -1;
    o["tx"] = ch.c.tx_frames;
    o["rx"] = ch.c.rx_frames;
    o["commands"] = ch.c.commands;
    o["acks"] = ch.c.acks;
    o["ack_timeouts"] = ch.c.ack_timeouts;
    o["retransmissions"] = ch.c.retransmissions;
    o["hb_sent"] = ch.c.hb_sent;
    o["hb_recv"] = ch.c.hb_recv;
    o["events"] = ch.c.events;
    o["event_duplicates"] = ch.c.event_duplicates;
    o["invalid"] = ch.c.invalid;
    o["reboots"] = ch.c.reboots;
    o["rtt_last_ms"] = ch.rtt_last_ms;
    o["rtt_avg_ms"] = ch.rtt_avg_ms;
    if (ch.hb_valid) {
      o["rssi"] = ch.last_hb.rssi;
      o["device_rx_rejected"] = ch.last_hb.rx_rejected;
    }
  }
  emit(d);
}

}  // namespace ufo
