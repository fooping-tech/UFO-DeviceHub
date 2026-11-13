// Shared fakes for the native (host) firmware tests.
#pragma once

#include <string.h>

#include <string>
#include <vector>

#include "device_ids.h"
#include "messages.h"
#include "node_core.h"
#include "protocol.h"

namespace testsupport {

using namespace ufo;

struct Frame {
  std::vector<uint8_t> bytes;
  uint8_t mac[6];
};

inline Packet decode(const Frame& f) {
  Packet p;
  DecodeError e = decodePacket(f.bytes.data(), f.bytes.size(), p);
  if (e != DecodeError::Ok) p.h.magic = 0;  // marks an invalid frame for the test
  return p;
}

// Captures what a NodeCore sends to the hub.
class FakeNodeTransport : public NodeTransport {
 public:
  bool sendToHub(const uint8_t* data, size_t len) override {
    Frame f;
    f.bytes.assign(data, data + len);
    memset(f.mac, 0, 6);
    frames.push_back(f);
    return true;
  }
  std::vector<Packet> packets(MsgType t) const {
    std::vector<Packet> out;
    for (const auto& f : frames) {
      Packet p = decode(f);
      if (p.h.magic == kMagic && p.h.type == t) out.push_back(p);
    }
    return out;
  }
  Packet last(MsgType t) const {
    auto v = packets(t);
    return v.empty() ? Packet() : v.back();
  }
  void clear() { frames.clear(); }
  std::vector<Frame> frames;
};

// Records events emitted by a device controller.
class FakeEmitter : public EventEmitter {
 public:
  uint32_t emit(const EventData& e, uint32_t /*now*/) override {
    events.push_back(e);
    return ++next_id;
  }
  size_t count(EventCode c) const {
    size_t n = 0;
    for (const auto& e : events)
      if (e.code == static_cast<uint8_t>(c)) ++n;
    return n;
  }
  std::vector<EventData> events;
  uint32_t next_id = 0;
};

inline const uint8_t* hubMac() { return findDevice(kHubId)->mac; }
inline const uint8_t* macOf(DeviceId id) { return findDevice(static_cast<uint8_t>(id))->mac; }

// Builds an encoded hub -> device frame.
inline std::vector<uint8_t> hubFrame(uint8_t dest, MsgType t, uint32_t session, uint32_t txn,
                                     const uint8_t* payload, size_t len, uint16_t seq = 1) {
  Packet p;
  p.h.source = kHubId;
  p.h.destination = dest;
  p.h.type = t;
  p.h.sequence = seq;
  p.h.session = session;
  p.h.txn = txn;
  memcpy(p.payload, payload, len);
  p.h.payload_len = static_cast<uint8_t>(len);
  std::vector<uint8_t> out(kMaxPacketSize);
  out.resize(encodePacket(p, out.data(), out.size()));
  return out;
}

inline std::vector<uint8_t> commandFrame(uint8_t dest, uint32_t session, uint32_t cmd_id,
                                         const uint8_t* payload, size_t len, uint16_t seq = 1) {
  return hubFrame(dest, MsgType::Command, session, cmd_id, payload, len, seq);
}

inline std::vector<uint8_t> simpleCommand(uint8_t dest, uint32_t session, uint32_t cmd_id, CommandCode c) {
  uint8_t pl[4];
  size_t n = encodeCommand(c, pl, sizeof(pl));
  return commandFrame(dest, session, cmd_id, pl, n);
}

inline std::vector<uint8_t> hubHeartbeat(uint8_t dest, uint32_t session, uint16_t seq) {
  HubHeartbeat hb;
  hb.flags = session ? 1 : 0;
  uint8_t pl[8];
  size_t n = encodeHubHeartbeat(hb, pl, sizeof(pl));
  return hubFrame(dest, MsgType::Heartbeat, session, 0, pl, n, seq);
}

inline AckPayload ackOf(const Packet& p) {
  AckPayload a;
  decodeAck(p.payload, p.h.payload_len, a);
  return a;
}

inline std::vector<uint8_t> fromHex(const std::string& hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  return out;
}

}  // namespace testsupport
