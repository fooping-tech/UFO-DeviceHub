// Hub protocol engine (pure C++, unit-tested on the host).
//
// The hub has NO game logic. It only:
//   * keeps the PC session (opened by HELLO, closed on PC timeout / new HELLO)
//   * validates and routes PC commands to devices by Device ID
//   * handles device ACKs, retransmissions and ACK timeouts
//   * relays device EVENTs to the PC (store-and-forward, re-sent until EVENT_ACK)
//   * sends heartbeats, detects device link loss, measures RTT
//   * keeps devices in sync with the PC session (SESSION_BEGIN / SESSION_END)
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "device_ids.h"
#include "messages.h"
#include "reliability.h"

namespace ufo {

struct HubChildCounters {
  uint32_t tx_frames = 0;
  uint32_t rx_frames = 0;
  uint32_t commands = 0;
  uint32_t acks = 0;
  uint32_t ack_timeouts = 0;
  uint32_t retransmissions = 0;
  uint32_t hb_sent = 0;
  uint32_t hb_recv = 0;
  uint32_t events = 0;
  uint32_t event_duplicates = 0;
  uint32_t invalid = 0;
  uint32_t reboots = 0;
};

struct HubChild {
  const DeviceEntry* entry = nullptr;
  LinkMonitor link;
  bool hb_valid = false;
  NodeHeartbeat last_hb;
  uint32_t reported_session = 0;
  uint32_t last_session_sync = 0;
  bool session_sync_pending = false;
  uint16_t hb_seq_sent = 0;
  uint32_t hb_sent_at = 0;
  uint32_t rtt_last_ms = 0;
  uint32_t rtt_avg_ms = 0;  // EMA (1/8)
  HubChildCounters c;
};

struct HubCounters {
  uint32_t rx_frames = 0;
  uint32_t rx_unknown_mac = 0;
  uint32_t rx_bad_magic = 0;
  uint32_t rx_bad_version = 0;
  uint32_t rx_bad_length = 0;
  uint32_t rx_bad_type = 0;
  uint32_t rx_bad_id = 0;      // logical id does not match the MAC / wrong destination
  uint32_t rx_bad_payload = 0;
  uint32_t late_acks = 0;
  uint32_t commands_rejected = 0;
  uint32_t pc_events_queued = 0;
  uint32_t pc_events_dropped = 0;  // purged by a session change
  uint32_t pc_event_resends = 0;
  uint32_t sessions_opened = 0;
  uint32_t sessions_closed = 0;
  uint32_t invalidTotal() const {
    return rx_unknown_mac + rx_bad_magic + rx_bad_version + rx_bad_length + rx_bad_type + rx_bad_id +
           rx_bad_payload;
  }
};

struct CommandResultInfo {
  uint8_t dest = 0;
  uint32_t command_id = 0;
  uint8_t result = 0;  // AckResult
  uint8_t flags = 0;   // AckFlag
  uint8_t detail = 0;
  bool from_device = false;
  uint8_t attempts = 0;
};

struct PcEvent {
  uint8_t source = 0;
  uint32_t session = 0;
  uint32_t event_id = 0;
  EventData data;
  uint8_t attempts = 0;
};

enum class SessionCloseReason : uint8_t { PcTimeout = 1, PcRequest = 2, Replaced = 3 };

class HubSink {
 public:
  virtual ~HubSink() {}
  virtual bool sendFrame(const uint8_t mac[6], const uint8_t* data, size_t len) = 0;
  virtual void commandResult(const CommandResultInfo& r) = 0;
  virtual void eventToPc(const PcEvent& e) = 0;
  virtual void statusToPc(uint8_t source, const StatusCommon& c, const IlluminationStatus& il,
                          const DialStatus& dl) = 0;
  virtual void linkChanged(uint8_t id, bool online) = 0;
  virtual void deviceRebooted(uint8_t id, uint32_t boot_id) = 0;
  virtual void sessionClosed(uint32_t session, SessionCloseReason reason) = 0;
  virtual void deviceError(uint8_t source, const ErrorPayload& e) = 0;
};

struct HubConfig {
  RetryPolicy command_policy;
  uint32_t heartbeat_interval_ms = 1000;
  uint32_t link_timeout_ms = 3000;
  uint32_t pc_timeout_ms = 3000;
  uint32_t pc_event_retry_ms = 500;
  uint32_t session_sync_interval_ms = 500;
  size_t max_queue_per_device = 4;
};

class HubCore : private FrameSink, private ReliableQueue::Listener {
 public:
  static constexpr size_t kMaxChildren = 8;
  static constexpr size_t kPcOutbox = 16;

  HubCore(HubSink& sink, const HubConfig& cfg);
  void begin(uint32_t now, uint32_t seed);

  // ---- PC side ----
  void onPcActivity(uint32_t now) { last_pc_rx_ = now; }
  bool openSession(uint32_t session, uint32_t now);
  void closeSession(uint32_t now, SessionCloseReason reason);
  uint32_t session() const { return session_; }
  // Immediate result: Accepted means "queued to the device"; the device result is
  // reported later through HubSink::commandResult (or Timeout).
  AckResult submitCommand(uint8_t dest, uint32_t session, uint32_t command_id, const uint8_t* payload,
                          size_t len, uint32_t now);
  void pcEventAck(uint8_t source, uint32_t event_id);

  // ---- radio side ----
  void onFrame(const uint8_t* src_mac, const uint8_t* data, size_t len, int8_t rssi, uint32_t now);
  void tick(uint32_t now);

  // ---- diagnostics ----
  size_t childCount() const { return child_count_; }
  const HubChild& childAt(size_t i) const { return children_[i]; }
  const HubChild* findChild(uint8_t id) const;
  const HubCounters& counters() const { return counters_; }
  size_t pcOutboxSize() const;
  size_t queuedCommands() const { return queue_.size(); }
  uint32_t uptime(uint32_t now) const { return now - boot_ms_; }

 private:
  struct PcSlot {
    bool used = false;
    PcEvent ev;
    uint32_t last_tx = 0;
  };

  bool sendFrame(uint8_t dest_id, const uint8_t* data, size_t len) override;
  void onGiveUp(const ReliableQueue::Entry& e) override;
  void onRetransmit(const ReliableQueue::Entry& e) override;

  HubChild* childById(uint8_t id);
  void fillHeader(Packet& p, uint8_t dest, MsgType t, uint32_t txn);
  void sendNow(HubChild& ch, Packet& p);
  void queueHubCommand(HubChild& ch, CommandCode code, uint32_t now);
  void failQueued(uint8_t dest, AckResult why);
  void sendAckToChild(HubChild& ch, const Packet& req, AckResult r);
  void handleAck(HubChild& ch, const Packet& p);
  void handleEvent(HubChild& ch, const Packet& p, uint32_t now);
  void handleHeartbeat(HubChild& ch, const Packet& p, uint32_t now);
  void syncSession(HubChild& ch, uint32_t now);
  void purgePcOutbox();

  HubSink& sink_;
  HubConfig cfg_;
  HubChild children_[kMaxChildren];
  size_t child_count_ = 0;
  ReliableQueueN<16> queue_;
  PcSlot pc_outbox_[kPcOutbox];
  RecentIds seen_events_;
  HubCounters counters_;
  uint32_t session_ = 0;
  uint32_t last_pc_rx_ = 0;
  uint32_t boot_ms_ = 0;
  uint32_t last_hb_ = 0;
  uint32_t hub_cmd_counter_ = 1;
  uint16_t next_seq_ = 1;
};

}  // namespace ufo
