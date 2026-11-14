#include "node_core.h"

#include <string.h>

namespace ufo {

NodeCore::NodeCore(const NodeConfig& cfg, NodeTransport& tx, NodeApp& app)
    : cfg_(cfg), tx_(tx), app_(app), link_(cfg.link_timeout_ms), outbox_(cfg.event_policy) {}

void NodeCore::begin(uint32_t now, uint32_t boot_id, uint32_t id_seed) {
  boot_ms_ = now;
  boot_id_ = boot_id ? boot_id : 1;
  next_event_id_ = id_seed ? (id_seed & 0x7FFFFFFF) | 1 : 1;
  next_seq_ = static_cast<uint16_t>(id_seed >> 16);
  session_ = 0;
  dedup_.clear();
  outbox_.purge();
  link_.reset();
}

void NodeCore::fillHeader(Packet& p, MsgType t, uint32_t txn) {
  p.h = Header();
  p.h.source = cfg_.self_id;
  p.h.destination = cfg_.hub_id;
  p.h.magic = systemMagic(cfg_.system_id);
  p.h.type = t;
  p.h.sequence = next_seq_++;
  p.h.session = session_;
  p.h.txn = txn;
}

bool NodeCore::sendFrame(uint8_t /*dest_id*/, const uint8_t* data, size_t len) {
  ++counters_.tx_frames;
  return tx_.sendToHub(data, len);
}

void NodeCore::sendPacket(Packet& p) {
  uint8_t buf[kMaxPacketSize];
  size_t n = encodePacket(p, buf, sizeof(buf));
  if (n) sendFrame(cfg_.hub_id, buf, n);
}

void NodeCore::onFrame(const uint8_t* src_mac, const uint8_t* data, size_t len, int8_t rssi, uint32_t now) {
  ++counters_.rx_frames;
  if (src_mac == nullptr || !macEqual(src_mac, cfg_.hub_mac)) {
    ++counters_.rx_bad_mac;
    return;
  }
  Packet p;
  if (decodePacket(data, len, p, systemMagic(cfg_.system_id)) != DecodeError::Ok) {
    ++counters_.rx_bad_frame;
    return;
  }
  if (p.h.source != cfg_.hub_id || p.h.destination != cfg_.self_id) {
    ++counters_.rx_bad_id;
    return;
  }
  ++counters_.rx_ok;
  link_.onRx(now);
  if (rssi != 0) last_rssi_ = rssi;
  switch (p.h.type) {
    case MsgType::Command: handleCommand(p, now); break;
    case MsgType::Ack: handleAck(p); break;
    case MsgType::Heartbeat: handleHeartbeat(p, now); break;
    default: ++counters_.rx_bad_payload; break;
  }
}

void NodeCore::sendAck(const Packet& req, uint8_t result, uint8_t flags, uint8_t detail) {
  Packet p;
  fillHeader(p, MsgType::Ack, req.h.txn);
  p.h.session = req.h.session;  // echo the session the command was issued in
  AckPayload a;
  a.acked_type = static_cast<uint8_t>(req.h.type);
  a.result = result;
  a.flags = flags;
  a.acked_seq = req.h.sequence;
  a.detail = detail;
  p.h.payload_len = static_cast<uint8_t>(encodeAck(a, p.payload, sizeof(p.payload)));
  sendPacket(p);
}

void NodeCore::setSession(uint32_t s, uint32_t now) {
  if (s == session_) return;
  session_ = s;
  dedup_.clear();
  size_t dropped = outbox_.purge();
  counters_.events_dropped += dropped;
  app_.onSessionChanged(s, now);
}

void NodeCore::handleCommand(const Packet& p, uint32_t now) {
  if (p.h.payload_len < 1 || p.h.txn == 0) {
    ++counters_.rx_bad_payload;
    sendAck(p, u8(AckResult::InvalidParams), 0, 0);
    return;
  }
  const uint8_t code = p.payload[0];
  const int plen = commandParamLength(code);
  if (plen < 0) {
    sendAck(p, u8(AckResult::Unsupported), 0, 0);
    return;
  }
  if (static_cast<int>(p.h.payload_len) - 1 != plen) {
    ++counters_.rx_bad_payload;
    sendAck(p, u8(AckResult::InvalidParams), 0, 0);
    return;
  }
  ++counters_.commands;

  switch (static_cast<CommandCode>(code)) {
    case CommandCode::SessionBegin:
      if (p.h.session == 0) {
        sendAck(p, u8(AckResult::InvalidParams), 0, 0);
        return;
      }
      setSession(p.h.session, now);
      sendAck(p, u8(AckResult::Accepted), 0, 0);
      return;
    case CommandCode::SessionEnd:
      setSession(0, now);
      sendAck(p, u8(AckResult::Accepted), 0, 0);
      return;
    case CommandCode::GetStatus:
      sendAck(p, u8(AckResult::Accepted), 0, 0);
      sendStatus(now);
      return;
    default:
      break;
  }

  if (commandRequiresSession(code) && (session_ == 0 || p.h.session != session_)) {
    ++counters_.rejected_session;
    sendAck(p, u8(AckResult::RejectedSession), 0, 0);
    return;
  }

  uint8_t stored_result = 0, stored_flags = 0;
  if (dedup_.lookup(p.h.txn, stored_result, stored_flags)) {
    ++counters_.duplicates;
    sendAck(p, stored_result, static_cast<uint8_t>(stored_flags | u8(AckFlag::Duplicate)), 0);
    return;
  }

  CommandOutcome out = app_.onCommand(static_cast<CommandCode>(code), p.payload + 1, plen, p.h.txn, now);
  dedup_.remember(p.h.txn, u8(out.result), out.flags);
  sendAck(p, u8(out.result), out.flags, out.detail);
}

void NodeCore::handleAck(const Packet& p) {
  AckPayload a;
  if (!decodeAck(p.payload, p.h.payload_len, a)) {
    ++counters_.rx_bad_payload;
    return;
  }
  if (a.acked_type == static_cast<uint8_t>(MsgType::Event)) {
    outbox_.acknowledge(cfg_.hub_id, MsgType::Event, p.h.txn);
  }
}

void NodeCore::handleHeartbeat(const Packet& p, uint32_t now) {
  HubHeartbeat hb;
  if (!decodeHubHeartbeat(p.payload, p.h.payload_len, hb)) {
    ++counters_.rx_bad_payload;
    return;
  }
  ++counters_.heartbeats;
  // The device session always follows the hub: a different (or no) hub session
  // ends ours. Only an explicit SESSION_BEGIN establishes a new one.
  if (session_ != 0 && p.h.session != session_) setSession(0, now);

  Packet r;
  fillHeader(r, MsgType::Heartbeat, 0);
  NodeHeartbeat nh;
  nh.echo_seq = p.h.sequence;
  nh.boot_id = boot_id_;
  nh.uptime_ms = uptime(now);
  nh.device_type = static_cast<uint8_t>(cfg_.device_type);
  nh.state = app_.state();
  nh.flags = combinedFlags();
  nh.rssi = last_rssi_;
  nh.rx_rejected = counters_.rejectedTotal();
  nh.fw_major = cfg_.fw_major;
  nh.fw_minor = cfg_.fw_minor;
  r.h.payload_len = static_cast<uint8_t>(encodeNodeHeartbeat(nh, r.payload, sizeof(r.payload)));
  sendPacket(r);
}

uint8_t NodeCore::combinedFlags() const {
  uint8_t f = app_.flags();
  if (session_ != 0) f |= u8(NodeFlag::SessionValid);
  if (link_.up()) f |= u8(NodeFlag::HubLinkUp);
  if (cfg_.example_keys) f |= u8(NodeFlag::ExampleKeys);
  return f;
}

void NodeCore::sendStatus(uint32_t now) {
  Packet p;
  fillHeader(p, MsgType::Status, 0);
  StatusCommon c;
  c.device_type = static_cast<uint8_t>(cfg_.device_type);
  c.state = app_.state();
  c.flags = combinedFlags();
  c.error = app_.errorCode();
  c.op_mode = app_.opMode();
  c.uptime_ms = uptime(now);
  c.boot_id = boot_id_;
  c.fw_major = cfg_.fw_major;
  c.fw_minor = cfg_.fw_minor;
  IlluminationStatus il;
  DialStatus dl;
  app_.fillStatus(il, dl);
  p.h.payload_len = static_cast<uint8_t>(encodeStatus(c, &il, &dl, p.payload, sizeof(p.payload)));
  if (p.h.payload_len) sendPacket(p);
}

uint32_t NodeCore::emitEvent(const EventData& e, uint32_t now) {
  if (session_ == 0) {
    ++counters_.events_dropped;
    return 0;
  }
  Packet p;
  uint32_t id = next_event_id_++;
  if (next_event_id_ == 0 || next_event_id_ > 0x7FFFFFFF) next_event_id_ = 1;
  fillHeader(p, MsgType::Event, id);
  p.h.payload_len = static_cast<uint8_t>(encodeEvent(e, p.payload, sizeof(p.payload)));
  if (p.h.payload_len == 0 || !outbox_.push(cfg_.hub_id, p, now)) {
    ++counters_.events_dropped;
    return 0;
  }
  ++counters_.events_sent;
  outbox_.tick(now, *this);  // first transmission right away
  return id;
}

void NodeCore::tick(uint32_t now) {
  int change = link_.update(now);
  if (change != 0) app_.onHubLinkChanged(change > 0, now);
  outbox_.tick(now, *this);
}

}  // namespace ufo
