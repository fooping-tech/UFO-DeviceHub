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
  explicit Rig(bool abort_on_loss = false, bool strip = false) {
    cfg.abort_release_on_link_loss = abort_on_loss;
    cfg.transfer_led = strip;
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
  CommandOutcome fx(DialEffect e, DialDirection d = DialDirection::None,
                    EffectPriority pr = EffectPriority::Normal, uint16_t ms = 0,
                    DialLedTarget target = DialLedTarget::Both, uint32_t id = 50, uint8_t red = 0, uint8_t green = 0,
                    uint8_t blue = 0) {
    SetDialEffectParams p;
    p.effect = static_cast<uint8_t>(e);
    p.direction = static_cast<uint8_t>(d);
    p.r = red;
    p.g = green;
    p.b = blue;
    p.duration_ms = ms;
    p.priority = static_cast<uint8_t>(pr);
    p.target = static_cast<uint8_t>(target);
    uint8_t b[16];
    size_t n = encodeSetDialEffect(p, b, sizeof(b));
    return app.onCommand(CommandCode::SetDialEffect, b + 1, n - 1, id, now);
  }
  LedPlan ring() const { return app.ledPlan(DialLedTarget::Front, now); }
  LedPlan strip() const { return app.ledPlan(DialLedTarget::Transfer, now); }
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
  // the ball left (switch OFF): the next cycle needs ARM_RECEIVER
  r.sw = false;
  r.run(50);
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ReleaseBall, 902).result);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cmd(CommandCode::ArmReceiver).result);
  TEST_ASSERT_EQUAL(2, r.app.cycle());
}

void test_release_again_while_the_ball_stays() {
  // 0.4: the release finished but the ball is still on the switch: a second RELEASE_BALL
  // (a new command id) runs the servo again. With the switch OFF it is refused as before.
  Rig r;
  r.game();
  r.dropBall();
  r.servo.writes.clear();
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cmd(CommandCode::ReleaseBall, 910).result);
  r.run(2200);
  TEST_ASSERT_EQUAL(DialState::Complete, r.app.dialState());
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseDone));
  // still there
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cmd(CommandCode::ReleaseBall, 911).result);
  TEST_ASSERT_EQUAL(DialState::Releasing, r.app.dialState());
  TEST_ASSERT_EQUAL(2, r.em.count(EventCode::ReleaseStarted));
  r.run(2200);
  TEST_ASSERT_EQUAL(DialState::Complete, r.app.dialState());
  TEST_ASSERT_EQUAL(2, r.em.count(EventCode::ReleaseDone));
  TEST_ASSERT_EQUAL(911, r.em.events.back().command_id);
  TEST_ASSERT_EQUAL(2, r.servo.count(0));
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::BallReceived));  // the same ball: no new event
  // now it left
  r.sw = false;
  r.run(50);
  TEST_ASSERT_EQUAL(AckResult::RejectedState, r.cmd(CommandCode::ReleaseBall, 912).result);
  TEST_ASSERT_EQUAL(2, r.servo.count(0));
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

// ---- game mode LEDs (docs/game-mode-interface.md) -----------------------------------
void test_led_path_geometry() {
  // NeoGacha arrangement: pixels 0..12 go up from the bottom, 13..25 come down from the top
  LedGeometry folded;
  folded.count = 26;
  folded.layout = LedLayout::Folded;
  TEST_ASSERT_EQUAL(13, ledPathLength(folded));
  TEST_ASSERT_EQUAL(0, ledPathPos(folded, 0));    // bottom of the first column
  TEST_ASSERT_EQUAL(12, ledPathPos(folded, 12));  // top of the first column
  TEST_ASSERT_EQUAL(12, ledPathPos(folded, 13));  // top of the second column
  TEST_ASSERT_EQUAL(1, ledPathPos(folded, 24));
  TEST_ASSERT_EQUAL(0, ledPathPos(folded, 25));   // bottom of the second column
  for (uint16_t i = 0; i < 13; ++i) TEST_ASSERT_EQUAL(ledPathPos(folded, i), ledPathPos(folded, 25 - i));
  folded.reversed = true;  // the screen is at the bottom
  TEST_ASSERT_EQUAL(12, ledPathPos(folded, 0));
  TEST_ASSERT_EQUAL(0, ledPathPos(folded, 13));
  LedGeometry odd;
  odd.count = 7;
  odd.layout = LedLayout::Folded;
  TEST_ASSERT_EQUAL(4, ledPathLength(odd));
  TEST_ASSERT_EQUAL(3, ledPathPos(odd, 3));  // the single pixel at the top
  TEST_ASSERT_EQUAL(2, ledPathPos(odd, 4));
  // a chase TO_SCREEN lights the same height in both columns and climbs
  for (uint32_t step = 0; step < 4; ++step)
    for (uint16_t i = 0; i < 13; ++i) {
      folded.reversed = false;
      uint8_t up = dialEffectLevel(DialEffect::Chase, DialDirection::ToScreen, ledPathPos(folded, i), 13,
                                   step * kChaseStepMs, 0);
      uint8_t down = dialEffectLevel(DialEffect::Chase, DialDirection::ToScreen, ledPathPos(folded, 25 - i), 13,
                                     step * kChaseStepMs, 0);
      TEST_ASSERT_EQUAL(up, down);
    }
  LedGeometry strip;
  strip.count = 30;
  TEST_ASSERT_EQUAL(30, ledPathLength(strip));
  TEST_ASSERT_EQUAL(0, ledPathPos(strip, 0));
  strip.reversed = true;
  TEST_ASSERT_EQUAL(29, ledPathPos(strip, 0));
  LedGeometry none;
  TEST_ASSERT_EQUAL(0, ledPathLength(none));
  TEST_ASSERT_EQUAL(0, dialEffectLevel(DialEffect::Solid, DialDirection::None, 0, 0, 0, 0));
}

