// Device protocol engine: MAC/ID filtering, sessions, dedup, heartbeat, events.
#include <unity.h>

#include "../support.h"

using namespace ufo;
using namespace testsupport;

void setUp() {}
void tearDown() {}

namespace {
const uint8_t kSelf = static_cast<uint8_t>(DeviceId::M5Dial1);

struct App : NodeApp {
  int executed = 0;
  CommandCode last = CommandCode::GetStatus;
  uint32_t session = 0xDEAD;
  int link_changes = 0;
  bool link = false;
  CommandOutcome onCommand(CommandCode c, const uint8_t*, size_t, uint32_t, uint32_t) override {
    ++executed;
    last = c;
    return CommandOutcome();
  }
  void onSessionChanged(uint32_t s, uint32_t) override { session = s; }
  void onHubLinkChanged(bool up, uint32_t) override {
    ++link_changes;
    link = up;
  }
  uint8_t state() const override { return 3; }
  uint8_t flags() const override { return 0; }
  void fillStatus(IlluminationStatus&, DialStatus& dl) override { dl.dial_state = 3; }
};

struct Fixture {
  App app;
  FakeNodeTransport tx;
  NodeConfig cfg;
  NodeCore* core;
  Fixture() {
    cfg.self_id = kSelf;
    cfg.device_type = DeviceType::Dial;
    memcpy(cfg.hub_mac, hubMac(), 6);
    cfg.event_policy.retry_forever = true;
    core = new NodeCore(cfg, tx, app);
    core->begin(0, 0x1111, 0x22222222);
  }
  ~Fixture() { delete core; }
  void feed(const std::vector<uint8_t>& f, uint32_t now = 0, const uint8_t* mac = nullptr) {
    core->onFrame(mac ? mac : hubMac(), f.data(), f.size(), -40, now);
  }
  void openSession(uint32_t s, uint32_t now = 0) {
    feed(simpleCommand(kSelf, s, 0x80000001, CommandCode::SessionBegin), now);
    core->tick(now);
  }
};
}  // namespace

void test_rejects_unknown_mac_and_broadcast_and_wrong_ids() {
  Fixture f;
  f.openSession(7);
  f.tx.clear();
  const uint8_t evil[6] = {0x24, 0x0A, 0xC4, 0x00, 0x00, 0x01};
  const uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  f.feed(simpleCommand(kSelf, 7, 10, CommandCode::ReleaseBall), 1, evil);
  f.feed(simpleCommand(kSelf, 7, 11, CommandCode::ReleaseBall), 1, bcast);
  f.feed(simpleCommand(0x21, 7, 12, CommandCode::ReleaseBall), 1);  // other dial
  f.feed(simpleCommand(kBroadcastId, 7, 13, CommandCode::ReleaseBall), 1);
  auto forged = simpleCommand(kSelf, 7, 14, CommandCode::ReleaseBall);
  forged[3] = 0x10;  // source id = illumination, but from the hub MAC
  f.feed(forged, 1);
  TEST_ASSERT_EQUAL(0, f.app.executed);
  TEST_ASSERT_EQUAL(0, f.tx.frames.size());  // nothing is even acknowledged
  TEST_ASSERT_EQUAL(2, f.core->counters().rx_bad_mac);
  TEST_ASSERT_EQUAL(3, f.core->counters().rx_bad_id);
}

void test_bad_magic_version_length() {
  Fixture f;
  auto c = simpleCommand(kSelf, 0, 10, CommandCode::StopAll);
  auto a = c;
  a[0] = 0;
  auto b = c;
  b[2] = 9;
  auto d = c;
  d.push_back(0);
  f.feed(a);
  f.feed(b);
  f.feed(d);
  TEST_ASSERT_EQUAL(0, f.app.executed);
  TEST_ASSERT_EQUAL(3, f.core->counters().rx_bad_frame);
}

