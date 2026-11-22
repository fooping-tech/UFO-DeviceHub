// Staff remote (M5StopWatch): staff actions, the input stream, tilt shaping, safety.
#include <math.h>
#include <unity.h>

#include "../support.h"
#include "staff_remote/remote_controller.h"

using namespace ufo;
using namespace testsupport;

void setUp() {}
void tearDown() {}

namespace {

struct Rig {
  RemoteConfig cfg;
  RemoteController app{cfg};
  FakeEmitter em;
  uint32_t now = 1000;
  Rig() {
    app.setEmitter(&em);
    app.onHubLinkChanged(true, now);
  }
  // flat on the table: the accelerometer reads +1 g on z
  void flat(bool move, uint32_t dt = 10) { tilt(move, 0.0f, 0.0f, dt); }
  // tipped by `right_deg` (right edge down) and `fwd_deg` (top edge down)
  void tilt(bool move, float right_deg, float fwd_deg, uint32_t dt = 10) {
    now += dt;
    const float r = right_deg / 57.2957795f, f = fwd_deg / 57.2957795f;
    app.tick(move, -sinf(r), -sinf(f), cosf(r) * cosf(f), now);
  }
  size_t inputs() const { return em.count(EventCode::RemoteInput); }
  const EventData& last() const { return em.events.back(); }
};

}  // namespace

void test_shape() {
  TEST_ASSERT_EQUAL_INT8(0, RemoteController::shape(3.9f, 4, 25));
  TEST_ASSERT_EQUAL_INT8(0, RemoteController::shape(-4.0f, 4, 25));
  TEST_ASSERT_EQUAL_INT8(100, RemoteController::shape(25.0f, 4, 25));
  TEST_ASSERT_EQUAL_INT8(100, RemoteController::shape(60.0f, 4, 25));
  TEST_ASSERT_EQUAL_INT8(-100, RemoteController::shape(-40.0f, 4, 25));
  TEST_ASSERT_EQUAL_INT8(50, RemoteController::shape(14.5f, 4, 25));
  TEST_ASSERT_EQUAL_INT8(1, RemoteController::shape(4.01f, 4, 25));  // out of the dead zone: never 0
  TEST_ASSERT_EQUAL_INT8(0, RemoteController::shape(NAN, 4, 25));
}

void test_tilt_axes() {
  RemoteConfig c;
  float r, f;
  RemoteController::tiltDeg(c, -0.5f, 0.0f, 0.866f, r, f);  // right edge down 30 deg
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, r);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, f);
  RemoteController::tiltDeg(c, 0.0f, -0.5f, 0.866f, r, f);  // top edge down: forward
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, f);
  c.swap_xy = true;
  c.invert_x = true;
  RemoteController::tiltDeg(c, 0.0f, -0.5f, 0.866f, r, f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -30.0f, r);
  RemoteController::tiltDeg(c, 0.0f, 0.0f, 0.0f, r, f);  // no IMU reading: level
  TEST_ASSERT_EQUAL_FLOAT(0.0f, r);
}

void test_staff_action_event() {
  Rig t;
  TEST_ASSERT_NOT_EQUAL(0, t.app.staffAction(StaffAction::Next, t.now));
  TEST_ASSERT_EQUAL(1, t.em.count(EventCode::StaffAction));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StaffAction::Next), t.last().reason);
  t.em.accept = false;  // no session
  TEST_ASSERT_EQUAL(0, t.app.staffAction(StaffAction::Skip, t.now));
}

void test_idle_sends_nothing() {
  Rig t;
  for (int i = 0; i < 100; ++i) t.tilt(false, 20, 10);  // tilted, button up: no stick
  TEST_ASSERT_EQUAL(0, t.inputs());
  TEST_ASSERT_FALSE(t.app.streaming());
}