// position of the first fully lit pixel
static int litPos(DialEffect e, DialDirection d, uint32_t t, uint16_t len = 12) {
  for (uint16_t p = 0; p < len; ++p)
    if (dialEffectLevel(e, d, p, len, t, 0) == 255) return p;
  return -1;
}

void test_led_direction_of_flow() {
  // CHASE: the lit pixels move up the path TO_SCREEN and down TO_DIAL
  TEST_ASSERT_EQUAL(0, litPos(DialEffect::Chase, DialDirection::ToScreen, 0));
  TEST_ASSERT_EQUAL(1, litPos(DialEffect::Chase, DialDirection::ToScreen, kChaseStepMs));
  TEST_ASSERT_EQUAL(2, litPos(DialEffect::Chase, DialDirection::ToScreen, 2 * kChaseStepMs));
  TEST_ASSERT_EQUAL(3, litPos(DialEffect::Chase, DialDirection::ToDial, 0));  // 11, 7, 3
  TEST_ASSERT_EQUAL(2, litPos(DialEffect::Chase, DialDirection::ToDial, kChaseStepMs));
  TEST_ASSERT_EQUAL(1, litPos(DialEffect::Chase, DialDirection::ToDial, 2 * kChaseStepMs));
  // COMET: one head; it starts at the dial end TO_SCREEN and at the screen end TO_DIAL
  TEST_ASSERT_EQUAL(0, litPos(DialEffect::Comet, DialDirection::ToScreen, 0));
  TEST_ASSERT_EQUAL(11, litPos(DialEffect::Comet, DialDirection::ToDial, 0));
  int mid_up = litPos(DialEffect::Comet, DialDirection::ToScreen, kCometPeriodMs / 2);
  int mid_down = litPos(DialEffect::Comet, DialDirection::ToDial, kCometPeriodMs / 2);
  TEST_ASSERT_TRUE(mid_up > 0 && mid_down < 11);
  TEST_ASSERT_EQUAL(11, mid_up + mid_down);  // mirror images
  // the tail is behind the head (towards the source)
  TEST_ASSERT_TRUE(dialEffectLevel(DialEffect::Comet, DialDirection::ToScreen, mid_up - 1, 12, kCometPeriodMs / 2, 0) > 0);
  TEST_ASSERT_EQUAL(0, dialEffectLevel(DialEffect::Comet, DialDirection::ToScreen, mid_up + 1, 12, kCometPeriodMs / 2, 0));
  // WIPE fills from the source end over duration_ms and stays full
  TEST_ASSERT_EQUAL(0, dialEffectLevel(DialEffect::Wipe, DialDirection::ToScreen, 0, 10, 0, 1000));
  TEST_ASSERT_EQUAL(255, dialEffectLevel(DialEffect::Wipe, DialDirection::ToScreen, 4, 10, 500, 1000));
  TEST_ASSERT_EQUAL(0, dialEffectLevel(DialEffect::Wipe, DialDirection::ToScreen, 5, 10, 500, 1000));
  TEST_ASSERT_EQUAL(255, dialEffectLevel(DialEffect::Wipe, DialDirection::ToDial, 5, 10, 500, 1000));
  TEST_ASSERT_EQUAL(0, dialEffectLevel(DialEffect::Wipe, DialDirection::ToDial, 4, 10, 500, 1000));
  TEST_ASSERT_EQUAL(255, dialEffectLevel(DialEffect::Wipe, DialDirection::ToScreen, 9, 10, 5000, 1000));
  TEST_ASSERT_EQUAL(255, dialEffectLevel(DialEffect::Flash, DialDirection::None, 3, 10, 0, 0));
  TEST_ASSERT_EQUAL(0, dialEffectLevel(DialEffect::Flash, DialDirection::None, 3, 10, kFlashPeriodMs / 2, 0));
  TEST_ASSERT_EQUAL(0, dialEffectLevel(DialEffect::None, DialDirection::None, 3, 10, 0, 0));
}

