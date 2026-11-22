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
  SetMist = 0x14,         // u8 MistPattern, u16 duration_s (0 = device max). OFF stops the mist
  ArmReceiver = 0x20,     // no params
  DisarmReceiver = 0x21,  // no params
  ReleaseBall = 0x22,     // no params (angles/hold time are device config, not wire params)
  // Game mode LED contract (docs/game-mode-interface.md)
  SetDialEffect = 0x23,   // u8 DialEffect, u8 DialDirection, u8 r, u8 g, u8 b, u16 duration_ms, u8 EffectPriority, u8 DialLedTarget
  SetDialColor = 0x24,    // u8 r, u8 g, u8 b (unit colour; 0,0,0 = configured default)
};

// ---- EVENT (device -> Hub -> PC) ---------------------------------------------
enum class EventCode : uint8_t {
  BallReceived = 0x10,    // u16 cycle
  ReleaseStarted = 0x11,  // u32 command_id
  ReleaseDone = 0x12,     // u32 command_id, u16 elapsed_ms
  ReleaseFailed = 0x13,   // u32 command_id, u8 ReleaseFailReason
  DialRequest = 0x14,     // u8 DialRequest: chosen in the menu on the dial (fw 0.5+)
  RelayDone = 0x20,       // u8 relay_id, u16 on_ms, u8 RelayEndReason
  MistDone = 0x21,        // u16 on_s, u8 RelayEndReason
  DeviceError = 0x30,     // u8 ErrorCode, u8 detail
  // Staff remote (M5StopWatch, fw 0.6+). It only reports; the game decides.
  StaffAction = 0x40,     // u8 StaffAction: one of the game's named staff operations
  RemoteInput = 0x41,     // u8 RemoteButton bits, i8 tilt_x, i8 tilt_y (-100..100), u8 RemotePose, u16 frame
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
  StaffRemote = 0x04,
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
  // Mist scenes (COB tape + WS2811, from the former atom_led_mist sketch)
  Aurora = 0x08,
  Storm = 0x09,
  Campfire = 0x0A,
  MistGlow = 0x0B,   // brightness follows the mist (use with SET_MIST)
  MagicPuff = 0x0C,  // sparkles while the mist is on (use with SET_MIST)
  Night = 0x0D,
};

enum class MistPattern : uint8_t {
  Off = 0x00,
  Continuous = 0x01,
  Breath = 0x02,  // 8 s on / 6 s off
  Puff = 0x03,    // 3 s on / 9 s off
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
  Settings = 0x08,  // device-side settings / hardware test mode (button long press)
};

// ---- M5Dial LED effects (SET_DIAL_EFFECT) -----------------------------------
enum class DialEffect : uint8_t {
  None = 0x00,     // stop the PC effect: the device shows its own state again
  Solid = 0x01,
  Breathe = 0x02,
  Chase = 0x03,    // directional: every 4th pixel lit, running
  Comet = 0x04,    // directional: one head with a fading tail, repeating
  Wipe = 0x05,     // directional: fills over duration_ms (0 = 1 s, repeating)
  Flash = 0x06,    // 5 Hz blink
  Sparkle = 0x07,
};

// Direction of the light flow between the real machine (dial) and the screen.
enum class DialDirection : uint8_t {
  None = 0x00,
  ToScreen = 0x01,  // dial -> screen (capsule taken in)
  ToDial = 0x02,    // screen -> dial (capsule returned)
};

// A new effect replaces the running one only if its priority is >= the running one.
enum class EffectPriority : uint8_t {
  Background = 0x00,
  Normal = 0x01,
  High = 0x02,
};

// LED outputs of one dial (bit mask).
enum class DialLedTarget : uint8_t {
  Front = 0x01,      // M5Dial front LED on G1 (always present)
  Transfer = 0x02,  // second LED on G15 (NeoGacha topLed; optional in the configuration)
  Both = 0x03,
};

enum class DialLedFlag : uint8_t {
  TransferFitted = 0x01,  // the second LED (G15) is configured on this unit
};
inline uint8_t u8(DialLedFlag f) { return static_cast<uint8_t>(f); }

enum class IlluminationState : uint8_t {
  Normal = 0x00,
  Effect = 0x01,
  RelayActive = 0x02,
  MistActive = 0x03,
  Error = 0x07,
};

// What a person asked for in the menu on the dial (EventCode::DialRequest). The
// dial only reports it; the game decides what happens.
enum class DialRequest : uint8_t {
  GameRestart = 0x01,
  GameEnd = 0x02,
};

// The game's staff operations (CoreXY_UFO_Game docs/staff_operations.md: F6..F10 + Space),
// sent by the staff remote (EventCode::StaffAction). Names are the game's, in capitals.
enum class StaffAction : uint8_t {
  Next = 0x01,     // F6
  Skip = 0x02,     // F7
  Menu = 0x03,     // F8
  Recover = 0x04,  // F9 (the arm moves: the remote asks for a long press)
  Switch = 0x05,   // F10
  Stop = 0x06,     // Space
};

// The remote's hand-like input (EventCode::RemoteInput): bits of `buttons`.
enum class RemoteButton : uint8_t {
  Move = 0x01,  // the move button is held: steer with the tilt (like a pinch)
  AxisZ = 0x02,  // the tilt's forward/back steers Z (like a palm-down pinch)
};
inline uint8_t u8(RemoteButton b) { return static_cast<uint8_t>(b); }

// The hand shape the remote shows (like the camera's poses).
enum class RemotePose : uint8_t {
  Open = 0x00,   // neutral
  Fist = 0x01,   // catch (claw close / the easy mode's catch)
  Peace = 0x02,  // claw open
  Point = 0x03,  // one finger: carry to the portal (easy mode)
};

