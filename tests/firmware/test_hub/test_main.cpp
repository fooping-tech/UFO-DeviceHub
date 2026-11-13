// Hub: JSON Lines validation, routing errors, session handling, diagnostics.
#include <unity.h>

#include "../sim.h"

using namespace sim;

void setUp() {}
void tearDown() {}

static const uint8_t ILL = static_cast<uint8_t>(DeviceId::Illumination);
static const uint8_t D1 = static_cast<uint8_t>(DeviceId::M5Dial1);

static void online(World& w) {
  w.run(1100);  // first heartbeat round trip
}

void test_bad_lines() {
  World w;
  w.pc("{not json");
  TEST_ASSERT_EQUAL_STRING("BAD_JSON", w.out.lastOf("ERROR")["code"]);
  w.pc("[1,2]");
  TEST_ASSERT_EQUAL_STRING("BAD_JSON", w.out.lastOf("ERROR")["code"]);
  w.pc("{\"protocol_version\":2,\"type\":\"HELLO\",\"session_id\":5}");
  TEST_ASSERT_EQUAL_STRING("UNSUPPORTED_VERSION", w.out.lastOf("ERROR")["code"]);
  w.pc("{\"type\":\"FOO\"}");
  TEST_ASSERT_EQUAL_STRING("UNKNOWN_TYPE", w.out.lastOf("ERROR")["code"]);
  w.bridge.reportLineTooLong(512);
  TEST_ASSERT_EQUAL_STRING("LINE_TOO_LONG", w.out.lastOf("ERROR")["code"]);
  w.pc("{\"type\":\"HELLO\",\"session_id\":0}");
  TEST_ASSERT_EQUAL_STRING("BAD_REQUEST", w.out.lastOf("ERROR")["code"]);
  TEST_ASSERT_EQUAL(0, w.hub.session());
}

void test_hello_ack() {
  World w;
  w.hello(1234);
  JsonDocument d = w.out.lastOf("HELLO_ACK");
  TEST_ASSERT_EQUAL(1234, d["session_id"].as<uint32_t>());
  TEST_ASSERT_EQUAL(1, d["protocol_version"].as<int>());
  TEST_ASSERT_FALSE(d["hub_mac"].isNull());
}

void test_command_validation() {
  World w;
  online(w);
  uint32_t id = w.command(ILL, "SET_LIGHT_MODE", "{\"mode\":\"IDLE\"}");
  TEST_ASSERT_EQUAL_STRING("NO_SESSION", w.ackFor(id)["result"]);
  w.hello(77);
  w.run(50);
  id = w.command(ILL, "RELAY_PULSE", "{}");
  TEST_ASSERT_EQUAL_STRING("BAD_REQUEST", w.ackFor(id)["result"]);
  TEST_ASSERT_EQUAL_STRING("hub", w.ackFor(id)["origin"]);
  id = w.command(ILL, "SET_LIGHT_MODE", "{\"mode\":\"DISCO\"}");
  TEST_ASSERT_EQUAL_STRING("BAD_REQUEST", w.ackFor(id)["result"]);
  id = w.command(ILL, "SET_LIGHT_MODE", "{\"mode\":\"CUSTOM\",\"color\":[1,2]}");
  TEST_ASSERT_EQUAL_STRING("BAD_REQUEST", w.ackFor(id)["result"]);
  id = w.command(ILL, "SET_LIGHT_MODE", "{\"mode\":\"IDLE\",\"brightness\":300}");
  TEST_ASSERT_EQUAL_STRING("BAD_REQUEST", w.ackFor(id)["result"]);
  id = w.command(ILL, "DANCE");
  TEST_ASSERT_EQUAL_STRING("BAD_REQUEST", w.ackFor(id)["result"]);
  id = w.command(0x42, "STOP_ALL");
  TEST_ASSERT_EQUAL_STRING("UNKNOWN_DEVICE", w.ackFor(id)["result"]);
  // hub-only commands cannot be injected by the PC
  id = w.command(D1, "SESSION_BEGIN");
  TEST_ASSERT_EQUAL_STRING("BAD_REQUEST", w.ackFor(id)["result"]);
  // wrong session
  w.pc("{\"type\":\"COMMAND\",\"destination\":16,\"session_id\":1,\"command_id\":999,\"name\":\"SET_LIGHT_MODE\","
       "\"params\":{\"mode\":\"IDLE\"}}");
  TEST_ASSERT_EQUAL_STRING("SESSION_MISMATCH", w.ackFor(999)["result"]);
  // reserved (hub) command id range
  w.pc("{\"type\":\"COMMAND\",\"destination\":16,\"session_id\":77,\"command_id\":2147483649,\"name\":\"STOP_ALL\"}");
  TEST_ASSERT_EQUAL_STRING("BAD_REQUEST", w.ackFor(2147483649u)["result"]);
}