void test_dial_effect_accepted_and_shown() {
  Rig r;
  TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);  // DISABLED: device display
  r.now = 1000;
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::Comet, DialDirection::ToDial).result);
  r.now = 1200;
  LedPlan p = r.ring();
  TEST_ASSERT_EQUAL(LedScene::Effect, p.scene);
  TEST_ASSERT_TRUE(p.from_pc);
  TEST_ASSERT_EQUAL(DialEffect::Comet, p.effect);
  TEST_ASSERT_EQUAL(DialDirection::ToDial, p.direction);
  TEST_ASSERT_EQUAL(200, p.elapsed_ms);
  TEST_ASSERT_EQUAL(0, p.r);  // no colour in the command: unit colour (default cyan)
  TEST_ASSERT_EQUAL(255, p.g);
  TEST_ASSERT_EQUAL(255, p.b);
  TEST_ASSERT_EQUAL(LedScene::Off, r.strip().scene);  // no strip fitted
  // explicit colour
  r.fx(DialEffect::Solid, DialDirection::None, EffectPriority::Normal, 0, DialLedTarget::Both, 51, 10, 20, 30);
  p = r.ring();
  TEST_ASSERT_EQUAL(10, p.r);
  TEST_ASSERT_EQUAL(30, p.b);
  // NONE stops it: back to the device display
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::None).result);
  TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
  // status
  r.fx(DialEffect::Sparkle);
  IlluminationStatus il;
  DialStatus dl;
  r.app.fillStatus(il, dl);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DialEffect::Sparkle), dl.front_effect);
  TEST_ASSERT_EQUAL(0, dl.transfer_effect);
  TEST_ASSERT_EQUAL(0, dl.led_flags);
  TEST_ASSERT_EQUAL(255, dl.color_g);
}

void test_dial_effect_invalid_params() {
  Rig r;
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.fx(DialEffect::Chase).result);  // needs a direction
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.fx(DialEffect::Wipe, DialDirection::None).result);
  uint8_t raw[9] = {8, 0, 0, 0, 0, 0, 0, 1, 3};  // unknown effect
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.app.onCommand(CommandCode::SetDialEffect, raw, 9, 1, 0).result);
  uint8_t prio[9] = {1, 0, 0, 0, 0, 0, 0, 3, 3};  // unknown priority
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.app.onCommand(CommandCode::SetDialEffect, prio, 9, 1, 0).result);
  uint8_t tgt[9] = {1, 0, 0, 0, 0, 0, 0, 1, 0};  // no target
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.app.onCommand(CommandCode::SetDialEffect, tgt, 9, 1, 0).result);
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.app.onCommand(CommandCode::SetDialEffect, tgt, 8, 1, 0).result);
  TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
}

void test_dial_effect_priority_and_duration() {
  Rig r;
  r.now = 100;
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::Flash, DialDirection::None, EffectPriority::High, 500).result);
  TEST_ASSERT_EQUAL(AckResult::IgnoredPriority, r.fx(DialEffect::Solid).result);
  TEST_ASSERT_EQUAL(AckResult::IgnoredPriority, r.fx(DialEffect::None).result);  // a low priority stop too
  TEST_ASSERT_EQUAL(DialEffect::Flash, r.ring().effect);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::Sparkle, DialDirection::None, EffectPriority::High, 500).result);
  r.run(495);
  TEST_ASSERT_EQUAL(DialEffect::Sparkle, r.ring().effect);
  r.run(10);  // duration over: device display again, lower priorities are free
  TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
  IlluminationStatus il;
  DialStatus dl;
  r.app.fillStatus(il, dl);
  TEST_ASSERT_EQUAL(0, dl.front_effect);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::Solid, DialDirection::None, EffectPriority::Background).result);
}

