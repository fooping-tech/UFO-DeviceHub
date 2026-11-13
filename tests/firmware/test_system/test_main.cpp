// End-to-end (in memory): PC JSON -> Hub -> ESP-NOW frames -> 3 devices and back,
// with ACK loss, device power loss, PC loss, duplicate events, foreign frames.
#include <unity.h>

#include "../sim.h"

using namespace sim;

void setUp() {}
void tearDown() {}

static const uint8_t ILL = static_cast<uint8_t>(DeviceId::Illumination);
static const uint8_t D1 = static_cast<uint8_t>(DeviceId::M5Dial1);
static const uint8_t D2 = static_cast<uint8_t>(DeviceId::M5Dial2);

static void ready(World& w) {
  w.hello(4242);
  w.runAlive(1500);
}

static bool isAckFrom(const Air::Msg& m, DeviceId id) {
  if (!m.to_hub || !macEqual(m.mac, macOf(id))) return false;
  Packet p;
  return decodePacket(m.bytes.data(), m.bytes.size(), p) == DecodeError::Ok && p.h.type == MsgType::Ack;
}

void test_individual_control_of_three_devices() {
  World w;
  ready(w);
  TEST_ASSERT_EQUAL(3, w.out.ofType("LINK").size());
  TEST_ASSERT_TRUE(w.illum.core.sessionValid() && w.dial1.core.sessionValid() && w.dial2.core.sessionValid());

  uint32_t a = w.command(ILL, "SET_LIGHT_MODE", "{\"mode\":\"GAME_PLAY\",\"brightness\":180,\"transition_ms\":300}");
  uint32_t b = w.command(D1, "SET_MODE", "{\"mode\":\"GAME\"}");
  w.runAlive(50);
  TEST_ASSERT_EQUAL_STRING("ACCEPTED", w.ackFor(a)["result"]);
  TEST_ASSERT_EQUAL_STRING("device", w.ackFor(a)["origin"]);
  TEST_ASSERT_EQUAL_STRING("ACCEPTED", w.ackFor(b)["result"]);
  TEST_ASSERT_EQUAL(LightMode::GamePlay, w.illum.app.lighting().mode().mode);
  TEST_ASSERT_EQUAL(180, w.illum.app.lighting().mode().brightness);
  TEST_ASSERT_EQUAL(DialState::Ready, w.dial1.app.dialState());
  TEST_ASSERT_EQUAL(DialState::Disabled, w.dial2.app.dialState());  // only #1 was addressed

  uint32_t c = w.command(D2, "ARM_RECEIVER");
  w.runAlive(50);
  TEST_ASSERT_EQUAL_STRING("REJECTED_STATE", w.ackFor(c)["result"]);  // #2 not in GAME mode

  uint32_t s = w.command(ILL, "GET_STATUS");
  w.runAlive(50);
  TEST_ASSERT_EQUAL_STRING("ACCEPTED", w.ackFor(s)["result"]);
  TEST_ASSERT_EQUAL_STRING("GAME_PLAY", w.out.lastOf("STATUS")["light_mode"]);
}

void test_steam_relay_pulse_and_done_event() {
  World w;
  ready(w);
  uint32_t id = w.command(ILL, "RELAY_PULSE", "{\"relay_id\":0,\"duration_ms\":800}");
  w.runAlive(20);
  TEST_ASSERT_EQUAL_STRING("ACCEPTED", w.ackFor(id)["result"]);
  TEST_ASSERT_TRUE(w.illum.gpio.on);
  w.runAlive(800);
  TEST_ASSERT_FALSE(w.illum.gpio.on);
  auto done = w.events("RELAY_DONE");
  TEST_ASSERT_EQUAL(1, done.size());
  TEST_ASSERT_INT_WITHIN(15, 800, done[0]["data"]["on_ms"].as<int>());
  uint32_t id2 = w.command(ILL, "RELAY_PULSE", "{\"duration_ms\":9000}");
  w.runAlive(20);
  TEST_ASSERT_EQUAL_STRING("COOLDOWN", w.ackFor(id2)["result"]);
}