void test_frames_from_unknown_mac_or_wrong_id_ignored() {
  World w;
  online(w);
  const uint8_t evil[6] = {0x30, 0xAE, 0xA4, 1, 2, 3};
  auto f = hubFrame(kHubId, MsgType::Event, 0, 1, (const uint8_t*)"\x10\x01\x00", 3);
  w.hub.onFrame(evil, f.data(), f.size(), 0, w.now);
  // dial1's MAC but claiming to be dial2
  Packet p;
  p.h.source = 0x21;
  p.h.destination = kHubId;
  p.h.type = MsgType::Heartbeat;
  uint8_t buf[64];
  size_t n = encodePacket(p, buf, sizeof(buf));
  w.hub.onFrame(macOf(DeviceId::M5Dial1), buf, n, 0, w.now);
  TEST_ASSERT_EQUAL(1, w.hub.counters().rx_unknown_mac);
  TEST_ASSERT_EQUAL(1, w.hub.counters().rx_bad_id);
  TEST_ASSERT_EQUAL(0, w.out.ofType("EVENT").size());
}

void test_devices_and_diag() {
  World w;
  w.hello(5);
  w.runAlive(1500);
  w.pc("{\"type\":\"GET_DEVICES\"}");
  JsonDocument d = w.out.lastOf("DEVICES");
  TEST_ASSERT_EQUAL(3, d["devices"].size());
  for (JsonObject o : d["devices"].as<JsonArray>()) {
    TEST_ASSERT_TRUE(o["online"].as<bool>());
    TEST_ASSERT_TRUE(o["session_ok"].as<bool>());
  }
  w.pc("{\"type\":\"GET_DIAG\"}");
  JsonDocument g = w.out.lastOf("DIAG");
  TEST_ASSERT_FALSE(g["counters"]["invalid_packets"].isNull());
  TEST_ASSERT_TRUE(g["devices"][0]["hb_recv"].as<int>() >= 1);
  TEST_ASSERT_FALSE(g["fw"].isNull());
}

void test_pc_timeout_closes_session() {
  World w;
  w.hello(5);
  w.runAlive(1500);
  TEST_ASSERT_TRUE(w.dial1.core.sessionValid());
  w.run(3500);  // PC silent
  TEST_ASSERT_EQUAL(0, w.hub.session());
  TEST_ASSERT_EQUAL_STRING("pc_timeout", w.out.lastOf("SESSION_CLOSED")["reason"]);
  TEST_ASSERT_FALSE(w.dial1.core.sessionValid());
  TEST_ASSERT_FALSE(w.illum.core.sessionValid());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_bad_lines);
  RUN_TEST(test_hello_ack);
  RUN_TEST(test_command_validation);
  RUN_TEST(test_frames_from_unknown_mac_or_wrong_id_ignored);
  RUN_TEST(test_devices_and_diag);
  RUN_TEST(test_pc_timeout_closes_session);
  return UNITY_END();
}
