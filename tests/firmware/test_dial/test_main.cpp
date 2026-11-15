// M5Dial: receiver debounce/edges, dispenser sequence, state machine, safety.
#include <unity.h>

#include "../support.h"
#include "m5dial/dial_controller.h"

using namespace ufo;
using namespace testsupport;

void setUp() {}
void tearDown() {}

namespace {
const uint8_t kSelf = static_cast<uint8_t>(DeviceId::M5Dial1);

struct FakeServo : ServoOutput {
  std::vector<uint8_t> writes;
  int releases = 0;
  void writeAngle(uint8_t d) override { writes.push_back(d); }
  void release() override { ++releases; }
  int count(uint8_t deg) const {
    int n = 0;
    for (auto w : writes)
      if (w == deg) ++n;
    return n;
  }
};

DispenserConfig neoGachaConfig() {
  DispenserConfig c;  // NeoGacha values: standby 180, release 0, hold 1500
  c.standby_deg = 180;
  c.release_deg = 0;
  c.hold_ms = 1500;
  c.return_settle_ms = 600;
  return c;
}

struct Rig {
  FakeServo servo;
  Dispenser disp{servo, neoGachaConfig()};
  BallReceiver rx{30};
  DialConfig cfg;
  DialController app{disp, rx, cfg};
  FakeEmitter em;
  bool sw = false;
  uint32_t now = 0;
  explicit Rig(bool abort_on_loss = false) {
    cfg.abort_release_on_link_loss = abort_on_loss;
    app.~DialController();
    new (&app) DialController(disp, rx, cfg);
    app.setEmitter(&em);
    app.begin(false, 0);
    app.onHubLinkChanged(true, 0);
  }
  CommandOutcome cmd(CommandCode c, uint32_t id = 1) {
    return app.onCommand(c, nullptr, 0, id, now);
  }
  CommandOutcome setMode(OperationMode m) {
    uint8_t b = static_cast<uint8_t>(m);
    return app.onCommand(CommandCode::SetMode, &b, 1, 1, now);
  }
  void run(uint32_t ms, uint32_t step = 5) {
    for (uint32_t t = 0; t < ms; t += step) {
      now += step;
      app.tick(sw, now);
    }
  }
  void game() {
    setMode(OperationMode::Game);
    cmd(CommandCode::ArmReceiver);
  }
  void dropBall() {
    sw = true;
    run(50);
  }
};
}  // namespace

void test_boot_safe_state() {
  Rig r;
  TEST_ASSERT_EQUAL(DialState::Disabled, r.app.dialState());
  TEST_ASSERT_EQUAL(180, r.servo.writes.back());  // standby at power up
  TEST_ASSERT_EQUAL(1, r.servo.writes.size());
}

void test_ball_detection_once_per_cycle() {
  Rig r;
  r.game();
  TEST_ASSERT_EQUAL(DialState::Armed, r.app.dialState());
  r.dropBall();
  TEST_ASSERT_EQUAL(DialState::BallDetected, r.app.dialState());
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::BallReceived));
  r.run(2000);  // switch held ON
  r.sw = false;
  r.run(100);
  r.sw = true;
  r.run(100);  // second ball without re-arming
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::BallReceived));
}

void test_chattering_filtered() {
  Rig r;
  r.game();
  for (int i = 0; i < 20; ++i) {  // 10 ms bounces
    r.sw = !r.sw;
    r.run(10);
  }
  r.sw = false;
  r.run(100);
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::BallReceived));
  r.sw = true;
  r.run(29, 1);
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::BallReceived));  // < 30 ms debounce
  r.run(5, 1);
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::BallReceived));
}

void test_ignored_outside_game() {
  Rig r;
  r.dropBall();  // DISABLED
  r.setMode(OperationMode::Game);
  r.sw = false;
  r.run(50);
  r.dropBall();  // READY but not armed
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::BallReceived));
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ReleaseBall).result);
}

void test_arm_while_switch_held_requires_release_first() {
  Rig r;
  r.setMode(OperationMode::Game);
  r.dropBall();  // ball (or stuck switch) present before arming
  r.cmd(CommandCode::ArmReceiver);
  r.run(500);
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::BallReceived));
  r.sw = false;
  r.run(50);
  r.dropBall();
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::BallReceived));
}

