#include "reliability.h"

#include <string.h>

namespace ufo {

bool ReliableQueue::push(uint8_t dest, const Packet& p, uint32_t now) {
  for (size_t i = 0; i < cap_; ++i) {
    Entry& e = entries_[i];
    if (e.used) continue;
    size_t n = encodePacket(p, e.data, sizeof(e.data));
    if (n == 0) return false;
    e.used = true;
    e.dest = dest;
    e.type = p.h.type;
    e.txn = p.h.txn;
    e.seq = p.h.sequence;
    e.len = static_cast<uint8_t>(n);
    e.attempts = 0;
    e.last_tx = now;
    e.enqueued_at = now;
    e.order = ++order_counter_;
    return true;
  }
  return false;
}

ReliableQueue::Entry* ReliableQueue::headFor(uint8_t dest) {
  Entry* head = nullptr;
  for (size_t i = 0; i < cap_; ++i) {
    Entry& e = entries_[i];
    if (!e.used || e.dest != dest) continue;
    if (!head || static_cast<int32_t>(e.order - head->order) < 0) head = &e;
  }
  return head;
}

bool ReliableQueue::acknowledge(uint8_t from_dest, MsgType acked_type, uint32_t txn, Entry* removed) {
  for (size_t i = 0; i < cap_; ++i) {
    Entry& e = entries_[i];
    if (e.used && e.dest == from_dest && e.type == acked_type && e.txn == txn) {
      if (removed) *removed = e;
      e.used = false;
      return true;
    }
  }
  return false;
}

void ReliableQueue::tick(uint32_t now, FrameSink& sink) {
  // Visit each destination's head once per tick.
  for (size_t i = 0; i < cap_; ++i) {
    Entry& candidate = entries_[i];
    if (!candidate.used) continue;
    Entry* head = headFor(candidate.dest);
    if (head != &candidate) continue;
    Entry& e = *head;
    if (e.attempts == 0) {
      sink.sendFrame(e.dest, e.data, e.len);
      e.attempts = 1;
      e.last_tx = now;
      continue;
    }
    uint32_t since = now - e.last_tx;
    bool fast_phase = e.attempts <= policy_.max_retries;
    if (fast_phase) {
      if (since < policy_.ack_timeout_ms) continue;
      sink.sendFrame(e.dest, e.data, e.len);
      ++e.attempts;
      e.last_tx = now;
      ++retransmissions_;
      if (listener_) listener_->onRetransmit(e);
      continue;
    }
    // all fast retries used and the last one has timed out
    if (!policy_.retry_forever) {
      if (since < policy_.ack_timeout_ms) continue;
      Entry copy = e;
      e.used = false;
      ++give_ups_;
      if (listener_) listener_->onGiveUp(copy);
      continue;
    }
    if (since < policy_.slow_retry_ms) continue;
    sink.sendFrame(e.dest, e.data, e.len);
    if (e.attempts < 255) ++e.attempts;
    e.last_tx = now;
    ++retransmissions_;
    if (listener_) listener_->onRetransmit(e);
  }
}

size_t ReliableQueue::purge(uint8_t dest) {
  size_t n = 0;
  for (size_t i = 0; i < cap_; ++i) {
    Entry& e = entries_[i];
    if (e.used && (dest == 0xFF || e.dest == dest)) {
      e.used = false;
      ++n;
    }
  }
  return n;
}

size_t ReliableQueue::purgeSession(uint32_t keep_session) {
  size_t n = 0;
  for (size_t i = 0; i < cap_; ++i) {
    Entry& e = entries_[i];
    if (!e.used) continue;
    Packet p;
    if (decodePacket(e.data, e.len, p) != DecodeError::Ok || p.h.session != keep_session) {
      e.used = false;
      ++n;
    }
  }
  return n;
}

bool ReliableQueue::popFor(uint8_t dest, Entry& out) {
  Entry* oldest = nullptr;
  for (size_t i = 0; i < cap_; ++i) {
    Entry& e = entries_[i];
    if (!e.used || (dest != 0xFF && e.dest != dest)) continue;
    if (!oldest || static_cast<int32_t>(e.order - oldest->order) < 0) oldest = &e;
  }
  if (!oldest) return false;
  out = *oldest;
  oldest->used = false;
  return true;
}

size_t ReliableQueue::size() const {
  size_t n = 0;
  for (size_t i = 0; i < cap_; ++i)
    if (entries_[i].used) ++n;
  return n;
}

size_t ReliableQueue::sizeFor(uint8_t dest) const {
  size_t n = 0;
  for (size_t i = 0; i < cap_; ++i)
    if (entries_[i].used && entries_[i].dest == dest) ++n;
  return n;
}

bool DedupCache::lookup(uint32_t txn, uint8_t& result, uint8_t& flags) const {
  for (const auto& it : items_) {
    if (it.used && it.txn == txn) {
      result = it.result;
      flags = it.flags;
      return true;
    }
  }
  return false;
}

void DedupCache::remember(uint32_t txn, uint8_t result, uint8_t flags) {
  for (auto& it : items_) {
    if (it.used && it.txn == txn) {
      it.result = result;
      it.flags = flags;
      return;
    }
  }
  items_[next_] = Item{true, txn, result, flags};
  next_ = (next_ + 1) % kSize;
}

void DedupCache::clear() {
  for (auto& it : items_) it.used = false;
  next_ = 0;
}

bool RecentIds::checkAndInsert(uint8_t source, uint32_t id) {
  for (const auto& it : items_)
    if (it.used && it.source == source && it.id == id) return true;
  items_[next_].used = true;
  items_[next_].source = source;
  items_[next_].id = id;
  next_ = (next_ + 1) % kSize;
  return false;
}

bool RecentIds::contains(uint8_t source, uint32_t id) const {
  for (const auto& it : items_)
    if (it.used && it.source == source && it.id == id) return true;
  return false;
}

void RecentIds::clear() {
  for (auto& it : items_) it.used = false;
  next_ = 0;
}

int LinkMonitor::update(uint32_t now) {
  bool alive = seen_ && (now - last_rx_) < timeout_;
  if (alive && !up_) {
    up_ = true;
    return 1;
  }
  if (!alive && up_) {
    up_ = false;
    return -1;
  }
  return 0;
}

}  // namespace ufo
