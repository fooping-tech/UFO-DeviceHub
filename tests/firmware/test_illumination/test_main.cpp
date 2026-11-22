// Illumination: light modes, effect priority / expiry, steam relay safety,
// mist patterns / limits / safety, local button scenes.
#include <unity.h>

#include "../support.h"
#include "illumination/illumination_controller.h"
#include "illumination/shelf_layout.h"

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

struct MistPin : MistOutput {
  bool on = false;
  int on_count = 0;
  int writes = 0;
  void setMist(bool v) override {
    if (v && !on) ++on_count;
    on = v;
    ++writes;
  }
};

struct Rig {
  Gpio gpio;
  bool configured[1] = {true};
  RelayController relay{gpio, 1, configured, 3000, 2000};
  MistPin mist_pin;
  MistController mist{mist_pin, true, 600000};
  IlluminationController app{relay, mist, LightMode::Idle};
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
  CommandOutcome setMist(MistPattern pat, uint16_t s, uint32_t now) {
    uint8_t b[8];
    SetMistParams p;
    p.pattern = static_cast<uint8_t>(pat);
    p.duration_s = s;
    size_t n = encodeSetMist(p, b, sizeof(b));
    return app.onCommand(CommandCode::SetMist, b + 1, n - 1, 1, now);
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

void test_mist_boot_off_and_new_light_modes() {
  Rig r;
  TEST_ASSERT_FALSE(r.mist_pin.on);
  TEST_ASSERT_TRUE(r.mist_pin.writes >= 1);  // OFF actively written at boot
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.mode(LightMode::Aurora, 0).result);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.mode(LightMode::Night, 0).result);
  uint8_t b[16];
  SetLightModeParams p;
  p.mode = static_cast<uint8_t>(LightMode::Night) + 1;
  size_t n = encodeSetLightMode(p, b, sizeof(b));
  TEST_ASSERT_EQUAL(AckResult::InvalidParams,
                    r.app.onCommand(CommandCode::SetLightMode, b + 1, n - 1, 1, 0).result);
}

void test_mist_patterns() {
  Rig r;
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.setMist(MistPattern::Breath, 60, 1000).result);
  TEST_ASSERT_TRUE(r.mist_pin.on);
  r.app.tick(1000 + 7999);
  TEST_ASSERT_TRUE(r.mist_pin.on);
  r.app.tick(1000 + 8000);  // 8 s on
  TEST_ASSERT_FALSE(r.mist_pin.on);
  r.app.tick(1000 + 13999);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  r.app.tick(1000 + 14000);  // 6 s off
  TEST_ASSERT_TRUE(r.mist_pin.on);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(IlluminationState::MistActive), r.app.state());
  // a new pattern replaces the running one without MIST_DONE
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.setMist(MistPattern::Puff, 60, 20000).result);
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::MistDone));
  r.app.tick(20000 + 2999);
  TEST_ASSERT_TRUE(r.mist_pin.on);
  r.app.tick(20000 + 3000);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  TEST_ASSERT_TRUE(MistController::patternOn(MistPattern::Continuous, 123456));
  TEST_ASSERT_FALSE(MistController::patternOn(MistPattern::Off, 0));
}

void test_mist_duration_clamp_and_done_event() {
  Rig r;
  CommandOutcome o = r.setMist(MistPattern::Continuous, 5, 0);
  TEST_ASSERT_EQUAL(AckResult::Accepted, o.result);
  TEST_ASSERT_FALSE(o.flags & u8(AckFlag::Clamped));
  IlluminationStatus il;
  DialStatus dl;
  r.app.tick(2000);
  r.app.fillStatus(il, dl);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(MistPattern::Continuous), il.mist_pattern);
  TEST_ASSERT_TRUE(il.mist_flags & u8(MistStatusFlag::OutputOn));
  TEST_ASSERT_EQUAL(3, il.mist_remaining_s);
  TEST_ASSERT_EQUAL(2, il.mist_on_s);
  r.app.tick(4999);
  TEST_ASSERT_TRUE(r.mist_pin.on);
  r.app.tick(5000);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::MistDone));
  TEST_ASSERT_EQUAL(5, r.em.events[0].on_s);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(RelayEndReason::Completed), r.em.events[0].reason);
  // > 600 s -> clamped to the device maximum
  o = r.setMist(MistPattern::Continuous, 3600, 10000);
  TEST_ASSERT_TRUE(o.flags & u8(AckFlag::Clamped));
  r.app.tick(10000 + 599999);
  TEST_ASSERT_TRUE(r.mist_pin.on);
  r.app.tick(10000 + 600000);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  // 0 = maximum, not clamped
  o = r.setMist(MistPattern::Continuous, 0, 700000);
  TEST_ASSERT_FALSE(o.flags & u8(AckFlag::Clamped));
}

