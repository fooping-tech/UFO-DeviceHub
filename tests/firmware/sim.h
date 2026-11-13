// In-memory system simulation: HubCore + JSON bridge + 3 devices (NodeCore +
// real controllers) connected by a simulated radio with loss injection.
#pragma once

#include <ArduinoJson.h>

#include <deque>
#include <functional>
#include <string>

#include "hub/hub_core.h"
#include "hub/json_bridge.h"
#include "illumination/illumination_controller.h"
#include "m5dial/dial_controller.h"
#include "support.h"

namespace sim {

using namespace ufo;
using namespace testsupport;

struct Lines : LineOutput {
  std::vector<std::string> lines;
  void writeLine(const char* l, size_t n) override { lines.emplace_back(l, n); }
  std::vector<JsonDocument> ofType(const char* type) const {
    std::vector<JsonDocument> out;
    for (const auto& s : lines) {
      JsonDocument d;
      deserializeJson(d, s);
      if (strcmp(d["type"] | "", type) == 0) out.push_back(d);
    }
    return out;
  }
  JsonDocument lastOf(const char* type) const {
    auto v = ofType(type);
    return v.empty() ? JsonDocument() : v.back();
  }
  void clear() { lines.clear(); }
};

struct Air {
  struct Msg {
    bool to_hub;
    uint8_t mac[6];  // sender MAC (to hub) or destination MAC (from hub)
    std::vector<uint8_t> bytes;
  };
  std::deque<Msg> q;
  // return true to drop the frame
  std::function<bool(const Msg&)> drop = [](const Msg&) { return false; };
  uint32_t dropped = 0;
};

struct HubRadio : RadioOutput {
  Air* air;
  bool sendRadio(const uint8_t mac[6], const uint8_t* d, size_t n) override {
    Air::Msg m;
    m.to_hub = false;
    memcpy(m.mac, mac, 6);
    m.bytes.assign(d, d + n);
    air->q.push_back(m);
    return true;
  }
};

struct DevRadio : NodeTransport {
  Air* air;
  const uint8_t* mac;
  bool sendToHub(const uint8_t* d, size_t n) override {
    Air::Msg m;
    m.to_hub = true;
    memcpy(m.mac, mac, 6);
    m.bytes.assign(d, d + n);
    air->q.push_back(m);
    return true;
  }
};

struct ServoLog : ServoOutput {
  std::vector<uint8_t> writes;
  void writeAngle(uint8_t d) override { writes.push_back(d); }
  int count(uint8_t d) const {
    int n = 0;
    for (auto w : writes)
      if (w == d) ++n;
    return n;
  }
};

struct RelayLog : RelayOutput {
  bool on = false;
  int pulses = 0;
  void setRelay(uint8_t, bool v) override {
    if (v && !on) ++pulses;
    on = v;
  }
};

inline NodeConfig nodeCfg(DeviceId id, DeviceType t) {
  NodeConfig c;
  c.self_id = static_cast<uint8_t>(id);
  c.device_type = t;
  memcpy(c.hub_mac, hubMac(), 6);
  c.event_policy.retry_forever = true;
  return c;
}

struct Dial {
  ServoLog servo;
  Dispenser disp{servo, DispenserConfig()};
  BallReceiver rx{30};
  DialController app{disp, rx, DialConfig()};
  DevRadio radio;
  NodeCore core;
  bool sw = false;
  bool powered = true;
  Dial(Air& air, DeviceId id) : core(nodeCfg(id, DeviceType::Dial), radio, app) {
    radio.air = &air;
    radio.mac = macOf(id);
    app.setEmitter(&core);
  }
  void boot(uint32_t now, uint32_t boot_id) {
    app.begin(false, now);
    core.begin(now, boot_id, boot_id * 7919u);
  }
};

struct Illum {
  RelayLog gpio;
  bool configured[1] = {true};
  RelayController relay{gpio, 1, configured, 3000, 2000};
  IlluminationController app{relay, LightMode::Idle};
  DevRadio radio;
  NodeCore core;
  bool powered = true;
  explicit Illum(Air& air) : core(nodeCfg(DeviceId::Illumination, DeviceType::Illumination), radio, app) {
    radio.air = &air;
    radio.mac = macOf(DeviceId::Illumination);
    app.setEmitter(&core);
  }
  void boot(uint32_t now) {
    app.begin(now);
    core.begin(now, 0xABC, 0x1234567);
  }
};

struct World {
  Air air;
  Lines out;
  HubRadio hub_radio;
  HubInfo info;
  HubJsonBridge bridge{out, hub_radio, info};
  HubCore hub{bridge, HubConfig()};
  Illum illum{air};
  Dial dial1{air, DeviceId::M5Dial1};
  Dial dial2{air, DeviceId::M5Dial2};
  uint32_t now = 0;
  uint32_t session = 0;
  uint32_t next_cmd = 1;

