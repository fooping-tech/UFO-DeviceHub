// Packet framing / payload codecs + cross-language golden vectors.
#include <ArduinoJson.h>
#include <unity.h>

#include <fstream>
#include <sstream>

#include "../support.h"

using namespace ufo;
using namespace testsupport;

void setUp() {}
void tearDown() {}

static Packet sample() {
  Packet p;
  p.h.source = 0x01;
  p.h.destination = 0x20;
  p.h.type = MsgType::Command;
  p.h.sequence = 0x1234;
  p.h.session = 0xA1B2C3D4;
  p.h.txn = 0x01020304;
  p.h.payload_len = 1;
  p.payload[0] = 0x22;
  return p;
}

void test_roundtrip_and_little_endian() {
  Packet p = sample();
  uint8_t buf[kMaxPacketSize];
  size_t n = encodePacket(p, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(kHeaderSize + 1, n);
  const uint8_t expect[] = {0x46, 0x55, 0x01, 0x01, 0x20, 0x01, 0x34, 0x12, 0xD4,
                            0xC3, 0xB2, 0xA1, 0x04, 0x03, 0x02, 0x01, 0x01, 0x22};
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
  Packet q;
  TEST_ASSERT_EQUAL(DecodeError::Ok, decodePacket(buf, n, q));
  TEST_ASSERT_EQUAL_HEX32(0xA1B2C3D4, q.h.session);
  TEST_ASSERT_EQUAL_HEX32(0x01020304, q.h.txn);
  TEST_ASSERT_EQUAL_HEX16(0x1234, q.h.sequence);
  TEST_ASSERT_EQUAL_HEX8(0x22, q.payload[0]);
}

void test_rejects_bad_frames() {
  Packet p = sample();
  uint8_t buf[kMaxPacketSize + 4];
  size_t n = encodePacket(p, buf, sizeof(buf));
  Packet q;
  TEST_ASSERT_EQUAL(DecodeError::TooShort, decodePacket(buf, kHeaderSize - 1, q));
  TEST_ASSERT_EQUAL(DecodeError::TooShort, decodePacket(nullptr, 0, q));
  TEST_ASSERT_EQUAL(DecodeError::TooLong, decodePacket(buf, kMaxPacketSize + 1, q));
  // trailing byte / truncated payload
  TEST_ASSERT_EQUAL(DecodeError::BadLength, decodePacket(buf, n + 1, q));
  TEST_ASSERT_EQUAL(DecodeError::BadLength, decodePacket(buf, n - 1 < kHeaderSize ? kHeaderSize : n - 1, q));
  uint8_t bad[kMaxPacketSize];
  memcpy(bad, buf, n);
  bad[0] ^= 0xFF;
  TEST_ASSERT_EQUAL(DecodeError::BadMagic, decodePacket(bad, n, q));
  memcpy(bad, buf, n);
  bad[2] = 2;
  TEST_ASSERT_EQUAL(DecodeError::BadVersion, decodePacket(bad, n, q));
  memcpy(bad, buf, n);
  bad[5] = 0x07;
  TEST_ASSERT_EQUAL(DecodeError::BadType, decodePacket(bad, n, q));
  memcpy(bad, buf, n);
  bad[16] = 48;  // payload_length > 47
  TEST_ASSERT_EQUAL(DecodeError::BadLength, decodePacket(bad, n, q));
}

void test_max_payload() {
  Packet p = sample();
  p.h.payload_len = kMaxPayload;
  uint8_t buf[kMaxPacketSize];
  TEST_ASSERT_EQUAL(kMaxPacketSize, encodePacket(p, buf, sizeof(buf)));
  p.h.payload_len = kMaxPayload + 1;
  TEST_ASSERT_EQUAL(0, encodePacket(p, buf, sizeof(buf)));
  p.h.payload_len = kMaxPayload;
  TEST_ASSERT_EQUAL(0, encodePacket(p, buf, kMaxPacketSize - 1));  // cap too small
}

void test_payload_codecs_strict_length() {
  uint8_t b[kMaxPayload];
  SetLightModeParams lm;
  lm.mode = 4;
  lm.brightness = 180;
  lm.transition_ms = 500;
  lm.r = 1;
  size_t n = encodeSetLightMode(lm, b, sizeof(b));
  TEST_ASSERT_EQUAL(9, n);
  SetLightModeParams out;
  TEST_ASSERT_TRUE(decodeSetLightMode(b + 1, n - 1, out));
  TEST_ASSERT_EQUAL(500, out.transition_ms);
  TEST_ASSERT_FALSE(decodeSetLightMode(b + 1, n - 2, out));
  b[1] = 14;  // mode out of range (NIGHT = 13 is the last)
  TEST_ASSERT_FALSE(decodeSetLightMode(b + 1, n - 1, out));

  SetMistParams sm;
  sm.pattern = static_cast<uint8_t>(MistPattern::Breath);
  sm.duration_s = 300;
  n = encodeSetMist(sm, b, sizeof(b));
  TEST_ASSERT_EQUAL(4, n);
  SetMistParams smo;
  TEST_ASSERT_TRUE(decodeSetMist(b + 1, n - 1, smo));
  TEST_ASSERT_EQUAL(300, smo.duration_s);
  b[1] = 4;  // unknown pattern
  TEST_ASSERT_FALSE(decodeSetMist(b + 1, n - 1, smo));
  TEST_ASSERT_TRUE(commandRequiresSession(static_cast<uint8_t>(CommandCode::SetMist)));
  TEST_ASSERT_EQUAL_HEX16(0x5546, systemMagic(1));
  TEST_ASSERT_EQUAL_HEX16(0x5547, systemMagic(2));

  RelayPulseParams rp;
  rp.duration_ms = 0;
  n = encodeRelayPulse(rp, b, sizeof(b));
  TEST_ASSERT_FALSE(decodeRelayPulse(b + 1, n - 1, rp));  // zero duration rejected

  TriggerEffectParams te;
  te.effect = 0;  // NONE is not a valid trigger
  n = encodeTriggerEffect(te, b, sizeof(b));
  TEST_ASSERT_FALSE(decodeTriggerEffect(b + 1, n - 1, te));

  SetDialEffectParams de;
  de.effect = static_cast<uint8_t>(DialEffect::Comet);
  de.direction = static_cast<uint8_t>(DialDirection::ToDial);
  de.r = 10;
  de.g = 11;
  de.b = 12;
  de.duration_ms = 1500;
  de.priority = static_cast<uint8_t>(EffectPriority::High);
  de.target = static_cast<uint8_t>(DialLedTarget::Transfer);
  n = encodeSetDialEffect(de, b, sizeof(b));
  const uint8_t expect_de[] = {0x23, 0x04, 0x02, 0x0A, 0x0B, 0x0C, 0xDC, 0x05, 0x02, 0x02};
  TEST_ASSERT_EQUAL(sizeof(expect_de), n);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expect_de, b, n);
  SetDialEffectParams deo;
  TEST_ASSERT_TRUE(decodeSetDialEffect(b + 1, n - 1, deo));
  TEST_ASSERT_EQUAL(1500, deo.duration_ms);
  TEST_ASSERT_EQUAL(12, deo.b);
  TEST_ASSERT_FALSE(decodeSetDialEffect(b + 1, n - 2, deo));
  b[2] = 0;  // COMET without a direction
  TEST_ASSERT_FALSE(decodeSetDialEffect(b + 1, n - 1, deo));
  b[2] = 3;  // unknown direction
  TEST_ASSERT_FALSE(decodeSetDialEffect(b + 1, n - 1, deo));
  b[2] = 2;
  b[9] = 4;  // unknown target bit
  TEST_ASSERT_FALSE(decodeSetDialEffect(b + 1, n - 1, deo));
  SetDialColorParams dc;
  dc.r = 255;
  dc.b = 255;
  n = encodeSetDialColor(dc, b, sizeof(b));
  TEST_ASSERT_EQUAL(4, n);
  TEST_ASSERT_TRUE(decodeSetDialColor(b + 1, n - 1, dc));
  TEST_ASSERT_FALSE(decodeSetDialColor(b + 1, 2, dc));
  TEST_ASSERT_TRUE(commandRequiresSession(static_cast<uint8_t>(CommandCode::SetDialEffect)));
  TEST_ASSERT_TRUE(commandRequiresSession(static_cast<uint8_t>(CommandCode::SetDialColor)));
  TEST_ASSERT_EQUAL_STRING("SET_DIAL_EFFECT", commandName(0x23));

  TEST_ASSERT_EQUAL(-1, commandParamLength(0x99));
  TEST_ASSERT_TRUE(commandRequiresSession(static_cast<uint8_t>(CommandCode::ReleaseBall)));
  TEST_ASSERT_TRUE(commandRequiresSession(static_cast<uint8_t>(CommandCode::RelayPulse)));
  TEST_ASSERT_FALSE(commandRequiresSession(static_cast<uint8_t>(CommandCode::StopAll)));
  TEST_ASSERT_FALSE(commandRequiresSession(static_cast<uint8_t>(CommandCode::RelayOff)));
}

