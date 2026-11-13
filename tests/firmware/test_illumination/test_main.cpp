// Illumination: light modes, effect priority / expiry, steam relay safety.
#include <unity.h>

#include "../support.h"
#include "illumination/illumination_controller.h"

using namespace ufo;
using namespace testsupport;

void setUp() {}
void tearDown() {}

namespace {
const uint8_t kSelf = static_cast<uint8_t>(DeviceId::Illumination);

struct Gpio : RelayOutput {
  bool on[4] = {false, false, false, false};
  int writes = 0;
  int on_count = 0;
  void setRelay(uint8_t id, bool v) override {
    if (v && !on[id]) ++on_count;
    on[id] = v;
    ++writes;
  }
};

struct Rig {
  Gpio gpio;
  bool configured[1] = {true};
  RelayController relay{gpio, 1, configured, 3000, 2000};
  IlluminationController app{relay, LightMode::Idle};
  FakeEmitter em;
  Rig() {
    app.setEmitter(&em);
    app.begin(0);
    app.onHubLinkChanged(true, 0);
  }
  CommandOutcome pulse(uint16_t ms, uint32_t now) {
    uint8_t b[8];
    RelayPulseParams p;
    p.duration_ms = ms;
    size_t n = encodeRelayPulse(p, b, sizeof(b));
    return app.onCommand(CommandCode::RelayPulse, b + 1, n - 1, 1, now);
  }
  CommandOutcome mode(LightMode m, uint32_t now) {
    uint8_t b[16];
    SetLightModeParams p;
    p.mode = static_cast<uint8_t>(m);
    size_t n = encodeSetLightMode(p, b, sizeof(b));
    return app.onCommand(CommandCode::SetLightMode, b + 1, n - 1, 1, now);
  }
  CommandOutcome effect(EffectId e, uint16_t ms, uint32_t now) {
    uint8_t b[8];
    TriggerEffectParams p;
    p.effect = static_cast<uint8_t>(e);
    p.duration_ms = ms;
    size_t n = encodeTriggerEffect(p, b, sizeof(b));
    return app.onCommand(CommandCode::TriggerEffect, b + 1, n - 1, 1, now);
  }
};
}  // namespace

void test_boot_relay_off() {
  Rig r;
  TEST_ASSERT_FALSE(r.gpio.on[0]);
  TEST_ASSERT_TRUE(r.gpio.writes >= 1);  // OFF level actively written at boot
}

void test_mode_switch_and_effect_returns_to_mode() {
  Rig r;
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.mode(LightMode::GamePlay, 0).result);
  TEST_ASSERT_EQUAL(LightMode::GamePlay, r.app.lighting().mode().mode);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.effect(EffectId::Catch, 500, 100).result);
  TEST_ASSERT_TRUE(r.app.lighting().effectActive());
  r.app.tick(599);
  TEST_ASSERT_TRUE(r.app.lighting().effectActive());
  r.app.tick(600);
  TEST_ASSERT_FALSE(r.app.lighting().effectActive());
  TEST_ASSERT_EQUAL(LightMode::GamePlay, r.app.lighting().mode().mode);  // mode persisted
}

void test_effect_priority() {
  Rig r;
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.effect(EffectId::Score, 1000, 0).result);
  TEST_ASSERT_EQUAL(AckResult::IgnoredPriority, r.effect(EffectId::ArmMove, 0, 10).result);
  TEST_ASSERT_EQUAL(EffectId::Score, r.app.lighting().effect().id);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.effect(EffectId::GameClear, 0, 20).result);  // interrupts
  TEST_ASSERT_EQUAL(EffectId::GameClear, r.app.lighting().effect().id);
  TEST_ASSERT_EQUAL(AckResult::IgnoredPriority, r.effect(EffectId::Score, 0, 30).result);
  // ERROR mode beats every effect
  r.mode(LightMode::Error, 40);
  TEST_ASSERT_FALSE(r.app.lighting().effectActive());
  TEST_ASSERT_EQUAL(AckResult::IgnoredPriority, r.effect(EffectId::GameClear, 0, 50).result);
  // after the effect expired a lower one may run again
  r.mode(LightMode::Idle, 60);
  r.effect(EffectId::Score, 100, 70);
  r.app.tick(200);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.effect(EffectId::ArmMove, 0, 200).result);
}

void test_relay_pulse_exact_duration() {
  Rig r;
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.pulse(500, 1000).result);
  TEST_ASSERT_TRUE(r.gpio.on[0]);
  r.app.tick(1499);
  TEST_ASSERT_TRUE(r.gpio.on[0]);
  r.app.tick(1500);
  TEST_ASSERT_FALSE(r.gpio.on[0]);
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::RelayDone));
  TEST_ASSERT_EQUAL(500, r.em.events[0].elapsed_ms);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(RelayEndReason::Completed), r.em.events[0].reason);
}