void test_release_sequence() {
  Rig r;
  r.game();
  r.dropBall();
  r.servo.writes.clear();
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cmd(CommandCode::ReleaseBall, 900).result);
  TEST_ASSERT_EQUAL(DialState::Releasing, r.app.dialState());
  TEST_ASSERT_EQUAL(0, r.servo.writes.back());  // release angle
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseStarted));
  TEST_ASSERT_EQUAL(AckResult::Busy, r.cmd(CommandCode::ReleaseBall, 901).result);  // during motion
  r.run(1495);
  TEST_ASSERT_EQUAL(DialState::Releasing, r.app.dialState());
  r.run(10);
  TEST_ASSERT_EQUAL(DialState::Returning, r.app.dialState());
  TEST_ASSERT_EQUAL(180, r.servo.writes.back());
  r.run(600);
  TEST_ASSERT_EQUAL(DialState::Complete, r.app.dialState());
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseDone));
  TEST_ASSERT_EQUAL(900, r.em.events.back().command_id);
  TEST_ASSERT_EQUAL(1, r.servo.count(0));
  // next cycle needs ARM_RECEIVER
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ReleaseBall, 902).result);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cmd(CommandCode::ArmReceiver).result);
  TEST_ASSERT_EQUAL(2, r.app.cycle());
}

void test_double_release_same_command_id_via_node_core() {
  Rig r;
  FakeNodeTransport tx;
  NodeConfig cfg;
  cfg.self_id = kSelf;
  memcpy(cfg.hub_mac, hubMac(), 6);
  NodeCore core(cfg, tx, r.app);
  core.begin(0, 1, 1);
  auto feed = [&](const std::vector<uint8_t>& f) { core.onFrame(hubMac(), f.data(), f.size(), 0, r.now); };
  feed(simpleCommand(kSelf, 3, 0x80000001, CommandCode::SessionBegin));
  uint8_t gm[2] = {static_cast<uint8_t>(CommandCode::SetMode), 1};
  feed(commandFrame(kSelf, 3, 10, gm, 2));
  feed(simpleCommand(kSelf, 3, 11, CommandCode::ArmReceiver));
  r.dropBall();
  auto rel = simpleCommand(kSelf, 3, 12, CommandCode::ReleaseBall);
  feed(rel);
  r.run(3000);
  feed(rel);  // retransmission after completion
  feed(rel);
  r.run(3000);
  TEST_ASSERT_EQUAL(1, r.servo.count(0));
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseStarted));
  TEST_ASSERT_TRUE(ackOf(tx.last(MsgType::Ack)).flags & u8(AckFlag::Duplicate));
}

void test_link_loss_behaviour() {
  Rig r;
  r.game();
  r.app.onHubLinkChanged(false, r.now);
  TEST_ASSERT_EQUAL(DialState::Ready, r.app.dialState());  // disarmed
  r.dropBall();
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::BallReceived));
  r.app.onHubLinkChanged(true, r.now);
  r.sw = false;
  r.run(50);
  TEST_ASSERT_EQUAL(DialState::Ready, r.app.dialState());  // waits for the next ARM_RECEIVER
}

void test_link_loss_during_release_finishes_to_standby() {
  Rig r;
  r.game();
  r.dropBall();
  r.cmd(CommandCode::ReleaseBall, 5);
  r.run(200);
  r.app.onHubLinkChanged(false, r.now);
  r.run(3000);
  TEST_ASSERT_EQUAL(180, r.servo.writes.back());
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseDone));
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ReleaseBall, 6).result);
}

void test_link_loss_abort_option() {
  Rig r(true);
  r.game();
  r.dropBall();
  r.cmd(CommandCode::ReleaseBall, 5);
  r.run(200);
  r.app.onHubLinkChanged(false, r.now);
  TEST_ASSERT_EQUAL(180, r.servo.writes.back());  // immediately back to standby
  r.run(1000);
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseFailed));
}

void test_stop_all_during_release() {
  Rig r;
  r.game();
  r.dropBall();
  r.cmd(CommandCode::ReleaseBall, 5);
  r.run(100);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cmd(CommandCode::StopAll, 6).result);
  TEST_ASSERT_EQUAL(180, r.servo.writes.back());
  r.run(1000);
  TEST_ASSERT_EQUAL(DialState::Disabled, r.app.dialState());
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseFailed));
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::ReleaseDone));
}