// state() of the staff remote (heartbeat / STATUS)
enum class RemoteState : uint8_t {
  Idle = 0x00,
  Active = 0x01,  // input is being sent
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
  LocalButton = 0x04,  // stopped with the device button
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
const char* dialRequestName(uint8_t request);  // nullptr = unknown
const char* staffActionName(uint8_t action);    // nullptr = unknown
const char* remotePoseName(uint8_t pose);       // nullptr = unknown
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
struct SetMistParams {
  uint8_t pattern = 0;      // MistPattern
  uint16_t duration_s = 0;  // 0 = device maximum
};
struct SetDialEffectParams {
  uint8_t effect = 0;        // DialEffect
  uint8_t direction = 0;     // DialDirection (required for Chase / Comet / Wipe)
  uint8_t r = 0, g = 0, b = 0;  // 0,0,0 = the unit colour
  uint16_t duration_ms = 0;  // 0 = until replaced / cleared by the device
  uint8_t priority = 1;      // EffectPriority
  uint8_t target = 3;        // DialLedTarget mask
};
struct SetDialColorParams {
  uint8_t r = 0, g = 0, b = 0;  // 0,0,0 = back to the configured default
};
// Chase / Comet / Wipe have a flow direction and need ToScreen or ToDial.
bool dialEffectIsDirectional(uint8_t effect);

// Command payload builders: write [code][params...]; return size or 0.
size_t encodeCommand(CommandCode code, uint8_t* out, size_t cap);  // no-param commands
size_t encodeSetMode(OperationMode m, uint8_t* out, size_t cap);
size_t encodeSetLightMode(const SetLightModeParams& p, uint8_t* out, size_t cap);
size_t encodeTriggerEffect(const TriggerEffectParams& p, uint8_t* out, size_t cap);
size_t encodeRelayPulse(const RelayPulseParams& p, uint8_t* out, size_t cap);
size_t encodeRelayOff(uint8_t relay_id, uint8_t* out, size_t cap);
size_t encodeSetMist(const SetMistParams& p, uint8_t* out, size_t cap);
size_t encodeSetDialEffect(const SetDialEffectParams& p, uint8_t* out, size_t cap);
size_t encodeSetDialColor(const SetDialColorParams& p, uint8_t* out, size_t cap);

// Param decoders take the bytes AFTER the command code and require an exact length.
bool decodeSetMode(const uint8_t* p, size_t n, OperationMode& m);
bool decodeSetLightMode(const uint8_t* p, size_t n, SetLightModeParams& out);
bool decodeTriggerEffect(const uint8_t* p, size_t n, TriggerEffectParams& out);
bool decodeRelayPulse(const uint8_t* p, size_t n, RelayPulseParams& out);
bool decodeRelayOff(const uint8_t* p, size_t n, uint8_t& relay_id);
bool decodeSetMist(const uint8_t* p, size_t n, SetMistParams& out);
bool decodeSetDialEffect(const uint8_t* p, size_t n, SetDialEffectParams& out);
bool decodeSetDialColor(const uint8_t* p, size_t n, SetDialColorParams& out);
// Expected param length for a command code (-1 = unknown command).
int commandParamLength(uint8_t code);

// Events
struct EventData {
  uint8_t code = 0;
  uint16_t cycle = 0;        // BallReceived
  uint32_t command_id = 0;   // Release*
  uint16_t elapsed_ms = 0;   // ReleaseDone / RelayDone(on_ms)
  uint16_t on_s = 0;         // MistDone
  uint8_t reason = 0;        // ReleaseFailed / RelayDone / MistDone / DeviceError(code) / DialRequest(request)
  uint8_t relay_id = 0;      // RelayDone
  uint8_t detail = 0;        // DeviceError
  // RemoteInput (StaffAction uses `reason`)
  uint8_t buttons = 0;       // RemoteButton bits
  int8_t tilt_x = 0;         // -100..100, + = right
  int8_t tilt_y = 0;         // -100..100, + = forward (away from the holder)
  uint8_t pose = 0;          // RemotePose
  uint16_t frame = 0;        // the remote's input frame counter (order; wraps)
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
  uint8_t mist_pattern = 0;       // MistPattern of the running mist (0 = none)
  uint8_t mist_flags = 0;         // MistStatusFlag bits
  uint16_t mist_remaining_s = 0;  // until the running mist stops by itself
  uint16_t mist_on_s = 0;         // output ON time of the running (or last) mist
};
constexpr size_t kIlluminationStatusSize = 18;

enum class MistStatusFlag : uint8_t {
  OutputOn = 0x01,   // the mist relay is ON right now
  LocalMode = 0x02,  // scenes are being driven by the device button (no PC session)
};
inline uint8_t u8(MistStatusFlag f) { return static_cast<uint8_t>(f); }

struct DialStatus {
  uint8_t dial_state = 0;
  uint16_t cycle = 0;
  uint8_t switch_on = 0;
  uint8_t servo_deg = 0;
  uint16_t releases = 0;
  uint32_t last_release_command = 0;
  uint8_t sim_button = 0;
  // Game mode LED fields (firmware >= 0.3). A 12-byte status from an older
  // dial is still accepted; these then stay 0.
  uint8_t front_effect = 0;      // DialEffect the PC is running on the front LED (0 = device state display)
  uint8_t transfer_effect = 0;  // same for the second LED (G15)
  uint8_t led_flags = 0;        // DialLedFlag bits
  uint8_t color_r = 0, color_g = 0, color_b = 0;  // unit colour in use
};
constexpr size_t kDialStatusSize = 18;
constexpr size_t kDialStatusSizeV1 = 12;  // firmware 0.2 and older

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