void test_transfer_strip_optional() {
  Rig ring_only;
  TEST_ASSERT_EQUAL(AckResult::NotConfigured,
                    ring_only.fx(DialEffect::Chase, DialDirection::ToDial, EffectPriority::Normal, 0,
                                 DialLedTarget::Transfer).result);
  TEST_ASSERT_EQUAL(AckResult::Accepted, ring_only.fx(DialEffect::Chase, DialDirection::ToDial).result);  // BOTH
  TEST_ASSERT_EQUAL(DialEffect::Chase, ring_only.ring().effect);
  TEST_ASSERT_EQUAL(LedScene::Off, ring_only.strip().scene);

  Rig r(false, true);  // strip fitted
  TEST_ASSERT_EQUAL(LedScene::Off, r.strip().scene);  // dark until an effect runs
  TEST_ASSERT_EQUAL(AckResult::Accepted,
                    r.fx(DialEffect::Comet, DialDirection::ToDial, EffectPriority::High, 0, DialLedTarget::Transfer).result);
  TEST_ASSERT_EQUAL(DialEffect::Comet, r.strip().effect);
  TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);  // outputs are independent
  // BOTH is all or nothing: the strip's HIGH effect blocks a NORMAL one for both outputs
  TEST_ASSERT_EQUAL(AckResult::IgnoredPriority, r.fx(DialEffect::Solid).result);
  TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::Solid, DialDirection::None, EffectPriority::Normal, 0,
                                             DialLedTarget::Front).result);
  IlluminationStatus il;
  DialStatus dl;
  r.app.fillStatus(il, dl);
  TEST_ASSERT_EQUAL(u8(DialLedFlag::TransferFitted), dl.led_flags);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DialEffect::Comet), dl.transfer_effect);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DialEffect::Solid), dl.front_effect);
}

void test_local_receive_animation() {
  Rig r(false, true);
  r.game();
  TEST_ASSERT_EQUAL(LedScene::Armed, r.ring().scene);
  r.fx(DialEffect::Breathe, DialDirection::None, EffectPriority::High);  // PC attract effect while waiting
  r.sw = true;
  r.run(35);  // debounce -> BALL_DETECTED
  TEST_ASSERT_EQUAL(DialState::BallDetected, r.app.dialState());
  // at once, before the PC knows: white flash on both outputs, the PC effect is gone
  for (LedPlan p : {r.ring(), r.strip()}) {
    TEST_ASSERT_EQUAL(LedScene::Effect, p.scene);
    TEST_ASSERT_FALSE(p.from_pc);
    TEST_ASSERT_EQUAL(DialEffect::Solid, p.effect);
    TEST_ASSERT_EQUAL(255, p.r);
  }
  r.run(200);  // then the unit colour runs to the screen
  for (LedPlan p : {r.ring(), r.strip()}) {
    TEST_ASSERT_EQUAL(DialEffect::Chase, p.effect);
    TEST_ASSERT_EQUAL(DialDirection::ToScreen, p.direction);
    TEST_ASSERT_EQUAL(0, p.r);
    TEST_ASSERT_EQUAL(255, p.b);
    TEST_ASSERT_FALSE(p.from_pc);
  }
  // the PC effect that arrives after BALL_RECEIVED takes over (same look, PC timing)
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::Chase, DialDirection::ToScreen, EffectPriority::Normal, 800).result);
  TEST_ASSERT_TRUE(r.ring().from_pc);
  r.run(900);  // PC effect over, local chase time (1500 ms) not yet
  TEST_ASSERT_FALSE(r.ring().from_pc);
  TEST_ASSERT_EQUAL(DialEffect::Chase, r.ring().effect);
  r.run(1000);  // transfer shown: holding display until RELEASE_BALL
  TEST_ASSERT_EQUAL(DialEffect::Breathe, r.ring().effect);
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::BallReceived));
}

void test_pc_effect_during_flash_waits_for_flash() {
  Rig r;
  r.game();
  r.sw = true;
  r.run(35);
  r.fx(DialEffect::Chase, DialDirection::ToScreen);  // arrives within the 150 ms flash
  TEST_ASSERT_EQUAL(DialEffect::Solid, r.ring().effect);
  TEST_ASSERT_FALSE(r.ring().from_pc);
  r.run(160);
  TEST_ASSERT_TRUE(r.ring().from_pc);
}