void test_session_change_resets_to_disabled() {
  Rig r;
  r.game();
  r.dropBall();
  r.app.onSessionChanged(0, r.now);
  TEST_ASSERT_EQUAL(DialState::Disabled, r.app.dialState());
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ReleaseBall).result);
}

void test_power_cycle_does_not_replay_release() {
  // A fresh boot (new controller) with a retransmitted RELEASE_BALL arriving
  // after the session is re-established must not dispense: no ball detected.
  Rig r;
  r.setMode(OperationMode::Game);
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ReleaseBall, 12).result);
  TEST_ASSERT_EQUAL(0, r.servo.count(0));
}

// ---- power: ramped motion + release at rest -----------------------------------
struct SlewRig {
  FakeServo servo;
  DispenserConfig cfg;
  Dispenser* disp = nullptr;
  uint32_t now = 0;
  SlewRig() {
    cfg = neoGachaConfig();
    cfg.slew_deg_per_s = 240;
    cfg.detach_after_ms = 500;
    disp = new Dispenser(servo, cfg);
    disp->begin(0);
  }
  ~SlewRig() { delete disp; }
  Dispenser::Result run(uint32_t ms, uint32_t step = 5) {
    Dispenser::Result last = Dispenser::Result::None;
    for (uint32_t t = 0; t < ms; t += step) {
      now += step;
      auto r = disp->tick(now);
      if (r != Dispenser::Result::None) last = r;
    }
    return last;
  }
};

void test_servo_ramps_instead_of_jumping() {
  SlewRig r;
  r.servo.writes.clear();
  TEST_ASSERT_TRUE(r.disp->start(1, r.now));
  // no single write moves more than a few degrees
  uint8_t prev = 180;
  r.run(100);
  for (auto w : r.servo.writes) {
    TEST_ASSERT_TRUE(prev - w <= 3);
    prev = w;
  }
  TEST_ASSERT_TRUE(r.disp->moving());
  r.run(700);  // 180 deg at 240 deg/s = 750 ms
  TEST_ASSERT_EQUAL(0, r.disp->angle());
  TEST_ASSERT_FALSE(r.disp->moving());
  // the hold starts on arrival: still releasing 1.4 s after arrival
  r.run(1400);
  TEST_ASSERT_EQUAL(Dispenser::Phase::Releasing, r.disp->phase());
  r.run(200);
  TEST_ASSERT_EQUAL(Dispenser::Phase::Returning, r.disp->phase());
  TEST_ASSERT_EQUAL(Dispenser::Result::Finished, r.run(1500));
  TEST_ASSERT_EQUAL(180, r.disp->angle());
}

void test_servo_released_at_rest_and_reattached() {
  SlewRig r;
  r.run(400);
  TEST_ASSERT_EQUAL(0, r.servo.releases);
  r.run(200);
  TEST_ASSERT_EQUAL(1, r.servo.releases);  // pulses off 500 ms after standby
  TEST_ASSERT_FALSE(r.disp->attached());
  r.run(2000);
  TEST_ASSERT_EQUAL(1, r.servo.releases);  // only once
  r.servo.writes.clear();
  r.disp->start(2, r.now);
  r.run(10);
  TEST_ASSERT_TRUE(r.disp->attached());
  TEST_ASSERT_FALSE(r.servo.writes.empty());
  r.run(5000);
  TEST_ASSERT_EQUAL(2, r.servo.releases);  // released again after the cycle
}

void test_abort_ramps_back_from_current_angle() {
  SlewRig r;
  r.disp->start(1, r.now);
  r.run(250);  // ~60 deg on the way down
  uint8_t mid = r.disp->angle();
  TEST_ASSERT_TRUE(mid < 180 && mid > 90);
  r.servo.writes.clear();
  r.disp->abort(r.now, 1);
  r.run(20);
  TEST_ASSERT_TRUE(r.servo.writes.back() >= mid);  // no jump to standby
  TEST_ASSERT_EQUAL(Dispenser::Result::Aborted, r.run(2000));
  TEST_ASSERT_EQUAL(180, r.disp->angle());
}

