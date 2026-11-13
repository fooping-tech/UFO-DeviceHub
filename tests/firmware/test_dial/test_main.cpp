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
  void writeAngle(uint8_t d) override { writes.push_back(d); }
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
  return UNITY_END();
}
