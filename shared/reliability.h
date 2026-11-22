// Reliability building blocks shared by the hub and the devices (pure C++).
//
//  * ReliableQueue : application-level ACK / timeout / retransmit. One message in
//                    flight per destination (keeps per-device ordering), FIFO behind it.
//  * DedupCache    : remembers the result of recently handled command_ids so a
//                    retransmitted command is answered, never re-executed.
//  * RecentIds     : (source, id) window used to drop duplicated events.
//  * LinkMonitor   : heartbeat based up/down detection.
//
// The ESP-NOW "send success" callback is NOT used as an application ACK anywhere.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

namespace ufo {

struct RetryPolicy {
  uint32_t ack_timeout_ms = 100;
  uint8_t max_retries = 3;           // retransmissions after the first send
  bool retry_forever = false;        // events: keep trying after max_retries ...
  uint32_t slow_retry_ms = 500;      // ... at this interval
};

class FrameSink {
 public:
  virtual ~FrameSink() {}
  virtual bool sendFrame(uint8_t dest_id, const uint8_t* data, size_t len) = 0;
};

class ReliableQueue {
 public:
  struct Entry {
    bool used = false;
    uint8_t dest = 0;
    MsgType type = MsgType::Command;
    uint32_t txn = 0;
    uint16_t seq = 0;
    uint8_t len = 0;
    uint8_t data[kMaxPacketSize] = {0};
    uint8_t attempts = 0;   // transmissions so far
    uint32_t last_tx = 0;
    uint32_t enqueued_at = 0;
    uint32_t order = 0;     // FIFO order
  };

  class Listener {
   public:
    virtual ~Listener() {}
    virtual void onGiveUp(const Entry& e) = 0;
    virtual void onRetransmit(const Entry& /*e*/) {}
  };

  ReliableQueue(Entry* storage, size_t capacity, const RetryPolicy& policy)
      : entries_(storage), cap_(capacity), policy_(policy) {}

  void setListener(Listener* l) { listener_ = l; }
  // Encodes p and queues it. Returns false when the queue is full / packet invalid.
  bool push(uint8_t dest, const Packet& p, uint32_t now);
  // Matches an ACK (by destination, acked type and txn). Returns true if an entry was removed.
  bool acknowledge(uint8_t from_dest, MsgType acked_type, uint32_t txn, Entry* removed = nullptr);
  // Sends due (re)transmissions.
  void tick(uint32_t now, FrameSink& sink);
  // Drops queued entries. dest = 0xFF: all destinations. Returns the number removed.
  size_t purge(uint8_t dest = 0xFF);
  size_t purgeSession(uint32_t keep_session);  // drop entries whose header session differs
  // drop queued EVENT frames whose payload starts with `event_code` (not yet acknowledged)
  size_t purgeEvent(uint8_t event_code);
  // Removes the oldest entry for dest (any dest if 0xFF) and copies it out.
  bool popFor(uint8_t dest, Entry& out);
  size_t size() const;
  size_t sizeFor(uint8_t dest) const;
  bool full() const { return size() >= cap_; }

  uint32_t retransmissions() const { return retransmissions_; }
  uint32_t giveUps() const { return give_ups_; }

 private:
  Entry* headFor(uint8_t dest);
  Entry* entries_;
  size_t cap_;
  RetryPolicy policy_;
  Listener* listener_ = nullptr;
  uint32_t order_counter_ = 0;
  uint32_t retransmissions_ = 0;
  uint32_t give_ups_ = 0;
};

template <size_t N>
class ReliableQueueN : public ReliableQueue {
 public:
  explicit ReliableQueueN(const RetryPolicy& p) : ReliableQueue(storage_, N, p) {}

 private:
  Entry storage_[N];
};

class DedupCache {
 public:
  struct Item {
    bool used = false;
    uint32_t txn = 0;
    uint8_t result = 0;
    uint8_t flags = 0;
  };
  static constexpr size_t kSize = 16;
  // Returns true and fills result/flags if txn was seen.
  bool lookup(uint32_t txn, uint8_t& result, uint8_t& flags) const;
  void remember(uint32_t txn, uint8_t result, uint8_t flags);
  void clear();

 private:
  Item items_[kSize];
  size_t next_ = 0;
};

class RecentIds {
 public:
  static constexpr size_t kSize = 32;
  // Returns true if (source, id) was already seen; otherwise records it.
  bool checkAndInsert(uint8_t source, uint32_t id);
  bool contains(uint8_t source, uint32_t id) const;
  void clear();

 private:
  struct Item {
    bool used = false;
    uint8_t source = 0;
    uint32_t id = 0;
  } items_[kSize];
  size_t next_ = 0;
};

class LinkMonitor {
 public:
  explicit LinkMonitor(uint32_t timeout_ms = 3000) : timeout_(timeout_ms) {}
  void setTimeout(uint32_t t) { timeout_ = t; }
  void onRx(uint32_t now) {
    last_rx_ = now;
    seen_ = true;
  }
  // Returns +1 on down->up, -1 on up->down, 0 otherwise.
  int update(uint32_t now);
  bool up() const { return up_; }
  bool everSeen() const { return seen_; }
  uint32_t lastRx() const { return last_rx_; }
  void reset() {
    up_ = false;
    seen_ = false;
  }

 private:
  uint32_t timeout_;
  uint32_t last_rx_ = 0;
  bool seen_ = false;
  bool up_ = false;
};

}  // namespace ufo