  World() {
    hub_radio.air = &air;
    bridge.attach(&hub);
    hub.begin(0, 0x5555);
    illum.boot(0);
    dial1.boot(0, 0x100);
    dial2.boot(0, 0x200);
  }

  void deliver() {
    int guard = 0;
    while (!air.q.empty() && guard++ < 10000) {
      Air::Msg m = air.q.front();
      air.q.pop_front();
      if (air.drop(m)) {
        ++air.dropped;
        continue;
      }
      if (m.to_hub) {
        hub.onFrame(m.mac, m.bytes.data(), m.bytes.size(), -50, now);
        continue;
      }
      if (macEqual(m.mac, macOf(DeviceId::Illumination)) && illum.powered)
        illum.core.onFrame(hubMac(), m.bytes.data(), m.bytes.size(), -50, now);
      if (macEqual(m.mac, macOf(DeviceId::M5Dial1)) && dial1.powered)
        dial1.core.onFrame(hubMac(), m.bytes.data(), m.bytes.size(), -50, now);
      if (macEqual(m.mac, macOf(DeviceId::M5Dial2)) && dial2.powered)
        dial2.core.onFrame(hubMac(), m.bytes.data(), m.bytes.size(), -50, now);
    }
  }

  void step(uint32_t ms = 5) {
    now += ms;
    bridge.setNow(now);
    hub.tick(now);
    if (illum.powered) {
      illum.core.tick(now);
      illum.app.tick(now);
    }
    for (Dial* d : {&dial1, &dial2}) {
      if (!d->powered) continue;
      d->core.tick(now);
      d->app.tick(d->sw, now);
    }
    deliver();
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 5) step(5);
  }
  void pc(const std::string& line) {
    bridge.handleLine(line.c_str(), line.size(), now);
    deliver();
  }
  void hello(uint32_t s) {
    session = s;
    pc("{\"protocol_version\":1,\"type\":\"HELLO\",\"session_id\":" + std::to_string(s) + "}");
  }
  uint32_t command(uint8_t dest, const std::string& name, const std::string& params = "{}") {
    uint32_t id = next_cmd++;
    pc("{\"protocol_version\":1,\"type\":\"COMMAND\",\"destination\":" + std::to_string(dest) +
       ",\"session_id\":" + std::to_string(session) + ",\"command_id\":" + std::to_string(id) + ",\"name\":\"" +
       name + "\",\"params\":" + params + "}");
    return id;
  }
  void heartbeatPc() { pc("{\"type\":\"HEARTBEAT\"}"); }
  // keep the PC alive while time passes
  void runAlive(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 5) {
      if (t % 500 == 0) heartbeatPc();
      step(5);
    }
  }
  JsonDocument ackFor(uint32_t cmd) {
    for (auto& d : out.ofType("ACK"))
      if (d["command_id"].as<uint32_t>() == cmd) return d;
    return JsonDocument();
  }
  std::vector<JsonDocument> events(const char* name) {
    std::vector<JsonDocument> v;
    for (auto& d : out.ofType("EVENT"))
      if (strcmp(d["name"] | "", name) == 0) v.push_back(d);
    return v;
  }
  void ackEvent(const JsonDocument& e) {
    pc("{\"type\":\"EVENT_ACK\",\"source\":" + std::to_string(e["source"].as<int>()) +
       ",\"event_id\":" + std::to_string(e["event_id"].as<uint32_t>()) + "}");
  }
};

}  // namespace sim