void test_physical_command_requires_session() {
  Fixture f;
  f.feed(simpleCommand(kSelf, 0, 10, CommandCode::ArmReceiver));
  TEST_ASSERT_EQUAL(0, f.app.executed);
  TEST_ASSERT_EQUAL(u8(AckResult::RejectedSession), ackOf(f.tx.last(MsgType::Ack)).result);
  // safety commands work without a session
  f.feed(simpleCommand(kSelf, 0, 11, CommandCode::StopAll));
  TEST_ASSERT_EQUAL(1, f.app.executed);
  TEST_ASSERT_EQUAL(u8(AckResult::Accepted), ackOf(f.tx.last(MsgType::Ack)).result);
}

void test_session_begin_and_old_session_rejected() {
  Fixture f;
  f.openSession(100);
  TEST_ASSERT_EQUAL(100, f.app.session);
  TEST_ASSERT_TRUE(f.core->sessionValid());
  f.feed(simpleCommand(kSelf, 100, 20, CommandCode::ArmReceiver));
  TEST_ASSERT_EQUAL(1, f.app.executed);
  f.openSession(200);  // PC restarted
  TEST_ASSERT_EQUAL(200, f.app.session);
  f.feed(simpleCommand(kSelf, 100, 21, CommandCode::ReleaseBall));  // late command of the old session
  TEST_ASSERT_EQUAL(1, f.app.executed);
  TEST_ASSERT_EQUAL(u8(AckResult::RejectedSession), ackOf(f.tx.last(MsgType::Ack)).result);
  f.feed(simpleCommand(kSelf, 200, 0x80000009, CommandCode::SessionEnd));
  TEST_ASSERT_EQUAL(0, f.app.session);
}

void test_duplicate_command_not_executed_twice() {
  Fixture f;
  f.openSession(5);
  auto cmd = simpleCommand(kSelf, 5, 42, CommandCode::ReleaseBall);
  f.feed(cmd);
  f.feed(cmd);  // retransmission (ACK lost)
  f.feed(cmd);
  TEST_ASSERT_EQUAL(1, f.app.executed);
  auto acks = f.tx.packets(MsgType::Ack);
  TEST_ASSERT_EQUAL(4, acks.size());  // session begin + 3
  TEST_ASSERT_EQUAL(0, ackOf(acks[1]).flags & u8(AckFlag::Duplicate));
  TEST_ASSERT_TRUE(ackOf(acks[2]).flags & u8(AckFlag::Duplicate));
  TEST_ASSERT_EQUAL(42, acks[3].h.txn);
  TEST_ASSERT_EQUAL(2, f.core->counters().duplicates);
}

void test_invalid_payload_and_unknown_command() {
  Fixture f;
  f.openSession(5);
  uint8_t bad[2] = {static_cast<uint8_t>(CommandCode::ReleaseBall), 0x00};  // extra param
  f.feed(commandFrame(kSelf, 5, 50, bad, 2));
  TEST_ASSERT_EQUAL(u8(AckResult::InvalidParams), ackOf(f.tx.last(MsgType::Ack)).result);
  uint8_t unk[1] = {0x7E};
  f.feed(commandFrame(kSelf, 5, 51, unk, 1));
  TEST_ASSERT_EQUAL(u8(AckResult::Unsupported), ackOf(f.tx.last(MsgType::Ack)).result);
  TEST_ASSERT_EQUAL(0, f.app.executed);
}

void test_heartbeat_reply_and_session_follow() {
  Fixture f;
  f.openSession(9);
  f.feed(hubHeartbeat(kSelf, 9, 777), 10);
  Packet hb = f.tx.last(MsgType::Heartbeat);
  NodeHeartbeat nh;
  TEST_ASSERT_TRUE(decodeNodeHeartbeat(hb.payload, hb.h.payload_len, nh));
  TEST_ASSERT_EQUAL(777, nh.echo_seq);
  TEST_ASSERT_EQUAL(0x1111, nh.boot_id);
  TEST_ASSERT_EQUAL(9, hb.h.session);
  TEST_ASSERT_TRUE(nh.flags & u8(NodeFlag::SessionValid));
  // hub lost its session (e.g. hub rebooted): our session ends -> safe state
  f.feed(hubHeartbeat(kSelf, 0, 778), 20);
  TEST_ASSERT_EQUAL(0, f.app.session);
  TEST_ASSERT_FALSE(f.core->sessionValid());
}