void test_mist_safety_paths() {
  Rig r;
  r.setMist(MistPattern::Continuous, 0, 0);
  r.app.onHubLinkChanged(false, 100);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.setMist(MistPattern::Continuous, 0, 200).result);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.setMist(MistPattern::Off, 0, 200).result);  // OFF always ok
  r.app.onHubLinkChanged(true, 300);
  r.setMist(MistPattern::Continuous, 0, 300);
  r.app.onSessionChanged(0, 400);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  r.app.onSessionChanged(5, 500);
  r.setMist(MistPattern::Continuous, 0, 500);
  r.app.onCommand(CommandCode::StopAll, nullptr, 0, 9, 600);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  r.setMist(MistPattern::Continuous, 0, 700);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.setMist(MistPattern::Off, 0, 800).result);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  size_t done = r.em.count(EventCode::MistDone);
  TEST_ASSERT_EQUAL(4, done);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(RelayEndReason::OffCommand), r.em.events.back().reason);
  // invalid pattern
  uint8_t bad[3] = {4, 0, 0};
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.app.onCommand(CommandCode::SetMist, bad, 3, 10, 900).result);
  // physical stop with the button while the PC is in control
  r.setMist(MistPattern::Continuous, 0, 1000);
  TEST_ASSERT_EQUAL(ButtonResult::MistOff, r.app.onButtonLong(1100));
  TEST_ASSERT_FALSE(r.mist_pin.on);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(RelayEndReason::LocalButton), r.em.events.back().reason);
  TEST_ASSERT_EQUAL(ButtonResult::Ignored, r.app.onButtonShort(1200));  // session 5 is active
}

void test_mist_not_configured() {
  Gpio g;
  bool cfg[1] = {false};
  RelayController rc(g, 1, cfg, 3000, 0);
  MistPin pin;
  MistController mc(pin, false, 600000);
  IlluminationController app(rc, mc, LightMode::Idle);
  app.begin(0);
  app.onHubLinkChanged(true, 0);
  uint8_t b[3] = {1, 0, 0};
  TEST_ASSERT_EQUAL(AckResult::NotConfigured, app.onCommand(CommandCode::SetMist, b, 3, 1, 0).result);
  TEST_ASSERT_EQUAL(0, pin.writes);  // an unconfigured pin is never touched
  TEST_ASSERT_TRUE(app.flags() & u8(NodeFlag::ConfigError));
}

void test_local_scenes_without_pc() {
  Rig r;
  r.app.onHubLinkChanged(false, 0);  // standalone: no hub at all
  TEST_ASSERT_EQUAL(ButtonResult::SceneChanged, r.app.onButtonShort(100));
  TEST_ASSERT_TRUE(r.app.localMode());
  TEST_ASSERT_EQUAL(LightMode::Aurora, r.app.lighting().mode().mode);  // scene 1 = aurora mist
  TEST_ASSERT_TRUE(r.mist_pin.on);
  r.app.onButtonShort(200);  // storm cloud
  TEST_ASSERT_EQUAL(LightMode::Storm, r.app.lighting().mode().mode);
  TEST_ASSERT_TRUE(r.mist_pin.on);
  // long press: mist off, the next scenes stay dry
  TEST_ASSERT_EQUAL(ButtonResult::MistOff, r.app.onButtonLong(300));
  TEST_ASSERT_FALSE(r.mist_pin.on);
  r.app.onButtonShort(400);  // campfire
  TEST_ASSERT_EQUAL(LightMode::Campfire, r.app.lighting().mode().mode);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  TEST_ASSERT_EQUAL(ButtonResult::MistOn, r.app.onButtonLong(500));
  TEST_ASSERT_TRUE(r.mist_pin.on);
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::MistDone));  // local runs are not reported
  // the local mist also stops at the device maximum
  r.app.tick(500 + 599999);
  TEST_ASSERT_TRUE(r.mist_pin.on);
  r.app.tick(500 + 600000);
  TEST_ASSERT_FALSE(r.mist_pin.on);
  // wraps around to "off"
  for (int i = 0; i < 5; ++i) r.app.onButtonShort(700000 + i);
  TEST_ASSERT_EQUAL(0, r.app.localScene());
  TEST_ASSERT_EQUAL(LightMode::Off, r.app.lighting().mode().mode);
}

