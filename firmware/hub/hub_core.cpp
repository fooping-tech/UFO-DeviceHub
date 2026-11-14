#include "hub_core.h"

#include <string.h>

namespace ufo {

namespace {
constexpr uint32_t kHubCommandBit = 0x80000000u;
bool isHubCommand(uint32_t txn) { return (txn & kHubCommandBit) != 0; }
}  // namespace

HubCore::HubCore(HubSink& sink, const HubConfig& cfg) : sink_(sink), cfg_(cfg), queue_(cfg.command_policy) {
  queue_.setListener(this);
}

void HubCore::begin(uint32_t now, uint32_t seed) {
  boot_ms_ = now;
  last_hb_ = now - cfg_.heartbeat_interval_ms;  // first heartbeat on the first tick
  next_seq_ = static_cast<uint16_t>(seed);
  hub_cmd_counter_ = (seed >> 8) & 0x00FFFFFF;
  child_count_ = 0;
  for (size_t i = 0; i < deviceCount() && child_count_ < kMaxChildren; ++i) {
    const DeviceEntry& d = deviceAt(i);
    if (d.id == kHubId) continue;
    HubChild& ch = children_[child_count_++];
    ch = HubChild();
    ch.entry = &d;
    ch.link.setTimeout(cfg_.link_timeout_ms);
  }
  session_ = 0;
  queue_.purge();
  purgePcOutbox();
  seen_events_.clear();
}

HubChild* HubCore::childById(uint8_t id) {
  for (size_t i = 0; i < child_count_; ++i)
    if (children_[i].entry->id == id) return &children_[i];
  return nullptr;
}

const HubChild* HubCore::findChild(uint8_t id) const {
  for (size_t i = 0; i < child_count_; ++i)
    if (children_[i].entry->id == id) return &children_[i];
  return nullptr;
}

size_t HubCore::pcOutboxSize() const {
  size_t n = 0;
  for (const auto& s : pc_outbox_)
    if (s.used) ++n;
  return n;
}

void HubCore::fillHeader(Packet& p, uint8_t dest, MsgType t, uint32_t txn) {
  p.h = Header();
  p.h.magic = systemMagic(cfg_.system_id);
  p.h.source = kHubId;
  p.h.destination = dest;
  p.h.type = t;
  p.h.sequence = next_seq_++;
  p.h.session = session_;
  p.h.txn = txn;
}

bool HubCore::sendFrame(uint8_t dest_id, const uint8_t* data, size_t len) {
  HubChild* ch = childById(dest_id);
  if (!ch) return false;
  ++ch->c.tx_frames;
  return sink_.sendFrame(ch->entry->mac, data, len);
}

void HubCore::sendNow(HubChild& ch, Packet& p) {
  uint8_t buf[kMaxPacketSize];
  size_t n = encodePacket(p, buf, sizeof(buf));
  if (n) sendFrame(ch.entry->id, buf, n);
}

void HubCore::onRetransmit(const ReliableQueue::Entry& e) {
  if (HubChild* ch = childById(e.dest)) ++ch->c.retransmissions;
}

void HubCore::onGiveUp(const ReliableQueue::Entry& e) {
  HubChild* ch = childById(e.dest);
  if (ch) ++ch->c.ack_timeouts;
  if (e.type == MsgType::Command && !isHubCommand(e.txn)) {
    CommandResultInfo r;
    r.dest = e.dest;
    r.command_id = e.txn;
    r.result = u8(AckResult::Timeout);
    r.attempts = e.attempts;
    sink_.commandResult(r);
  }
  if (ch && e.type == MsgType::Command && isHubCommand(e.txn)) ch->session_sync_pending = false;
}

void HubCore::failQueued(uint8_t dest, AckResult why) {
  ReliableQueue::Entry e;
  while (queue_.popFor(dest, e)) {
    if (e.type == MsgType::Command && !isHubCommand(e.txn)) {
      CommandResultInfo r;
      r.dest = e.dest;
      r.command_id = e.txn;
      r.result = u8(why);
      r.attempts = e.attempts;
      sink_.commandResult(r);
    }
  }
  if (HubChild* ch = childById(dest)) ch->session_sync_pending = false;
}

void HubCore::queueHubCommand(HubChild& ch, CommandCode code, uint32_t now) {
  Packet p;
  uint32_t txn = kHubCommandBit | (hub_cmd_counter_++ & 0x7FFFFFFF);
  if (txn == kHubCommandBit) txn = kHubCommandBit | (hub_cmd_counter_++ & 0x7FFFFFFF);
  fillHeader(p, ch.entry->id, MsgType::Command, txn);
  p.h.payload_len = static_cast<uint8_t>(encodeCommand(code, p.payload, sizeof(p.payload)));
  if (queue_.push(ch.entry->id, p, now)) {
    ch.session_sync_pending = true;
    ch.last_session_sync = now;
    queue_.tick(now, *this);
  }
}

void HubCore::purgePcOutbox() {
  for (auto& s : pc_outbox_) {
    if (s.used) ++counters_.pc_events_dropped;
    s.used = false;
  }
}

bool HubCore::openSession(uint32_t session, uint32_t now) {
  if (session == 0) return false;
  last_pc_rx_ = now;
  if (session == session_) return true;  // HELLO repeated: same session, nothing to reset
  // A new session never inherits commands or events of the previous one.
  queue_.purge();
  purgePcOutbox();
  seen_events_.clear();
  session_ = session;
  ++counters_.sessions_opened;
  for (size_t i = 0; i < child_count_; ++i) {
    children_[i].session_sync_pending = false;
    if (children_[i].link.up()) queueHubCommand(children_[i], CommandCode::SessionBegin, now);
  }
  return true;
}

void HubCore::closeSession(uint32_t now, SessionCloseReason reason) {
  if (session_ == 0) return;
  uint32_t old = session_;
  queue_.purge();
  purgePcOutbox();
  session_ = 0;
  ++counters_.sessions_closed;
  for (size_t i = 0; i < child_count_; ++i) {
    children_[i].session_sync_pending = false;
    if (children_[i].link.up()) queueHubCommand(children_[i], CommandCode::SessionEnd, now);
  }
  sink_.sessionClosed(old, reason);
}

AckResult HubCore::submitCommand(uint8_t dest, uint32_t session, uint32_t command_id, const uint8_t* payload,
                                 size_t len, uint32_t now) {
  AckResult r = AckResult::Accepted;
  const uint8_t code = len > 0 ? payload[0] : 0;
  const int plen = len > 0 ? commandParamLength(code) : -1;
  HubChild* ch = childById(dest);
  if (command_id == 0 || isHubCommand(command_id) || len == 0 || len > kMaxPayload || plen < 0 ||
      static_cast<int>(len) - 1 != plen || code == static_cast<uint8_t>(CommandCode::SessionBegin) ||
      code == static_cast<uint8_t>(CommandCode::SessionEnd)) {
    r = AckResult::BadRequest;
  } else if (!ch) {
    r = AckResult::UnknownDevice;
  } else if (commandRequiresSession(code) && session_ == 0) {
    r = AckResult::NoSession;
  } else if (commandRequiresSession(code) && session != session_) {
    r = AckResult::SessionMismatch;
  } else if (!ch->link.up()) {
    r = AckResult::Offline;
  } else if (queue_.sizeFor(dest) >= cfg_.max_queue_per_device || queue_.full()) {
    r = AckResult::QueueFull;
  }
  if (r != AckResult::Accepted) {
    ++counters_.commands_rejected;
    return r;
  }
  Packet p;
  fillHeader(p, dest, MsgType::Command, command_id);
  memcpy(p.payload, payload, len);
  p.h.payload_len = static_cast<uint8_t>(len);
  if (!queue_.push(dest, p, now)) return AckResult::QueueFull;
  ++ch->c.commands;
  queue_.tick(now, *this);
  return AckResult::Accepted;
}

void HubCore::pcEventAck(uint8_t source, uint32_t event_id) {
  for (auto& s : pc_outbox_) {
    if (s.used && s.ev.source == source && s.ev.event_id == event_id) s.used = false;
  }
}

void HubCore::sendAckToChild(HubChild& ch, const Packet& req, AckResult r) {
  Packet p;
  fillHeader(p, ch.entry->id, MsgType::Ack, req.h.txn);
  p.h.session = req.h.session;
  AckPayload a;
  a.acked_type = static_cast<uint8_t>(req.h.type);
  a.result = u8(r);
  a.acked_seq = req.h.sequence;
  p.h.payload_len = static_cast<uint8_t>(encodeAck(a, p.payload, sizeof(p.payload)));
  sendNow(ch, p);
}

void HubCore::onFrame(const uint8_t* src_mac, const uint8_t* data, size_t len, int8_t /*rssi*/, uint32_t now) {
  ++counters_.rx_frames;
  const DeviceEntry* dev = src_mac ? findDeviceByMac(src_mac) : nullptr;
  HubChild* ch = dev ? childById(dev->id) : nullptr;
  if (!ch) {
    ++counters_.rx_unknown_mac;
    return;
  }
  Packet p;
  DecodeError err = decodePacket(data, len, p, systemMagic(cfg_.system_id));
  if (err != DecodeError::Ok) {
    ++ch->c.invalid;
    switch (err) {
      case DecodeError::BadMagic: ++counters_.rx_bad_magic; break;
      case DecodeError::BadVersion: ++counters_.rx_bad_version; break;
      case DecodeError::BadType: ++counters_.rx_bad_type; break;
      default: ++counters_.rx_bad_length; break;
    }
    return;
  }
  // Source MAC and logical source id must both match the provisioning table.
  if (p.h.source != ch->entry->id || p.h.destination != kHubId) {
    ++counters_.rx_bad_id;
    ++ch->c.invalid;
    return;
  }
  ++ch->c.rx_frames;
  ch->link.onRx(now);
  switch (p.h.type) {
    case MsgType::Ack: handleAck(*ch, p); break;
    case MsgType::Event: handleEvent(*ch, p, now); break;
    case MsgType::Heartbeat: handleHeartbeat(*ch, p, now); break;
    case MsgType::Status: {
      StatusCommon c;
      IlluminationStatus il;
      DialStatus dl;
      if (decodeStatus(p.payload, p.h.payload_len, c, il, dl))
        sink_.statusToPc(ch->entry->id, c, il, dl);
      else
        ++counters_.rx_bad_payload;
      break;
    }
    case MsgType::Error: {
      ErrorPayload e;
      if (decodeError(p.payload, p.h.payload_len, e))
        sink_.deviceError(ch->entry->id, e);
      else
        ++counters_.rx_bad_payload;
      break;
    }
    default: ++counters_.rx_bad_payload; break;  // devices never send COMMAND to the hub
  }
}

void HubCore::handleAck(HubChild& ch, const Packet& p) {
  AckPayload a;
  if (!decodeAck(p.payload, p.h.payload_len, a) || a.acked_type != static_cast<uint8_t>(MsgType::Command)) {
    ++counters_.rx_bad_payload;
    return;
  }
  ReliableQueue::Entry removed;
  if (!queue_.acknowledge(ch.entry->id, MsgType::Command, p.h.txn, &removed)) {
    ++counters_.late_acks;  // duplicate ACK of a retransmission, or already timed out
    return;
  }
  ++ch.c.acks;
  if (isHubCommand(p.h.txn)) {
    ch.session_sync_pending = false;
    return;
  }
  CommandResultInfo r;
  r.dest = ch.entry->id;
  r.command_id = p.h.txn;
  r.result = a.result;
  r.flags = a.flags;
  r.detail = a.detail;
  r.from_device = true;
  r.attempts = removed.attempts;
  sink_.commandResult(r);
}

void HubCore::handleEvent(HubChild& ch, const Packet& p, uint32_t now) {
  EventData ev;
  if (!decodeEvent(p.payload, p.h.payload_len, ev)) {
    ++counters_.rx_bad_payload;
    sendAckToChild(ch, p, AckResult::InvalidParams);
    return;
  }
  if (session_ == 0 || p.h.session != session_) {
    sendAckToChild(ch, p, AckResult::RejectedSession);  // the device drops it
    return;
  }
  // Already relayed? Answer again, do not relay twice.
  bool queued_or_seen = false;
  for (const auto& s : pc_outbox_)
    if (s.used && s.ev.source == ch.entry->id && s.ev.event_id == p.h.txn) queued_or_seen = true;
  if (queued_or_seen || seen_events_.contains(ch.entry->id, p.h.txn)) {
    ++ch.c.event_duplicates;
    sendAckToChild(ch, p, AckResult::Accepted);
    return;
  }
  PcSlot* slot = nullptr;
  for (auto& s : pc_outbox_)
    if (!s.used) {
      slot = &s;
      break;
    }
  if (!slot) return;  // PC outbox full: no ACK, the device retries later (back-pressure)
  seen_events_.checkAndInsert(ch.entry->id, p.h.txn);
  slot->used = true;
  slot->ev.source = ch.entry->id;
  slot->ev.session = p.h.session;
  slot->ev.event_id = p.h.txn;
  slot->ev.data = ev;
  slot->ev.attempts = 0;
  slot->last_tx = now;
  ++ch.c.events;
  ++counters_.pc_events_queued;
  sendAckToChild(ch, p, AckResult::Accepted);
  // deliver right away
  slot->ev.attempts = 1;
  sink_.eventToPc(slot->ev);
}

void HubCore::syncSession(HubChild& ch, uint32_t now) {
  if (ch.session_sync_pending) return;
  if (ch.last_session_sync != 0 && now - ch.last_session_sync < cfg_.session_sync_interval_ms) return;
  if (session_ != 0 && ch.reported_session != session_) {
    queueHubCommand(ch, CommandCode::SessionBegin, now);
  } else if (session_ == 0 && ch.reported_session != 0) {
    queueHubCommand(ch, CommandCode::SessionEnd, now);
  }
}

void HubCore::handleHeartbeat(HubChild& ch, const Packet& p, uint32_t now) {
  NodeHeartbeat hb;
  if (!decodeNodeHeartbeat(p.payload, p.h.payload_len, hb)) {
    ++counters_.rx_bad_payload;
    return;
  }
  ++ch.c.hb_recv;
  if (hb.echo_seq == ch.hb_seq_sent && ch.hb_sent_at != 0) {
    ch.rtt_last_ms = now - ch.hb_sent_at;
    ch.rtt_avg_ms = ch.rtt_avg_ms == 0 ? ch.rtt_last_ms : (ch.rtt_avg_ms * 7 + ch.rtt_last_ms) / 8;
  }
  if (ch.hb_valid && hb.boot_id != ch.last_hb.boot_id) {
    // The device rebooted: whatever was in flight for it must not be re-sent.
    ++ch.c.reboots;
    failQueued(ch.entry->id, AckResult::Offline);
    sink_.deviceRebooted(ch.entry->id, hb.boot_id);
  }
  ch.last_hb = hb;
  ch.hb_valid = true;
  ch.reported_session = p.h.session;
  syncSession(ch, now);
}

void HubCore::tick(uint32_t now) {
  // PC watchdog: no line from the PC -> close the session, devices go safe.
  if (session_ != 0 && now - last_pc_rx_ > cfg_.pc_timeout_ms) closeSession(now, SessionCloseReason::PcTimeout);

  for (size_t i = 0; i < child_count_; ++i) {
    HubChild& ch = children_[i];
    int change = ch.link.update(now);
    if (change < 0) {
      failQueued(ch.entry->id, AckResult::Offline);
      ch.reported_session = 0xFFFFFFFF;  // unknown until the next heartbeat
      sink_.linkChanged(ch.entry->id, false);
    } else if (change > 0) {
      sink_.linkChanged(ch.entry->id, true);
    }
  }

  if (now - last_hb_ >= cfg_.heartbeat_interval_ms) {
    last_hb_ = now;
    for (size_t i = 0; i < child_count_; ++i) {
      HubChild& ch = children_[i];
      Packet p;
      fillHeader(p, ch.entry->id, MsgType::Heartbeat, 0);
      HubHeartbeat hb;
      hb.uptime_ms = uptime(now);
      hb.flags = session_ != 0 ? 1 : 0;
      p.h.payload_len = static_cast<uint8_t>(encodeHubHeartbeat(hb, p.payload, sizeof(p.payload)));
      ch.hb_seq_sent = p.h.sequence;
      ch.hb_sent_at = now;
      ++ch.c.hb_sent;
      sendNow(ch, p);
    }
  }

  queue_.tick(now, *this);

  for (auto& s : pc_outbox_) {
    if (!s.used || now - s.last_tx < cfg_.pc_event_retry_ms) continue;
    s.last_tx = now;
    if (s.ev.attempts < 255) ++s.ev.attempts;
    ++counters_.pc_event_resends;
    sink_.eventToPc(s.ev);
  }
}

}  // namespace ufo
