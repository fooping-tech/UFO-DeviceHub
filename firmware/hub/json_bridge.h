// PC <-> Hub JSON Lines bridge (ArduinoJson; portable, unit-tested on the host).
//
// Converts JSON command lines into binary command payloads for HubCore and turns
// HubCore callbacks into JSON lines for the PC. See docs/protocol.md section 3.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ArduinoJson.h>

#include "hub_core.h"

namespace ufo {

class LineOutput {
 public:
  virtual ~LineOutput() {}
  virtual void writeLine(const char* line, size_t len) = 0;  // without the trailing '\n'
};

class RadioOutput {
 public:
  virtual ~RadioOutput() {}
  virtual bool sendRadio(const uint8_t mac[6], const uint8_t* data, size_t len) = 0;
};

struct HubInfo {
  uint8_t mac[6] = {0};
  const char* fw = "0.0.0";
  uint8_t channel = 1;
  uint8_t system_id = 1;
  const char* system_check = "ok";  // systemSeparationText()
  bool example_keys = false;
  bool example_devices = false;
  bool example_hardware = false;
};

using DiagProvider = void (*)(JsonObject transport, void* ctx);

// Builds a binary command payload from a JSON command name + params.
// Returns the payload size, or 0 with *error set to a short reason.
size_t buildCommandPayload(const char* name, JsonVariantConst params, uint8_t* out, size_t cap,
                           const char** error);

class HubJsonBridge : public HubSink {
 public:
  HubJsonBridge(LineOutput& out, RadioOutput& radio, const HubInfo& info);
  void attach(HubCore* core) { core_ = core; }
  void setDiagProvider(DiagProvider p, void* ctx) {
    diag_ = p;
    diag_ctx_ = ctx;
  }
  void setNow(uint32_t now) { now_ = now; }

  // One complete line from the PC (without the newline).
  void handleLine(const char* line, size_t len, uint32_t now);
  // Framing errors detected by the serial reader.
  void reportLineTooLong(size_t len);
  void emitBoot();
  void log(const char* level, const char* msg);

  // HubSink
  bool sendFrame(const uint8_t mac[6], const uint8_t* data, size_t len) override;
  void commandResult(const CommandResultInfo& r) override;
  void eventToPc(const PcEvent& e) override;
  void statusToPc(uint8_t source, const StatusCommon& c, const IlluminationStatus& il,
                  const DialStatus& dl) override;
  void linkChanged(uint8_t id, bool online) override;
  void deviceRebooted(uint8_t id, uint32_t boot_id) override;
  void sessionClosed(uint32_t session, SessionCloseReason reason) override;
  void deviceError(uint8_t source, const ErrorPayload& e) override;

  uint32_t badLines() const { return bad_lines_; }

 private:
  void emit(JsonDocument& doc);
  void error(const char* code, const char* detail);
  void hubAck(uint8_t dest, uint32_t command_id, AckResult r, const char* reason);
  void handleCommand(JsonDocument& doc, uint32_t now);
  void emitDevices();
  void emitDiag();

  LineOutput& out_;
  RadioOutput& radio_;
  HubInfo info_;
  HubCore* core_ = nullptr;
  DiagProvider diag_ = nullptr;
  void* diag_ctx_ = nullptr;
  uint32_t now_ = 0;
  uint32_t bad_lines_ = 0;
};

// Enum <-> name helpers shared with the tests.
const char* lightModeName(uint8_t m);
const char* mistPatternName(uint8_t p);
const char* effectName(uint8_t e);
const char* dialStateName(uint8_t s);
const char* dialEffectName(uint8_t e);
const char* illuminationStateName(uint8_t s);
const char* deviceTypeName(uint8_t t);
bool parseLightMode(JsonVariantConst v, uint8_t& out);
bool parseEffect(JsonVariantConst v, uint8_t& out);
bool parseOperationMode(JsonVariantConst v, uint8_t& out);
bool parseMistPattern(JsonVariantConst v, uint8_t& out);
void formatMac(const uint8_t mac[6], char out[18]);

}  // namespace ufo