void test_status_and_heartbeat_sizes() {
  uint8_t b[kMaxPayload];
  StatusCommon c;
  c.device_type = static_cast<uint8_t>(DeviceType::Dial);
  DialStatus dl;
  dl.last_release_command = 77;
  IlluminationStatus il;
  size_t n = encodeStatus(c, &il, &dl, b, sizeof(b));
  TEST_ASSERT_EQUAL(kStatusCommonSize + kDialStatusSize, n);
  StatusCommon c2;
  DialStatus dl2;
  IlluminationStatus il2;
  TEST_ASSERT_TRUE(decodeStatus(b, n, c2, il2, dl2));
  TEST_ASSERT_EQUAL(77, dl2.last_release_command);
  TEST_ASSERT_TRUE(n <= kMaxPayload);
  // LED fields (fw >= 0.3) and compatibility with the 12-byte status of an older dial
  dl.front_effect = 3;
  dl.led_flags = 1;
  dl.color_b = 200;
  n = encodeStatus(c, &il, &dl, b, sizeof(b));
  TEST_ASSERT_TRUE(decodeStatus(b, n, c2, il2, dl2));
  TEST_ASSERT_EQUAL(3, dl2.front_effect);
  TEST_ASSERT_EQUAL(200, dl2.color_b);
  DialStatus old;
  TEST_ASSERT_TRUE(decodeStatus(b, kStatusCommonSize + kDialStatusSizeV1, c2, il2, old));
  TEST_ASSERT_EQUAL(77, old.last_release_command);
  TEST_ASSERT_EQUAL(0, old.front_effect);
  TEST_ASSERT_FALSE(decodeStatus(b, n - 1, c2, il2, dl2));  // any other length is still rejected
  TEST_ASSERT_FALSE(decodeStatus(b, kStatusCommonSize + kDialStatusSizeV1 + 1, c2, il2, dl2));
  NodeHeartbeat hb;
  TEST_ASSERT_EQUAL(kNodeHeartbeatSize, encodeNodeHeartbeat(hb, b, sizeof(b)));
}