// ---- settings mode ---------------------------------------------------------------
void test_settings_enter_only_when_idle() {
  Rig r;
  TEST_ASSERT_TRUE(r.app.enterSettings(r.now));
  TEST_ASSERT_EQUAL(DialState::Settings, r.app.dialState());
  r.app.exitSettings(r.now);
  TEST_ASSERT_EQUAL(DialState::Disabled, r.app.dialState());
  r.game();  // ARMED: a game cycle runs
  TEST_ASSERT_FALSE(r.app.enterSettings(r.now));
  r.dropBall();  // BALL_DETECTED
  TEST_ASSERT_FALSE(r.app.enterSettings(r.now));
  r.cmd(CommandCode::ReleaseBall, 7);
  TEST_ASSERT_FALSE(r.app.enterSettings(r.now));  // servo running
  r.run(3000);
  TEST_ASSERT_EQUAL(DialState::Complete, r.app.dialState());
  TEST_ASSERT_TRUE(r.app.enterSettings(r.now));
}

void test_settings_switch_counts_without_events() {
  Rig r;
  r.app.enterSettings(r.now);
  for (int i = 0; i < 3; ++i) {
    r.sw = true;
    r.run(50);
    r.sw = false;
    r.run(50);
  }
  TEST_ASSERT_EQUAL(3, r.app.settingsSwitchCount());
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::BallReceived));
}

void test_settings_rejects_pc_commands_but_stop_all_leaves() {
  Rig r;
  r.app.enterSettings(r.now);
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.setMode(OperationMode::Game).result);
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ArmReceiver).result);
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ReleaseBall).result);
  r.app.onSessionChanged(5, r.now);  // a PC (re)connects: the person at the device keeps control
  TEST_ASSERT_EQUAL(DialState::Settings, r.app.dialState());
  TEST_ASSERT_TRUE(r.app.settingsReleaseTest(r.now));
  r.run(100);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cmd(CommandCode::StopAll).result);
  TEST_ASSERT_EQUAL(DialState::Disabled, r.app.dialState());
  r.run(3000);
  TEST_ASSERT_EQUAL(180, r.servo.writes.back());
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::ReleaseFailed));  // test motions never report
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::ReleaseDone));
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.setMode(OperationMode::Game).result);
}

void test_settings_servo_tests() {
  Rig r;
  r.app.enterSettings(r.now);
  r.servo.writes.clear();
  TEST_ASSERT_TRUE(r.app.settingsReleaseTest(r.now));
  TEST_ASSERT_EQUAL(0, r.servo.writes.back());
  TEST_ASSERT_FALSE(r.app.settingsServoAngle(90, r.now));  // busy with the release test
  r.run(3000);
  TEST_ASSERT_EQUAL(180, r.servo.writes.back());
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::ReleaseStarted));
  TEST_ASSERT_EQUAL(0, r.em.count(EventCode::ReleaseDone));
  // manual angle, then leave settings while the horn is away: it returns to standby quietly
  TEST_ASSERT_TRUE(r.app.settingsServoAngle(90, r.now));
  TEST_ASSERT_TRUE(r.app.settingsServoAngle(45, r.now));
  r.run(100);
  TEST_ASSERT_EQUAL(45, r.servo.writes.back());
  r.app.exitSettings(r.now);
  r.run(2000);
  TEST_ASSERT_EQUAL(180, r.servo.writes.back());
  TEST_ASSERT_EQUAL(DialState::Disabled, r.app.dialState());
  TEST_ASSERT_EQUAL(0, r.em.events.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_safe_state);
  RUN_TEST(test_ball_detection_once_per_cycle);
  RUN_TEST(test_chattering_filtered);
  RUN_TEST(test_ignored_outside_game);
  RUN_TEST(test_arm_while_switch_held_requires_release_first);
  RUN_TEST(test_release_sequence);
  RUN_TEST(test_double_release_same_command_id_via_node_core);
  RUN_TEST(test_link_loss_behaviour);
  RUN_TEST(test_link_loss_during_release_finishes_to_standby);
  RUN_TEST(test_link_loss_abort_option);
  RUN_TEST(test_stop_all_during_release);
  RUN_TEST(test_session_change_resets_to_disabled);
  RUN_TEST(test_power_cycle_does_not_replay_release);
  RUN_TEST(test_servo_ramps_instead_of_jumping);
  RUN_TEST(test_servo_released_at_rest_and_reattached);
  RUN_TEST(test_abort_ramps_back_from_current_angle);
  RUN_TEST(test_settings_enter_only_when_idle);
  RUN_TEST(test_settings_switch_counts_without_events);
  RUN_TEST(test_settings_rejects_pc_commands_but_stop_all_leaves);
  RUN_TEST(test_settings_servo_tests);
  return UNITY_END();
}
