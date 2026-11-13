// ACK / retransmit / give-up, de-duplication and link monitoring.
#include <unity.h>

#include <vector>

#include "../support.h"
#include "reliability.h"

using namespace ufo;

void setUp() {}
void tearDown() {}

struct Sink : FrameSink {
  std::vector<std::pair<uint8_t, uint32_t>> sent;  // (dest, txn)
  bool sendFrame(uint8_t dest, const uint8_t* d, size_t n) override {
    Packet p;
    decodePacket(d, n, p);
    sent.push_back({dest, p.h.txn});
    return true;
  }
};

struct Listener : ReliableQueue::Listener {
  std::vector<uint32_t> gave_up;
  void onGiveUp(const ReliableQueue::Entry& e) override { gave_up.push_back(e.txn); }
};

static Packet pkt(uint8_t dest, uint32_t txn) {
  Packet p;
  p.h.source = 1;
  p.h.destination = dest;
  p.h.type = MsgType::Command;
  p.h.txn = txn;
  p.h.session = 5;
  p.h.payload_len = 1;
  p.payload[0] = 0x03;
  return p;
}

void test_retransmit_then_give_up() {
  RetryPolicy pol;  // 100 ms, 3 retries
  ReliableQueueN<4> q(pol);
  Listener l;
  q.setListener(&l);
  Sink s;
  TEST_ASSERT_TRUE(q.push(0x20, pkt(0x20, 7), 0));
  q.tick(0, s);
  TEST_ASSERT_EQUAL(1, s.sent.size());
  q.tick(99, s);
  TEST_ASSERT_EQUAL(1, s.sent.size());  // not yet
  q.tick(100, s);
  q.tick(200, s);
  q.tick(300, s);
  TEST_ASSERT_EQUAL(4, s.sent.size());  // 1 + 3 retries, same txn every time
  for (auto& x : s.sent) TEST_ASSERT_EQUAL(7, x.second);
  TEST_ASSERT_EQUAL(0, l.gave_up.size());
  q.tick(399, s);
  TEST_ASSERT_EQUAL(0, l.gave_up.size());
  q.tick(400, s);
  TEST_ASSERT_EQUAL(1, l.gave_up.size());
  TEST_ASSERT_EQUAL(0, q.size());
  TEST_ASSERT_EQUAL(3, q.retransmissions());
}

void test_ack_stops_retransmission_and_unblocks_fifo() {
  RetryPolicy pol;
  ReliableQueueN<4> q(pol);
  Sink s;
  q.push(0x20, pkt(0x20, 1), 0);
  q.push(0x20, pkt(0x20, 2), 0);
  q.push(0x10, pkt(0x10, 3), 0);
  q.tick(0, s);
  // one in flight per destination, destinations in parallel
  TEST_ASSERT_EQUAL(2, s.sent.size());
  TEST_ASSERT_TRUE(q.acknowledge(0x20, MsgType::Command, 1));
  TEST_ASSERT_FALSE(q.acknowledge(0x20, MsgType::Command, 1));  // duplicate ACK
  TEST_ASSERT_FALSE(q.acknowledge(0x21, MsgType::Command, 3));  // wrong device
  q.tick(10, s);
  TEST_ASSERT_EQUAL(3, s.sent.size());
  TEST_ASSERT_EQUAL(2, s.sent.back().second);
}

void test_retry_forever_for_events() {
  RetryPolicy pol;
  pol.retry_forever = true;
  pol.slow_retry_ms = 500;
  ReliableQueueN<2> q(pol);
  Listener l;
  q.setListener(&l);
  Sink s;
  q.push(1, pkt(1, 99), 0);
  for (uint32_t t = 0; t <= 5000; t += 10) q.tick(t, s);
  TEST_ASSERT_EQUAL(0, l.gave_up.size());
  TEST_ASSERT_TRUE(s.sent.size() >= 4 + 8);  // fast phase + slow retries
  TEST_ASSERT_TRUE(q.acknowledge(1, MsgType::Command, 99));
}

void test_full_and_purge() {
  RetryPolicy pol;
  ReliableQueueN<2> q(pol);
  TEST_ASSERT_TRUE(q.push(1, pkt(1, 1), 0));
  TEST_ASSERT_TRUE(q.push(1, pkt(1, 2), 0));
  TEST_ASSERT_FALSE(q.push(1, pkt(1, 3), 0));
  TEST_ASSERT_EQUAL(2, q.purgeSession(6));  // all entries are session 5
  TEST_ASSERT_EQUAL(0, q.size());
}

void test_dedup_cache() {
  DedupCache c;
  uint8_t r, f;
  TEST_ASSERT_FALSE(c.lookup(5, r, f));
  c.remember(5, 0x01, 0x02);
  TEST_ASSERT_TRUE(c.lookup(5, r, f));
  TEST_ASSERT_EQUAL(0x01, r);
  TEST_ASSERT_EQUAL(0x02, f);
  for (uint32_t i = 100; i < 100 + DedupCache::kSize; ++i) c.remember(i, 0, 0);
  TEST_ASSERT_FALSE(c.lookup(5, r, f));  // evicted after kSize newer commands
  c.clear();
  TEST_ASSERT_FALSE(c.lookup(100, r, f));
}

void test_recent_ids_and_link_monitor() {
  RecentIds ids;
  TEST_ASSERT_FALSE(ids.checkAndInsert(0x20, 1));
  TEST_ASSERT_TRUE(ids.checkAndInsert(0x20, 1));
  TEST_ASSERT_FALSE(ids.checkAndInsert(0x21, 1));  // same id, other device
  LinkMonitor m(3000);
  TEST_ASSERT_EQUAL(0, m.update(0));
  m.onRx(10);
  TEST_ASSERT_EQUAL(1, m.update(10));
  TEST_ASSERT_EQUAL(0, m.update(3009));
  TEST_ASSERT_EQUAL(-1, m.update(3010));
  TEST_ASSERT_FALSE(m.up());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_retransmit_then_give_up);
  RUN_TEST(test_ack_stops_retransmission_and_unblocks_fifo);
  RUN_TEST(test_retry_forever_for_events);
  RUN_TEST(test_full_and_purge);
  RUN_TEST(test_dedup_cache);
  RUN_TEST(test_recent_ids_and_link_monitor);
  return UNITY_END();
}