void test_ball_cycle_events_and_release() {
  World w;
  ready(w);
  w.command(D1, "SET_MODE", "{\"mode\":\"GAME\"}");
  w.command(D1, "ARM_RECEIVER");
  w.runAlive(50);
  w.dial1.sw = true;
  w.runAlive(100);
  auto got = w.events("BALL_RECEIVED");
  TEST_ASSERT_EQUAL(1, got.size());
  TEST_ASSERT_EQUAL(D1, got[0]["source"].as<int>());
  w.ackEvent(got[0]);
  w.runAlive(2000);
  TEST_ASSERT_EQUAL(1, w.events("BALL_RECEIVED").size());  // acked -> no resend

  uint32_t rel = w.command(D1, "RELEASE_BALL");
  w.runAlive(50);
  TEST_ASSERT_EQUAL_STRING("ACCEPTED", w.ackFor(rel)["result"]);
  TEST_ASSERT_EQUAL(1, w.events("RELEASE_STARTED").size());
  w.runAlive(2500);
  auto done = w.events("RELEASE_DONE");
  TEST_ASSERT_TRUE(done.size() >= 1);
  TEST_ASSERT_EQUAL(rel, done[0]["data"]["command_id"].as<uint32_t>());
  TEST_ASSERT_EQUAL(1, w.dial1.servo.count(0));
  TEST_ASSERT_EQUAL(DialState::Complete, w.dial1.app.dialState());
  TEST_ASSERT_EQUAL(0, w.dial2.servo.count(0));
}

void test_unacked_pc_event_is_resent_with_same_id() {
  World w;
  ready(w);
  w.command(D2, "SET_MODE", "{\"mode\":\"GAME\"}");
  w.command(D2, "ARM_RECEIVER");
  w.runAlive(50);
  w.dial2.sw = true;
  w.runAlive(1600);  // PC does not ACK
  auto evs = w.events("BALL_RECEIVED");
  TEST_ASSERT_TRUE(evs.size() >= 3);
  for (auto& e : evs) TEST_ASSERT_EQUAL(evs[0]["event_id"].as<uint32_t>(), e["event_id"].as<uint32_t>());
}

void test_ack_loss_is_recovered_without_double_dispense() {
  World w;
  ready(w);
  w.command(D1, "SET_MODE", "{\"mode\":\"GAME\"}");
  w.command(D1, "ARM_RECEIVER");
  w.runAlive(50);
  w.dial1.sw = true;
  w.runAlive(100);
  w.ackEvent(w.events("BALL_RECEIVED")[0]);
  int to_drop = 2;  // the first two ACKs from dial #1 are lost
  w.air.drop = [&](const Air::Msg& m) { return isAckFrom(m, DeviceId::M5Dial1) && to_drop-- > 0; };
  uint32_t rel = w.command(D1, "RELEASE_BALL");
  w.runAlive(500);
  JsonDocument ack = w.ackFor(rel);
  TEST_ASSERT_EQUAL_STRING("ACCEPTED", ack["result"]);
  TEST_ASSERT_TRUE(ack["duplicate"].as<bool>());  // answered from the dedup cache
  TEST_ASSERT_EQUAL(3, ack["attempts"].as<int>());
  w.runAlive(2500);
  TEST_ASSERT_EQUAL(1, w.dial1.servo.count(0));  // dispensed exactly once
}

void test_retry_limit_reports_timeout() {
  World w;
  ready(w);
  w.air.drop = [&](const Air::Msg& m) { return isAckFrom(m, DeviceId::Illumination); };
  uint32_t id = w.command(ILL, "TRIGGER_EFFECT", "{\"effect\":\"SCORE\"}");
  w.runAlive(600);
  TEST_ASSERT_EQUAL_STRING("TIMEOUT", w.ackFor(id)["result"]);
  w.pc("{\"type\":\"GET_DIAG\"}");
  JsonDocument g = w.out.lastOf("DIAG");
  TEST_ASSERT_EQUAL(1, g["devices"][0]["ack_timeouts"].as<int>());
  TEST_ASSERT_EQUAL(3, g["devices"][0]["retransmissions"].as<int>());
}

void test_device_power_loss_and_recovery() {
  World w;
  ready(w);
  w.dial2.powered = false;
  w.runAlive(3500);
  JsonDocument link = w.out.lastOf("LINK");
  TEST_ASSERT_EQUAL(D2, link["source"].as<int>());
  TEST_ASSERT_FALSE(link["online"].as<bool>());
  uint32_t id = w.command(D2, "SET_MODE", "{\"mode\":\"GAME\"}");
  TEST_ASSERT_EQUAL_STRING("OFFLINE", w.ackFor(id)["result"]);
  // power back: fresh boot, new boot id
  w.dial2.powered = true;
  w.dial2.boot(w.now, 0x999);
  w.runAlive(2000);
  TEST_ASSERT_TRUE(w.out.lastOf("LINK")["online"].as<bool>());
  TEST_ASSERT_EQUAL(1, w.out.ofType("DEVICE_REBOOTED").size());
  TEST_ASSERT_TRUE(w.dial2.core.sessionValid());  // session re-synchronised
  TEST_ASSERT_EQUAL(DialState::Disabled, w.dial2.app.dialState());
  uint32_t id2 = w.command(D2, "SET_MODE", "{\"mode\":\"GAME\"}");
  w.runAlive(50);
  TEST_ASSERT_EQUAL_STRING("ACCEPTED", w.ackFor(id2)["result"]);
}