void test_pc_session_takes_over_local_mode() {
  Rig r;
  r.app.onButtonShort(0);
  TEST_ASSERT_TRUE(r.mist_pin.on);
  r.app.onSessionChanged(42, 100);  // PC connects
  TEST_ASSERT_FALSE(r.app.localMode());
  TEST_ASSERT_FALSE(r.mist_pin.on);
  TEST_ASSERT_EQUAL(LightMode::Idle, r.app.lighting().mode().mode);  // back to the session-end mode
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::MistDone));
  IlluminationStatus il;
  DialStatus dl;
  r.app.fillStatus(il, dl);
  TEST_ASSERT_FALSE(il.mist_flags & u8(MistStatusFlag::LocalMode));
}

void test_local_mode_disabled_by_config() {
  Rig r;
  LocalConfig c;
  c.enabled = false;
  r.app.setLocalConfig(c);
  TEST_ASSERT_EQUAL(ButtonResult::Ignored, r.app.onButtonShort(0));
  TEST_ASSERT_EQUAL(ButtonResult::Ignored, r.app.onButtonLong(0));
  TEST_ASSERT_FALSE(r.mist_pin.on);
}

// ---- shelf layout: wiring order kept apart from the place on the shelf ----
using shelf::From;
using shelf::Part;

void test_shelf_layout_counts() {
  TEST_ASSERT_EQUAL(23, shelf::kLedCount);
  TEST_ASSERT_EQUAL(4, shelf::partCount(Part::FrontLeft));
  TEST_ASSERT_EQUAL(7, shelf::partCount(Part::Front));
  TEST_ASSERT_EQUAL(4, shelf::partCount(Part::FrontRight));
  TEST_ASSERT_EQUAL(4, shelf::partCount(Part::BackRight));
  TEST_ASSERT_EQUAL(4, shelf::partCount(Part::BackLeft));
  TEST_ASSERT_EQUAL(0, shelf::partCount(Part::Unused));
}

void test_shelf_layout_index_follows_the_wiring() {
  // front-left pillar, wired top -> bottom: 0..3
  TEST_ASSERT_EQUAL(0, shelf::index(Part::FrontLeft, 0, From::Top));
  TEST_ASSERT_EQUAL(3, shelf::index(Part::FrontLeft, 0, From::Bottom));
  // front face, left -> right: 4..10
  TEST_ASSERT_EQUAL(4, shelf::index(Part::Front, 0, From::Left));
  TEST_ASSERT_EQUAL(10, shelf::index(Part::Front, 0, From::Right));
  TEST_ASSERT_EQUAL(7, shelf::index(Part::Front, 3, From::Left));
  // front-right pillar, wired bottom -> top: 11 is the bottom, 14 the top
  TEST_ASSERT_EQUAL(14, shelf::index(Part::FrontRight, 0, From::Top));
  TEST_ASSERT_EQUAL(11, shelf::index(Part::FrontRight, 0, From::Bottom));
  // back-right, top -> bottom: 15..18
  TEST_ASSERT_EQUAL(15, shelf::index(Part::BackRight, 0, From::Top));
  TEST_ASSERT_EQUAL(18, shelf::index(Part::BackRight, 3, From::Top));
  // back-left, bottom -> top: 19 bottom, 22 top
  TEST_ASSERT_EQUAL(22, shelf::index(Part::BackLeft, 0, From::Top));
  TEST_ASSERT_EQUAL(19, shelf::index(Part::BackLeft, 0, From::Bottom));
  // out of range / an end that does not fit the part
  TEST_ASSERT_EQUAL(-1, shelf::index(Part::FrontLeft, 4, From::Top));
  TEST_ASSERT_EQUAL(-1, shelf::index(Part::FrontLeft, 0, From::Left));
  TEST_ASSERT_EQUAL(-1, shelf::index(Part::Front, 0, From::Top));
  TEST_ASSERT_EQUAL(-1, shelf::index(Part::Unused, 0, From::Top));
}