void test_move_streams_relative_tilt_then_closes() {
  Rig t;
  t.tilt(false, 10, 5);
  t.tilt(true, 10, 5);  // pressed while already tilted: that is the centre
  TEST_ASSERT_EQUAL(1, t.inputs());
  TEST_ASSERT_TRUE(t.last().buttons & u8(RemoteButton::Move));
  TEST_ASSERT_EQUAL_INT8(0, t.last().tilt_x);
  TEST_ASSERT_EQUAL_INT8(0, t.last().tilt_y);
  TEST_ASSERT_EQUAL_UINT16(1, t.last().frame);
  for (int i = 0; i < 9; ++i) t.tilt(true, 35, 5);  // 25 deg more to the right
  TEST_ASSERT_EQUAL(1, t.inputs());                  // one frame per frame_ms
  t.tilt(true, 35, 5);                               // 100 ms after the first
  TEST_ASSERT_EQUAL(2, t.inputs());
  TEST_ASSERT_EQUAL_INT8(100, t.last().tilt_x);
  TEST_ASSERT_EQUAL_INT8(0, t.last().tilt_y);
  TEST_ASSERT_EQUAL_UINT16(2, t.last().frame);
  TEST_ASSERT_TRUE(t.app.streaming());
  t.tilt(false, 35, 5);  // let go: one closing frame at once
  TEST_ASSERT_EQUAL(3, t.inputs());
  TEST_ASSERT_EQUAL_UINT8(0, t.last().buttons);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RemotePose::Open), t.last().pose);
  for (int i = 0; i < 50; ++i) t.flat(false);
  TEST_ASSERT_EQUAL(3, t.inputs());  // then nothing
  TEST_ASSERT_FALSE(t.app.streaming());
}

void test_z_mode_flag() {
  Rig t;
  t.app.setZMode(true);
  t.flat(true);
  TEST_ASSERT_TRUE(t.last().buttons & u8(RemoteButton::AxisZ));
  t.flat(false);
  TEST_ASSERT_FALSE(t.last().buttons & u8(RemoteButton::AxisZ));  // the closing frame: nothing held
}

void test_pose_shown_then_open_then_closed() {
  Rig t;
  t.app.showPose(RemotePose::Fist, t.now);
  t.flat(false);
  TEST_ASSERT_EQUAL(1, t.inputs());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RemotePose::Fist), t.last().pose);
  TEST_ASSERT_EQUAL_UINT8(0, t.last().buttons);
  for (int i = 0; i < 119; ++i) t.flat(false);  // 1200 ms of fist
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RemotePose::Fist), t.last().pose);
  size_t fist_frames = t.inputs();
  TEST_ASSERT_TRUE(fist_frames >= 12);
  for (int i = 0; i < 40; ++i) t.flat(false);  // 400 ms of the open hand
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(RemotePose::Open), t.last().pose);
  TEST_ASSERT_TRUE(t.app.streaming() || t.inputs() > fist_frames);
  for (int i = 0; i < 20; ++i) t.flat(false);
  TEST_ASSERT_FALSE(t.app.streaming());
  size_t n = t.inputs();
  for (int i = 0; i < 20; ++i) t.flat(false);
  TEST_ASSERT_EQUAL(n, t.inputs());
}

void test_latest_input_wins_in_the_queue() {
  Rig t;
  t.em.hold = true;  // nothing is acknowledged: the link is stalled
  for (int i = 0; i < 60; ++i) t.tilt(true, 30, 0);
  TEST_ASSERT_TRUE(t.inputs() >= 5);  // frames keep coming ...
  TEST_ASSERT_EQUAL(1, t.em.queued.size());  // ... but only the latest waits in the queue
  TEST_ASSERT_TRUE(t.app.framesDropped() >= 4);
  t.app.staffAction(StaffAction::Next, t.now);  // a staff action is never dropped
  t.tilt(true, 30, 0, 100);
  TEST_ASSERT_EQUAL(2, t.em.queued.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventCode::StaffAction), t.em.queued[0]);
  t.flat(false);  // let go: the closing frame replaces the queued movement
  TEST_ASSERT_EQUAL(2, t.em.queued.size());
  TEST_ASSERT_EQUAL_UINT8(0, t.last().buttons);
  t.em.backlog = 4;  // far behind other events: a frame waits instead of piling up
  t.flat(true);
  size_t n = t.inputs();
  t.flat(true, 100);
  TEST_ASSERT_EQUAL(n, t.inputs());
  TEST_ASSERT_TRUE(t.app.framesSkipped() > 0);
}

