// UFO Device protocol - payload definitions (single source of truth).
//
// Every payload starts with a 1-byte code (CommandCode / EventCode / status kind)
// followed by the fields documented in docs/protocol.md. pc/protocol.py mirrors
// these values; tests/protocol/test_consistency.py parses this header and fails
// if the two diverge. Keep every enum entry in the form `Name = 0xNN,`.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

namespace ufo {

// ---- COMMAND (PC/Hub -> device) ---------------------------------------------
enum class CommandCode : uint8_t {
  SessionBegin = 0x01,    // hub only. session in header. no params
  SessionEnd = 0x02,      // hub only. no params
  GetStatus = 0x03,       // no params -> ACK + STATUS
  StopAll = 0x04,         // no params. always accepted (safety)
  SetMode = 0x05,         // u8 OperationMode
  SetLightMode = 0x10,    // u8 mode, u8 brightness, u16 transition_ms, u8 speed, u8 r, u8 g, u8 b
  TriggerEffect = 0x11,   // u8 effect, u16 duration_ms, u8 intensity
  RelayPulse = 0x12,      // u8 relay_id, u16 duration_ms
  RelayOff = 0x13,        // u8 relay_id (0xFF = all)
  ArmReceiver = 0x20,     // no params
  DisarmReceiver = 0x21,  // no params
  ReleaseBall = 0x22,     // no params (angles/hold time are device config, not wire params)
};

// ---- EVENT (device -> Hub -> PC) ---------------------------------------------
enum class EventCode : uint8_t {
  BallReceived = 0x10,    // u16 cycle
  ReleaseStarted = 0x11,  // u32 command_id
  ReleaseDone = 0x12,     // u32 command_id, u16 elapsed_ms
  ReleaseFailed = 0x13,   // u32 command_id, u8 ReleaseFailReason
  RelayDone = 0x20,       // u8 relay_id, u16 on_ms, u8 RelayEndReason
  DeviceError = 0x30,     // u8 ErrorCode, u8 detail
};

// ---- ACK result ---------------------------------------------------------------
// 0x00-0x7F: produced by the device. 0x80-: produced by the hub (JSON only).
enum class AckResult : uint8_t {
  Accepted = 0x00,
  Busy = 0x01,
  RejectedState = 0x02,
  RejectedSession = 0x03,
  InvalidParams = 0x04,
  Unsupported = 0x05,
  Cooldown = 0x06,
  IgnoredPriority = 0x07,
  NotConfigured = 0x08,
  Timeout = 0x80,
  Offline = 0x81,
  QueueFull = 0x82,
  UnknownDevice = 0x83,
  NoSession = 0x84,
  BadRequest = 0x85,
  SessionMismatch = 0x86,
  Superseded = 0x87,
};

enum class AckFlag : uint8_t {
  Duplicate = 0x01,  // same command_id already handled; result is the stored one
  Clamped = 0x02,    // accepted but a parameter was limited (e.g. relay max on time)
};

enum class DeviceType : uint8_t {
  Hub = 0x01,
  Illumination = 0x02,
  Dial = 0x03,
};

enum class OperationMode : uint8_t {
  Standby = 0x00,
  Game = 0x01,
};

enum class LightMode : uint8_t {
  Off = 0x00,
  Idle = 0x01,
  Study = 0x02,
  GameReady = 0x03,
  GamePlay = 0x04,
  Result = 0x05,
  Error = 0x06,
  Custom = 0x07,
};

enum class EffectId : uint8_t {
  None = 0x00,
  GameStart = 0x01,
  ArmMove = 0x02,
  ArmDescend = 0x03,
  Catch = 0x04,
  BallReceived = 0x05,
  BallReleased = 0x06,
  Score = 0x07,
  GameClear = 0x08,
  GameOver = 0x09,
};

enum class DialState : uint8_t {
  Disabled = 0x00,
  Ready = 0x01,
  Armed = 0x02,
  BallDetected = 0x03,
  Releasing = 0x04,
  Returning = 0x05,
  Complete = 0x06,
  Error = 0x07,
};

enum class IlluminationState : uint8_t {
  Normal = 0x00,
  Effect = 0x01,
  RelayActive = 0x02,
  Error = 0x07,
};

enum class ReleaseFailReason : uint8_t {
  Aborted = 0x01,         // STOP_ALL during the sequence
  LinkLost = 0x02,        // configured to abort on link loss
  ServoNotConfigured = 0x03,
};

enum class RelayEndReason : uint8_t {
  Completed = 0x00,
  OffCommand = 0x01,
  Safety = 0x02,  // STOP_ALL / session end / link loss
  Watchdog = 0x03,
};

enum class ErrorCode : uint8_t {
  None = 0x00,
  MacMismatch = 0x01,
  ServoNotConfigured = 0x02,
  OutboxOverflow = 0x03,
  RelayWatchdog = 0x04,
  BadPacket = 0x05,
  ExampleKeys = 0x06,
};

// Heartbeat / status flag bits
enum class NodeFlag : uint8_t {
  SessionValid = 0x01,
  HubLinkUp = 0x02,
  Busy = 0x04,
  ExampleKeys = 0x08,
  ConfigError = 0x10,
  RelayOn = 0x20,
  SwitchOn = 0x40,
  GameMode = 0x80,
};

inline uint8_t u8(AckResult r) { return static_cast<uint8_t>(r); }
inline uint8_t u8(AckFlag f) { return static_cast<uint8_t>(f); }
inline uint8_t u8(NodeFlag f) { return static_cast<uint8_t>(f); }

const char* commandName(uint8_t code);
const char* eventName(uint8_t code);
const char* ackResultName(uint8_t code);

// Commands that change the physical world require an established session.
// Commands that only move the system toward a safe state do not.
bool commandRequiresSession(uint8_t code);

// ---- payload structs + codecs -------------------------------------------------
struct AckPayload {
  uint8_t acked_type = 0;   // MsgType that is acknowledged (COMMAND / EVENT)
  uint8_t result = 0;       // AckResult
  uint8_t flags = 0;        // AckFlag bits
  uint16_t acked_seq = 0;
  uint8_t detail = 0;
};
constexpr size_t kAckPayloadSize = 6;
size_t encodeAck(const AckPayload& a, uint8_t* out, size_t cap);
bool decodeAck(const uint8_t* in, size_t len, AckPayload& a);

struct SetLightModeParams {
  uint8_t mode = 0;
  uint8_t brightness = 128;
  uint16_t transition_ms = 0;
  uint8_t speed = 128;
  uint8_t r = 0, g = 0, b = 0;
};
struct TriggerEffectParams {
  uint8_t effect = 0;
  uint16_t duration_ms = 0;  // 0 = device default for the effect
  uint8_t intensity = 255;
};
struct RelayPulseParams {
  uint8_t relay_id = 0;
  uint16_t duration_ms = 0;
};

// Command payload builders: write [code][params...]; return size or 0.
size_t encodeCommand(CommandCode code, uint8_t* out, size_t cap);  // no-param commands
size_t encodeSetMode(OperationMode m, uint8_t* out, size_t cap);
size_t encodeSetLightMode(const SetLightModeParams& p, uint8_t* out, size_t cap);
size_t encodeTriggerEffect(const TriggerEffectParams& p, uint8_t* out, size_t cap);
size_t encodeRelayPulse(const RelayPulseParams& p, uint8_t* out, size_t cap);
size_t encodeRelayOff(uint8_t relay_id, uint8_t* out, size_t cap);

// Param decoders take the bytes AFTER the command code and require an exact length.
bool decodeSetMode(const uint8_t* p, size_t n, OperationMode& m);
bool decodeSetLightMode(const uint8_t* p, size_t n, SetLightModeParams& out);
bool decodeTriggerEffect(const uint8_t* p, size_t n, TriggerEffectParams& out);
bool decodeRelayPulse(const uint8_t* p, size_t n, RelayPulseParams& out);
bool decodeRelayOff(const uint8_t* p, size_t n, uint8_t& relay_id);
// Expected param length for a command code (-1 = unknown command).
int commandParamLength(uint8_t code);

// Events
struct EventData {
  uint8_t code = 0;
  uint16_t cycle = 0;        // BallReceived
  uint32_t command_id = 0;   // Release*
  uint16_t elapsed_ms = 0;   // ReleaseDone / RelayDone(on_ms)
  uint8_t reason = 0;        // ReleaseFailed / RelayDone / DeviceError(code)
  uint8_t relay_id = 0;      // RelayDone
  uint8_t detail = 0;        // DeviceError
};
size_t encodeEvent(const EventData& e, uint8_t* out, size_t cap);
bool decodeEvent(const uint8_t* in, size_t len, EventData& e);

// Heartbeats
struct HubHeartbeat {
  uint32_t uptime_ms = 0;
  uint8_t flags = 0;  // bit0: PC session active
};
constexpr size_t kHubHeartbeatSize = 5;
size_t encodeHubHeartbeat(const HubHeartbeat& h, uint8_t* out, size_t cap);
bool decodeHubHeartbeat(const uint8_t* in, size_t len, HubHeartbeat& h);

struct NodeHeartbeat {
  uint16_t echo_seq = 0;  // sequence of the hub heartbeat being answered (RTT)
  uint32_t boot_id = 0;
  uint32_t uptime_ms = 0;
  uint8_t device_type = 0;
  uint8_t state = 0;
  uint8_t flags = 0;  // NodeFlag bits
  int8_t rssi = 0;    // 0 = unknown
  uint16_t rx_rejected = 0;
  uint8_t fw_major = 0;
  uint8_t fw_minor = 0;
};
constexpr size_t kNodeHeartbeatSize = 18;
size_t encodeNodeHeartbeat(const NodeHeartbeat& h, uint8_t* out, size_t cap);
bool decodeNodeHeartbeat(const uint8_t* in, size_t len, NodeHeartbeat& h);

// STATUS: common part + device specific part (selected by device_type)
struct StatusCommon {
  uint8_t device_type = 0;
  uint8_t state = 0;
  uint8_t flags = 0;
  uint8_t error = 0;
  uint8_t op_mode = 0;
  uint32_t uptime_ms = 0;
  uint32_t boot_id = 0;
  uint8_t fw_major = 0;
  uint8_t fw_minor = 0;
};
constexpr size_t kStatusCommonSize = 15;

struct IlluminationStatus {
  uint8_t light_mode = 0;
  uint8_t brightness = 0;
  uint8_t active_effect = 0;
  uint16_t effect_remaining_ms = 0;
  uint8_t relay_on_mask = 0;
  uint16_t relay_remaining_ms = 0;
  uint16_t relay_cooldown_ms = 0;
  uint16_t relay_pulses = 0;
};
constexpr size_t kIlluminationStatusSize = 12;

struct DialStatus {
  uint8_t dial_state = 0;
  uint16_t cycle = 0;
  uint8_t switch_on = 0;
  uint8_t servo_deg = 0;
  uint16_t releases = 0;
  uint32_t last_release_command = 0;
  uint8_t sim_button = 0;
};
constexpr size_t kDialStatusSize = 12;

size_t encodeStatus(const StatusCommon& c, const IlluminationStatus* il, const DialStatus* dl,
                    uint8_t* out, size_t cap);
bool decodeStatus(const uint8_t* in, size_t len, StatusCommon& c, IlluminationStatus& il, DialStatus& dl);

struct ErrorPayload {
  uint8_t code = 0;
  uint16_t ref_seq = 0;
  uint8_t detail = 0;
};
size_t encodeError(const ErrorPayload& e, uint8_t* out, size_t cap);
bool decodeError(const uint8_t* in, size_t len, ErrorPayload& e);

}  // namespace ufo