void test_release_animation_and_end() {
  Rig r;
  r.game();
  r.dropBall();
  r.run(3000);
  // the game starts the return flow before RELEASE_BALL
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::Comet, DialDirection::ToDial).result);
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.cmd(CommandCode::ReleaseBall, 900).result);
  TEST_ASSERT_EQUAL(DialEffect::Comet, r.ring().effect);  // the PC effect keeps running
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.fx(DialEffect::None).result);
  LedPlan p = r.ring();  // device's own release animation: unit colour towards the dial
  TEST_ASSERT_EQUAL(DialEffect::Chase, p.effect);
  TEST_ASSERT_EQUAL(DialDirection::ToDial, p.direction);
  TEST_ASSERT_FALSE(p.from_pc);
  r.fx(DialEffect::Chase, DialDirection::ToDial, EffectPriority::High);
  r.run(2200);  // RELEASE_DONE: the device ends the animation, even a HIGH one
  TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseDone));
  TEST_ASSERT_EQUAL(LedScene::Complete, r.ring().scene);
  TEST_ASSERT_EQUAL(1, r.servo.count(0));
}

void test_effects_never_change_the_release_sequence() {
  Rig plain, lit;
  for (Rig* r : {&plain, &lit}) {
    r->game();
    r->dropBall();
  }
  lit.fx(DialEffect::Flash, DialDirection::None, EffectPriority::High);
  lit.fx(DialEffect::Chase, DialDirection::ToDial, EffectPriority::Normal, 0, DialLedTarget::Transfer);  // NOT_CONFIGURED
  for (Rig* r : {&plain, &lit}) {
    TEST_ASSERT_EQUAL(AckResult::Accepted, r->cmd(CommandCode::ReleaseBall, 7).result);
    r->run(3000);
  }
  TEST_ASSERT_EQUAL(plain.servo.writes.size(), lit.servo.writes.size());
  TEST_ASSERT_EQUAL(plain.em.events.back().elapsed_ms, lit.em.events.back().elapsed_ms);
  TEST_ASSERT_EQUAL(1, lit.em.count(EventCode::ReleaseDone));
}

void test_effects_cleared_by_safety_events() {
  {
    Rig r;  // STOP_ALL
    r.fx(DialEffect::Solid, DialDirection::None, EffectPriority::High);
    r.cmd(CommandCode::StopAll);
    TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
  }
  {
    Rig r;  // STOP_ALL during a release: effect off now, servo back to standby
    r.game();
    r.dropBall();
    r.fx(DialEffect::Comet, DialDirection::ToDial, EffectPriority::High);
    r.cmd(CommandCode::ReleaseBall, 5);
    r.run(100);
    r.cmd(CommandCode::StopAll, 6);
    TEST_ASSERT_FALSE(r.ring().from_pc);
    r.run(1000);
    TEST_ASSERT_EQUAL(1, r.em.count(EventCode::ReleaseFailed));
    TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
  }
  {
    Rig r;  // session change: effect and session colour gone
    uint8_t col[3] = {9, 8, 7};
    TEST_ASSERT_EQUAL(AckResult::Accepted, r.app.onCommand(CommandCode::SetDialColor, col, 3, 1, 0).result);
    r.fx(DialEffect::Solid);
    TEST_ASSERT_EQUAL(9, r.ring().r);
    r.app.onSessionChanged(0, r.now);
    TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
    uint8_t cr, cg, cb;
    r.app.unitColor(cr, cg, cb);
    TEST_ASSERT_EQUAL(0, cr);
    TEST_ASSERT_EQUAL(255, cg);
  }
  {
    Rig r;  // hub link lost: link display, effect dropped and not resumed
    r.fx(DialEffect::Solid, DialDirection::None, EffectPriority::High);
    r.app.onHubLinkChanged(false, r.now);
    TEST_ASSERT_EQUAL(LedScene::LinkLost, r.ring().scene);
    r.app.onHubLinkChanged(true, r.now);
    TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
  }
  {
    Rig r;  // settings mode: the person at the device owns the LEDs
    r.fx(DialEffect::Solid);
    r.app.enterSettings(r.now);
    TEST_ASSERT_EQUAL(LedScene::Settings, r.ring().scene);
    TEST_ASSERT_EQUAL(AckResult::RejectedState, r.fx(DialEffect::Solid).result);
    r.app.exitSettings(r.now);
    TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
  }
  {
    Rig r;  // configuration error: the error display is never covered
    r.app.setConfigError(ErrorCode::ServoNotConfigured);
    TEST_ASSERT_EQUAL(AckResult::RejectedState, r.fx(DialEffect::Solid).result);
    TEST_ASSERT_EQUAL(LedScene::Error, r.ring().scene);
  }
}