void test_link_monitor_callbacks() {
  Fixture f;
  f.feed(hubHeartbeat(kSelf, 0, 1), 0);
  f.core->tick(0);
  TEST_ASSERT_TRUE(f.app.link);
  f.core->tick(2999);
  TEST_ASSERT_TRUE(f.app.link);
  f.core->tick(3000);
  TEST_ASSERT_FALSE(f.app.link);
  TEST_ASSERT_EQUAL(2, f.app.link_changes);
}

void test_event_retransmitted_with_same_id_until_ack() {
  Fixture f;
  f.openSession(5);
  f.tx.clear();
  EventData e;
  e.code = static_cast<uint8_t>(EventCode::BallReceived);
  e.cycle = 3;
  uint32_t id = f.core->emitEvent(e, 1000);
  TEST_ASSERT_NOT_EQUAL(0, id);
  for (uint32_t t = 1000; t < 3000; t += 10) f.core->tick(t);
  auto evs = f.tx.packets(MsgType::Event);
  TEST_ASSERT_TRUE(evs.size() >= 5);
  for (auto& p : evs) TEST_ASSERT_EQUAL(id, p.h.txn);
  // hub ACK
  AckPayload a;
  a.acked_type = static_cast<uint8_t>(MsgType::Event);
  uint8_t pl[8];
  size_t n = encodeAck(a, pl, sizeof(pl));
  f.feed(hubFrame(kSelf, MsgType::Ack, 5, id, pl, n), 3000);
  size_t before = f.tx.packets(MsgType::Event).size();
  for (uint32_t t = 3000; t < 5000; t += 10) f.core->tick(t);
  TEST_ASSERT_EQUAL(before, f.tx.packets(MsgType::Event).size());
  TEST_ASSERT_EQUAL(0, f.core->pendingEvents());
}

void test_events_not_sent_without_session_and_purged_on_session_change() {
  Fixture f;
  EventData e;
  e.code = static_cast<uint8_t>(EventCode::BallReceived);
  TEST_ASSERT_EQUAL(0, f.core->emitEvent(e, 0));
  f.openSession(5);
  TEST_ASSERT_NOT_EQUAL(0, f.core->emitEvent(e, 0));
  TEST_ASSERT_EQUAL(1, f.core->pendingEvents());
  f.openSession(6);
  TEST_ASSERT_EQUAL(0, f.core->pendingEvents());  // old-session event is never delivered
}

void test_get_status_reply() {
  Fixture f;
  f.feed(simpleCommand(kSelf, 0, 70, CommandCode::GetStatus));
  Packet st = f.tx.last(MsgType::Status);
  StatusCommon c;
  IlluminationStatus il;
  DialStatus dl;
  TEST_ASSERT_TRUE(decodeStatus(st.payload, st.h.payload_len, c, il, dl));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DeviceType::Dial), c.device_type);
  TEST_ASSERT_EQUAL(3, dl.dial_state);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rejects_unknown_mac_and_broadcast_and_wrong_ids);
  RUN_TEST(test_bad_magic_version_length);
  RUN_TEST(test_physical_command_requires_session);
  RUN_TEST(test_session_begin_and_old_session_rejected);
  RUN_TEST(test_duplicate_command_not_executed_twice);
  RUN_TEST(test_invalid_payload_and_unknown_command);
  RUN_TEST(test_heartbeat_reply_and_session_follow);
  RUN_TEST(test_link_monitor_callbacks);
  RUN_TEST(test_event_retransmitted_with_same_id_until_ack);
  RUN_TEST(test_events_not_sent_without_session_and_purged_on_session_change);
  RUN_TEST(test_get_status_reply);
  return UNITY_END();
}