void test_halt_drops_queued_input() {
  Rig t;
  t.em.hold = true;
  t.flat(true);
  t.app.staffAction(StaffAction::Skip, t.now);
  TEST_ASSERT_EQUAL(2, t.em.queued.size());
  t.app.onCommand(CommandCode::StopAll, nullptr, 0, 1, t.now);  // STOP_ALL: no input may arrive later
  TEST_ASSERT_EQUAL(1, t.em.queued.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EventCode::StaffAction), t.em.queued[0]);
  t.flat(true);
  t.app.onHubLinkChanged(false, t.now);
  TEST_ASSERT_EQUAL(1, t.em.queued.size());
}

// Review on #14, with the real NodeCore: input frames never acknowledged, then STOP_ALL. No
// movement frame is transmitted after it, however long the outbox keeps retrying.
void test_nodecore_never_retransmits_input_after_stop_all() {
  RemoteConfig cfg;
  RemoteController app(cfg);
  FakeNodeTransport tx;
  NodeConfig nc;
  nc.self_id = static_cast<uint8_t>(DeviceId::StaffRemote);
  nc.device_type = DeviceType::StaffRemote;
  memcpy(nc.hub_mac, hubMac(), 6);
  nc.event_policy.retry_forever = true;
  NodeCore core(nc, tx, app);
  app.setEmitter(&core);
  core.begin(0, 0x1111, 0x2222);
  std::vector<uint8_t> f = simpleCommand(nc.self_id, 7, 0x80000001, CommandCode::SessionBegin);
  core.onFrame(hubMac(), f.data(), f.size(), -40, 0);
  core.tick(0);
  uint32_t now = 0;
  for (int i = 0; i < 50; ++i) {  // 500 ms of holding the key, the hub never ACKs
    now += 10;
    app.tick(true, -0.5f, 0.0f, 0.866f, now);
    core.tick(now);
  }
  TEST_ASSERT_TRUE(core.pendingEvents() <= 1);  // only the latest frame waits
  f = simpleCommand(nc.self_id, 7, 0x80000002, CommandCode::StopAll);
  core.onFrame(hubMac(), f.data(), f.size(), -40, now);
  TEST_ASSERT_EQUAL(0, core.pendingEvents());
  tx.clear();
  for (int i = 0; i < 300; ++i) {  // 3 s more of retries, the key still held (latched)
    now += 10;
    app.tick(true, -0.5f, 0.0f, 0.866f, now);
    core.tick(now);
  }
  size_t inputs = 0;
  for (const Packet& p : tx.packets(MsgType::Event))
    if (p.payload[0] == static_cast<uint8_t>(EventCode::RemoteInput)) ++inputs;
  TEST_ASSERT_EQUAL(0, inputs);
}

void test_stop_latches_move_until_released() {
  Rig t;
  t.flat(true);
  TEST_ASSERT_TRUE(t.app.streaming());
  t.app.staffAction(StaffAction::Stop, t.now);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(StaffAction::Stop), t.last().reason);
  size_t n = t.inputs();
  for (int i = 0; i < 30; ++i) t.tilt(true, 30, 0);  // still held: nothing
  TEST_ASSERT_EQUAL(n, t.inputs());
  TEST_ASSERT_TRUE(t.app.moveLatched());
  t.flat(false);
  TEST_ASSERT_EQUAL(n, t.inputs());  // no closing frame after a stop
  t.flat(true);                      // pressed again: streams
  TEST_ASSERT_EQUAL(n + 1, t.inputs());
}