void test_set_dial_color() {
  Rig r;
  uint8_t magenta[3] = {255, 0, 255};
  TEST_ASSERT_EQUAL(AckResult::Accepted, r.app.onCommand(CommandCode::SetDialColor, magenta, 3, 1, 0).result);
  TEST_ASSERT_EQUAL(AckResult::InvalidParams, r.app.onCommand(CommandCode::SetDialColor, magenta, 2, 2, 0).result);
  r.game();
  TEST_ASSERT_EQUAL(255, r.ring().r);  // ARMED display in the unit colour
  r.sw = true;
  r.run(300);
  TEST_ASSERT_EQUAL(DialEffect::Chase, r.ring().effect);  // local receive chase in the unit colour
  TEST_ASSERT_EQUAL(255, r.ring().r);
  TEST_ASSERT_EQUAL(0, r.ring().g);
  uint8_t zero[3] = {0, 0, 0};  // back to the configured default
  r.app.onCommand(CommandCode::SetDialColor, zero, 3, 3, r.now);
  TEST_ASSERT_EQUAL(0, r.ring().r);
  TEST_ASSERT_EQUAL(255, r.ring().g);
}

void test_duplicate_effect_command_does_not_restart_animation() {
  Rig r;
  FakeNodeTransport tx;
  NodeConfig cfg;
  cfg.self_id = kSelf;
  memcpy(cfg.hub_mac, hubMac(), 6);
  NodeCore core(cfg, tx, r.app);
  core.begin(0, 1, 1);
  auto feed = [&](const std::vector<uint8_t>& f) { core.onFrame(hubMac(), f.data(), f.size(), 0, r.now); };
  SetDialEffectParams p;
  p.effect = static_cast<uint8_t>(DialEffect::Wipe);
  p.direction = static_cast<uint8_t>(DialDirection::ToDial);
  p.duration_ms = 1000;
  uint8_t pl[16];
  size_t n = encodeSetDialEffect(p, pl, sizeof(pl));
  auto frame = commandFrame(kSelf, 3, 20, pl, n);
  feed(frame);  // no session yet
  TEST_ASSERT_EQUAL(u8(AckResult::RejectedSession), ackOf(tx.last(MsgType::Ack)).result);
  TEST_ASSERT_EQUAL(LedScene::Idle, r.ring().scene);
  feed(simpleCommand(kSelf, 3, 0x80000001, CommandCode::SessionBegin));
  feed(frame);
  TEST_ASSERT_EQUAL(u8(AckResult::Accepted), ackOf(tx.last(MsgType::Ack)).result);
  r.run(400);
  feed(frame);  // retransmission (the ACK was lost)
  TEST_ASSERT_TRUE(ackOf(tx.last(MsgType::Ack)).flags & u8(AckFlag::Duplicate));
  TEST_ASSERT_EQUAL(400, r.ring().elapsed_ms);  // not restarted
  feed(commandFrame(kSelf, 9, 21, pl, n));  // a command of an old session
  TEST_ASSERT_EQUAL(u8(AckResult::RejectedSession), ackOf(tx.last(MsgType::Ack)).result);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_safe_state);
  RUN_TEST(test_ball_detection_once_per_cycle);
  RUN_TEST(test_chattering_filtered);
  RUN_TEST(test_ignored_outside_game);
  RUN_TEST(test_arm_while_switch_held_requires_release_first);
  RUN_TEST(test_release_sequence);
  RUN_TEST(test_release_again_while_the_ball_stays);
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
  RUN_TEST(test_led_path_geometry);
  RUN_TEST(test_led_direction_of_flow);
  RUN_TEST(test_dial_effect_accepted_and_shown);
  RUN_TEST(test_dial_effect_invalid_params);
  RUN_TEST(test_dial_effect_priority_and_duration);
  RUN_TEST(test_transfer_strip_optional);
  RUN_TEST(test_local_receive_animation);
  RUN_TEST(test_pc_effect_during_flash_waits_for_flash);
  RUN_TEST(test_release_animation_and_end);
  RUN_TEST(test_effects_never_change_the_release_sequence);
  RUN_TEST(test_effects_cleared_by_safety_events);
  RUN_TEST(test_set_dial_color);
  RUN_TEST(test_duplicate_effect_command_does_not_restart_animation);
  return UNITY_END();
}