void test_relay_max_on_clamp_cooldown_busy() {
  Rig r;
  CommandOutcome o = r.pulse(10000, 0);
  TEST_ASSERT_EQUAL(AckResult::Accepted, o.result);
  TEST_ASSERT_TRUE(o.flags & u8(AckFlag::Clamped));
  TEST_ASSERT_EQUAL(AckResult::Busy, r.pulse(100, 100).result);
  r.app.tick(2999);
  TEST_ASSERT_TRUE(r.gpio.on[0]);
  r.app.tick(3000);
  TEST_ASSERT_FALSE(r.gpio.on[0]);  // limited to 3000 ms
  TEST_ASSERT_EQUAL(AckResult::Cooldown, r.pulse(100, 4000).result);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.pulse(100, 5000).result);
}

void test_relay_safety_paths() {
  Rig r;
  r.pulse(2000, 0);
  r.app.onHubLinkChanged(false, 100);  // link lost while steaming
  TEST_ASSERT_FALSE(r.gpio.on[0]);
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.pulse(100, 5000).result);  // no new pulse without link
  r.app.onHubLinkChanged(true, 5000);
  r.pulse(2000, 5000);
  r.app.onSessionChanged(0, 5100);  // PC gone
  TEST_ASSERT_FALSE(r.gpio.on[0]);
  r.app.onSessionChanged(9, 8000);
  r.pulse(2000, 8000);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.app.onCommand(CommandCode::StopAll, nullptr, 0, 9, 8100).result);
  TEST_ASSERT_FALSE(r.gpio.on[0]);
  uint8_t all = 0xFF;
  r.pulse(2000, 11000);
  r.app.onCommand(CommandCode::RelayOff, &all, 1, 10, 11100);
  TEST_ASSERT_FALSE(r.gpio.on[0]);
  uint8_t bad = 3;
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.app.onCommand(CommandCode::RelayOff, &bad, 1, 11, 11200).result);
}

void test_relay_never_exceeds_max_on() {
  Gpio g;
  bool cfg[1] = {true};
  RelayController rc(g, 1, cfg, 3000, 0);
  rc.begin();
  rc.pulse(0, 3000, 0);
  rc.tick(3000);  // normal completion exactly at max
  TEST_ASSERT_FALSE(g.on[0]);
}

void test_relay_not_configured() {
  Gpio g;
  bool cfg[1] = {false};
  RelayController rc(g, 1, cfg, 3000, 0);
  rc.begin();
  TEST_ASSERT_EQUAL(AckResult::NotConfigured, rc.pulse(0, 100, 0).result);
  TEST_ASSERT_EQUAL(0, g.writes);  // an unconfigured pin is never touched
}

void test_same_command_id_never_fires_twice_via_node_core() {
  Rig r;
  FakeNodeTransport tx;
  NodeConfig cfg;
  cfg.self_id = kSelf;
  cfg.device_type = DeviceType::Illumination;
  memcpy(cfg.hub_mac, hubMac(), 6);
  NodeCore core(cfg, tx, r.app);
  core.begin(0, 1, 1);
  auto sb = simpleCommand(kSelf, 77, 0x80000001, CommandCode::SessionBegin);
  core.onFrame(hubMac(), sb.data(), sb.size(), 0, 0);
  r.app.onHubLinkChanged(true, 0);
  uint8_t b[8];
  RelayPulseParams p;
  p.duration_ms = 1000;
  size_t n = encodeRelayPulse(p, b, sizeof(b));
  auto f = commandFrame(kSelf, 77, 500, b, n);
  core.onFrame(hubMac(), f.data(), f.size(), 0, 10);
  r.app.tick(1010);  // pulse finished, cooldown would block anyway -> wait past it
  r.app.tick(5000);
  core.onFrame(hubMac(), f.data(), f.size(), 0, 5000);  // late retransmission
  TEST_ASSERT_EQUAL(1, r.gpio.on_count);
  Packet last = tx.last(MsgType::Ack);
  TEST_ASSERT_TRUE(ackOf(last).flags & u8(AckFlag::Duplicate));
  TEST_ASSERT_EQUAL(u8(AckResult::Accepted), ackOf(last).result);
}

void test_arm_release_unsupported_here() {
  Rig r;
  TEST_ASSERT_EQUAL(AckResult::Unsupported, r.app.onCommand(CommandCode::ReleaseBall, nullptr, 0, 1, 0).result);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_relay_off);
  RUN_TEST(test_mode_switch_and_effect_returns_to_mode);
  RUN_TEST(test_effect_priority);
  RUN_TEST(test_relay_pulse_exact_duration);
  RUN_TEST(test_relay_max_on_clamp_cooldown_busy);
  RUN_TEST(test_relay_safety_paths);
  RUN_TEST(test_relay_never_exceeds_max_on);
  RUN_TEST(test_relay_not_configured);
  RUN_TEST(test_same_command_id_never_fires_twice_via_node_core);
  RUN_TEST(test_arm_release_unsupported_here);
  return UNITY_END();
}