void test_stop_all_session_and_link_halt() {
  Rig t;
  t.flat(true);
  CommandOutcome o = t.app.onCommand(CommandCode::StopAll, nullptr, 0, 1, t.now);
  TEST_ASSERT_EQUAL(AckResult::Accepted, o.result);
  TEST_ASSERT_FALSE(t.app.streaming());
  TEST_ASSERT_TRUE(t.app.moveLatched());
  t.flat(false);
  t.app.showPose(RemotePose::Peace, t.now);
  t.flat(false);
  TEST_ASSERT_TRUE(t.app.streaming());
  t.app.onSessionChanged(0, t.now);
  TEST_ASSERT_FALSE(t.app.streaming());
  t.flat(true);
  t.flat(true);
  t.app.onHubLinkChanged(false, t.now);
  TEST_ASSERT_FALSE(t.app.streaming());
  TEST_ASSERT_EQUAL(AckResult::Unsupported, t.app.onCommand(CommandCode::ReleaseBall, nullptr, 0, 2, t.now).result);
}

void test_event_codec_roundtrip() {
  EventData e;
  e.code = static_cast<uint8_t>(EventCode::RemoteInput);
  e.buttons = 3;
  e.tilt_x = -100;
  e.tilt_y = 42;
  e.pose = 3;
  e.frame = 0xBEEF;
  uint8_t buf[16];
  size_t n = encodeEvent(e, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(7, n);
  EventData d;
  TEST_ASSERT_TRUE(decodeEvent(buf, n, d));
  TEST_ASSERT_EQUAL_INT8(-100, d.tilt_x);
  TEST_ASSERT_EQUAL_INT8(42, d.tilt_y);
  TEST_ASSERT_EQUAL_UINT16(0xBEEF, d.frame);
  TEST_ASSERT_FALSE(decodeEvent(buf, n - 1, d));
  TEST_ASSERT_EQUAL_STRING("RECOVER", staffActionName(4));
  TEST_ASSERT_NULL(staffActionName(9));
  TEST_ASSERT_EQUAL_STRING("POINT", remotePoseName(3));
  // STATUS of the remote: the common part only
  StatusCommon c;
  c.device_type = static_cast<uint8_t>(DeviceType::StaffRemote);
  c.state = 1;
  uint8_t sb[32];
  size_t sn = encodeStatus(c, nullptr, nullptr, sb, sizeof(sb));
  TEST_ASSERT_EQUAL(kStatusCommonSize, sn);
  StatusCommon c2;
  IlluminationStatus il;
  DialStatus dl;
  TEST_ASSERT_TRUE(decodeStatus(sb, sn, c2, il, dl));
  TEST_ASSERT_EQUAL_UINT8(1, c2.state);
}

void test_registry_has_remote() {
  const DeviceEntry* d = findDevice(static_cast<uint8_t>(DeviceId::StaffRemote));
  TEST_ASSERT_NOT_NULL(d);
  TEST_ASSERT_EQUAL(DeviceType::StaffRemote, d->type);
  TEST_ASSERT_EQUAL_STRING("staff_remote", d->name);
  TEST_ASSERT_EQUAL(5, deviceCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_shape);
  RUN_TEST(test_tilt_axes);
  RUN_TEST(test_staff_action_event);
  RUN_TEST(test_idle_sends_nothing);
  RUN_TEST(test_move_streams_relative_tilt_then_closes);
  RUN_TEST(test_z_mode_flag);
  RUN_TEST(test_pose_shown_then_open_then_closed);
  RUN_TEST(test_latest_input_wins_in_the_queue);
  RUN_TEST(test_halt_drops_queued_input);
  RUN_TEST(test_nodecore_never_retransmits_input_after_stop_all);
  RUN_TEST(test_stop_latches_move_until_released);
  RUN_TEST(test_stop_all_session_and_link_halt);
  RUN_TEST(test_event_codec_roundtrip);
  RUN_TEST(test_registry_has_remote);
  return UNITY_END();
}