void test_shelf_layout_same_height_on_every_pillar() {
  // "the top of every pillar" is one height whichever way the pillar is wired
  for (Part p : shelf::kPillars) {
    shelf::Pixel top = shelf::pixel(static_cast<uint16_t>(shelf::index(p, 0, From::Top)));
    shelf::Pixel bottom = shelf::pixel(static_cast<uint16_t>(shelf::index(p, 0, From::Bottom)));
    TEST_ASSERT_EQUAL(p, top.part);
    TEST_ASSERT_EQUAL(255, top.height);
    TEST_ASSERT_EQUAL(0, bottom.height);
    TEST_ASSERT_EQUAL(0, top.pos);
    TEST_ASSERT_EQUAL(3, bottom.pos);
    TEST_ASSERT_EQUAL(top.around, bottom.around);
  }
  TEST_ASSERT_EQUAL(170, shelf::pixel(static_cast<uint16_t>(shelf::index(Part::FrontRight, 1, From::Top))).height);
}

void test_shelf_layout_around_the_shelf() {
  TEST_ASSERT_EQUAL(0, shelf::pixel(0).around);     // front-left
  TEST_ASSERT_EQUAL(64, shelf::pixel(11).around);   // front-right
  TEST_ASSERT_EQUAL(128, shelf::pixel(15).around);  // back-right
  TEST_ASSERT_EQUAL(192, shelf::pixel(19).around);  // back-left
  // the front face runs from the left pillar to the right one, in order
  uint8_t last = 0;
  for (uint8_t k = 0; k < 7; k++) {
    shelf::Pixel px = shelf::pixel(static_cast<uint16_t>(shelf::index(Part::Front, k, From::Left)));
    TEST_ASSERT_EQUAL(Part::Front, px.part);
    TEST_ASSERT_EQUAL(k, px.pos);
    TEST_ASSERT_EQUAL(shelf::kFrontHeight, px.height);
    TEST_ASSERT_TRUE(px.around > last && px.around < 64);
    last = px.around;
  }
  TEST_ASSERT_EQUAL(Part::Unused, shelf::pixel(shelf::kLedCount).part);
}

void test_shelf_layout_every_pixel_once() {
  // every pixel of the chain is reached exactly once through its place
  bool seen[shelf::kLedCount] = {};
  const Part parts[] = {Part::FrontLeft, Part::Front, Part::FrontRight, Part::BackRight, Part::BackLeft};
  for (Part p : parts)
    for (uint8_t k = 0; k < shelf::partCount(p); k++) {
      int16_t i = shelf::index(p, k, shelf::isPillar(p) ? From::Top : From::Left);
      TEST_ASSERT_TRUE(i >= 0 && i < shelf::kLedCount);
      TEST_ASSERT_FALSE(seen[i]);
      seen[i] = true;
      shelf::Pixel px = shelf::pixel(static_cast<uint16_t>(i));
      TEST_ASSERT_EQUAL(p, px.part);
      TEST_ASSERT_EQUAL(k, px.pos);
    }
  for (bool b : seen) TEST_ASSERT_TRUE(b);
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
  RUN_TEST(test_mist_boot_off_and_new_light_modes);
  RUN_TEST(test_mist_patterns);
  RUN_TEST(test_mist_duration_clamp_and_done_event);
  RUN_TEST(test_mist_safety_paths);
  RUN_TEST(test_mist_not_configured);
  RUN_TEST(test_local_scenes_without_pc);
  RUN_TEST(test_pc_session_takes_over_local_mode);
  RUN_TEST(test_local_mode_disabled_by_config);
  RUN_TEST(test_shelf_layout_counts);
  RUN_TEST(test_shelf_layout_index_follows_the_wiring);
  RUN_TEST(test_shelf_layout_same_height_on_every_pillar);
  RUN_TEST(test_shelf_layout_around_the_shelf);
  RUN_TEST(test_shelf_layout_every_pixel_once);
  return UNITY_END();
}