void test_pc_usb_disconnect_makes_devices_safe() {
  World w;
  ready(w);
  w.command(ILL, "RELAY_PULSE", "{\"duration_ms\":3000}");
  w.command(D1, "SET_MODE", "{\"mode\":\"GAME\"}");
  w.command(D1, "ARM_RECEIVER");
  w.runAlive(50);
  TEST_ASSERT_TRUE(w.illum.gpio.on);
  // PC disappears while the steam is on: no JSON lines any more
  uint32_t t0 = w.now;
  while (w.hub.session() != 0 && w.now - t0 < 5000) w.step(5);
  TEST_ASSERT_EQUAL(0, w.hub.session());
  TEST_ASSERT_TRUE(w.now - t0 <= 3100);
  w.run(100);  // SESSION_END reaches the devices
  TEST_ASSERT_FALSE(w.illum.gpio.on);
  TEST_ASSERT_FALSE(w.illum.core.sessionValid());
  TEST_ASSERT_EQUAL(DialState::Disabled, w.dial1.app.dialState());
  TEST_ASSERT_EQUAL(OperationMode::Standby, w.dial1.app.operationMode());
  // a new PC session never replays old commands
  w.hello(5151);
  w.runAlive(1500);
  TEST_ASSERT_EQUAL(1, w.illum.gpio.pulses);
}

void test_old_session_commands_not_replayed_after_reconnect() {
  World w;
  ready(w);
  w.air.drop = [&](const Air::Msg& m) { return !m.to_hub && macEqual(m.mac, macOf(DeviceId::Illumination)); };
  w.command(ILL, "RELAY_PULSE", "{\"duration_ms\":500}");  // stuck in the retry queue
  w.step(5);
  w.hello(999);  // PC restarted before the command got through
  w.air.drop = [](const Air::Msg&) { return false; };
  w.runAlive(2000);
  TEST_ASSERT_EQUAL(0, w.illum.gpio.pulses);
}

void test_foreign_broadcast_frames_do_nothing() {
  World w;
  ready(w);
  const uint8_t other[6] = {0x24, 0x6F, 0x28, 0x11, 0x22, 0x33};
  // well-formed RELEASE_BALL / RELAY_PULSE frames from another exhibit's MAC
  for (int i = 0; i < 200; ++i) {
    auto f = simpleCommand(D1, w.session, 5000 + i, CommandCode::ReleaseBall);
    w.dial1.core.onFrame(other, f.data(), f.size(), 0, w.now);
    uint8_t pl[4] = {0x12, 0, 0xE8, 0x03};
    auto g = commandFrame(ILL, w.session, 6000 + i, pl, 4);
    w.illum.core.onFrame(other, g.data(), g.size(), 0, w.now);
    uint8_t junk[5] = {3, 1, 1, 0, static_cast<uint8_t>(i)};  // NeoGacha style frame
    w.hub.onFrame(other, junk, 5, 0, w.now);
    w.step(1);
  }
  TEST_ASSERT_EQUAL(0, w.illum.gpio.pulses);
  TEST_ASSERT_EQUAL(0, w.dial1.servo.count(0));
  TEST_ASSERT_EQUAL(200, w.dial1.core.counters().rx_bad_mac);
  TEST_ASSERT_EQUAL(200, w.hub.counters().rx_unknown_mac);
}

void test_stop_all_always_accepted() {
  World w;
  ready(w);
  w.command(ILL, "RELAY_PULSE", "{\"duration_ms\":2000}");
  w.runAlive(50);
  uint32_t id = w.command(ILL, "STOP_ALL");
  w.runAlive(20);
  TEST_ASSERT_EQUAL_STRING("ACCEPTED", w.ackFor(id)["result"]);
  TEST_ASSERT_FALSE(w.illum.gpio.on);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_individual_control_of_three_devices);
  RUN_TEST(test_steam_relay_pulse_and_done_event);
  RUN_TEST(test_ball_cycle_events_and_release);
  RUN_TEST(test_unacked_pc_event_is_resent_with_same_id);
  RUN_TEST(test_ack_loss_is_recovered_without_double_dispense);
  RUN_TEST(test_retry_limit_reports_timeout);
  RUN_TEST(test_device_power_loss_and_recovery);
  RUN_TEST(test_pc_usb_disconnect_makes_devices_safe);
  RUN_TEST(test_old_session_commands_not_replayed_after_reconnect);
  RUN_TEST(test_foreign_broadcast_frames_do_nothing);
  RUN_TEST(test_stop_all_always_accepted);
  return UNITY_END();
}