// tests/protocol/vectors.json is produced by pc/protocol.py; the firmware codec
// must decode every vector to the same fields and re-encode it byte for byte.
void test_golden_vectors() {
  std::ifstream f(UFO_VECTORS_PATH);
  TEST_ASSERT_TRUE_MESSAGE(f.good(), "tests/protocol/vectors.json missing");
  std::stringstream ss;
  ss << f.rdbuf();
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, ss.str()));
  JsonArray arr = doc["vectors"].as<JsonArray>();
  TEST_ASSERT_TRUE(arr.size() >= 8);
  for (JsonObject v : arr) {
    auto bytes = fromHex(v["hex"].as<std::string>());
    Packet p;
    DecodeError e = decodePacket(bytes.data(), bytes.size(), p, systemMagic(v["system_id"] | 1));
    const char* expect_err = v["error"] | "";
    if (expect_err[0]) {
      TEST_ASSERT_EQUAL_STRING_MESSAGE(expect_err, decodeErrorName(e), v["name"].as<const char*>());
      continue;
    }
    TEST_ASSERT_EQUAL_MESSAGE(DecodeError::Ok, e, v["name"].as<const char*>());
    TEST_ASSERT_EQUAL(v["source"].as<uint8_t>(), p.h.source);
    TEST_ASSERT_EQUAL(v["destination"].as<uint8_t>(), p.h.destination);
    TEST_ASSERT_EQUAL(v["message_type"].as<uint8_t>(), static_cast<uint8_t>(p.h.type));
    TEST_ASSERT_EQUAL(v["sequence"].as<uint16_t>(), p.h.sequence);
    TEST_ASSERT_EQUAL_HEX32(v["session_id"].as<uint32_t>(), p.h.session);
    TEST_ASSERT_EQUAL_HEX32(v["command_id"].as<uint32_t>(), p.h.txn);
    auto payload = fromHex(v["payload_hex"].as<std::string>());
    TEST_ASSERT_EQUAL(payload.size(), p.h.payload_len);
    if (!payload.empty()) TEST_ASSERT_EQUAL_HEX8_ARRAY(payload.data(), p.payload, payload.size());
    uint8_t re[kMaxPacketSize];
    size_t n = encodePacket(p, re, sizeof(re));
    TEST_ASSERT_EQUAL(bytes.size(), n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(bytes.data(), re, n);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_and_little_endian);
  RUN_TEST(test_rejects_bad_frames);
  RUN_TEST(test_max_payload);
  RUN_TEST(test_payload_codecs_strict_length);
  RUN_TEST(test_status_and_heartbeat_sizes);
  RUN_TEST(test_golden_vectors);
  return UNITY_END();
}
