// Common device-side protocol engine (pure C++, unit-tested on the host).
//
// Used by the Illumination ATOM and both M5Dials. It owns everything that is not
// device specific:
//   * frame validation (length / magic / version / type, source MAC == hub MAC,
//     source id == hub id, destination == self)
//   * session handling (SESSION_BEGIN / SESSION_END, rejecting physical commands
//     without a valid session, purging old-session state)
//   * command de-duplication by command_id (retransmissions are answered with the
//     stored result and never executed twice)
//   * ACK generation, heartbeat replies, STATUS replies
//   * hub link monitoring (no hub frame for UFO_LINK_TIMEOUT_MS -> link down)
//   * the event outbox (retransmitted with the same event id until the hub ACKs)
//
// The device implements NodeApp. NodeCore never touches hardware.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "messages.h"
#include "reliability.h"

namespace ufo {

struct CommandOutcome {
  AckResult result = AckResult::Accepted;
  uint8_t flags = 0;   // AckFlag::Clamped etc.
  uint8_t detail = 0;
};

class NodeApp {
 public:
  virtual ~NodeApp() {}
  // Called for validated, non-duplicate commands. Session-requiring commands are
  // only passed through when the session is valid. SESSION_* and GET_STATUS are
  // handled by NodeCore and not forwarded.
  virtual CommandOutcome onCommand(CommandCode code, const uint8_t* params, size_t len,
                                   uint32_t command_id, uint32_t now) = 0;
  // session == 0: the session ended (PC gone / hub restarted). Go to the safe state.
  virtual void onSessionChanged(uint32_t session, uint32_t now) = 0;
  virtual void onHubLinkChanged(bool up, uint32_t now) = 0;
  virtual uint8_t state() const = 0;
  virtual uint8_t flags() const = 0;  // device NodeFlag bits (session/link bits are added by NodeCore)
  virtual uint8_t opMode() const { return 0; }
  virtual uint8_t errorCode() const { return 0; }
  // Device specific STATUS part.
  virtual void fillStatus(IlluminationStatus& /*il*/, DialStatus& /*dl*/) {}
};

// Devices report events through this interface (NodeCore implements it).
class EventEmitter {
 public:
  virtual ~EventEmitter() {}
  // Returns the event id, or 0 if the event could not be queued.
  virtual uint32_t emit(const EventData& e, uint32_t now) = 0;
  // Events queued and not yet acknowledged by the hub (a device that streams input
  // skips a frame while the link is backed up, so no stale burst arrives later).
  virtual size_t pending() const { return 0; }
};

// Physical sending of a frame to the hub (ESP-NOW unicast on the device).
class NodeTransport {
 public:
  virtual ~NodeTransport() {}
  virtual bool sendToHub(const uint8_t* data, size_t len) = 0;
};

struct NodeConfig {
  uint8_t system_id = 1;  // UFO system in the venue (selects the packet magic)
  uint8_t self_id = 0;
  DeviceType device_type = DeviceType::Dial;
  uint8_t hub_id = 0x01;
  uint8_t hub_mac[6] = {0};
  uint32_t link_timeout_ms = 3000;
  RetryPolicy event_policy;
  uint8_t fw_major = 0;
  uint8_t fw_minor = 0;
  bool example_keys = false;
};

struct NodeCounters {
  uint32_t rx_frames = 0;
  uint32_t rx_ok = 0;
  uint32_t rx_bad_mac = 0;
  uint32_t rx_bad_frame = 0;   // length / magic / version / type
  uint32_t rx_bad_id = 0;      // source id / destination mismatch
  uint32_t rx_bad_payload = 0;
  uint32_t commands = 0;
  uint32_t duplicates = 0;
  uint32_t rejected_session = 0;
  uint32_t events_sent = 0;
  uint32_t events_dropped = 0;
  uint32_t tx_frames = 0;
  uint32_t heartbeats = 0;
  uint16_t rejectedTotal() const {
    uint32_t n = rx_bad_mac + rx_bad_frame + rx_bad_id + rx_bad_payload;
    return n > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(n);
  }
};

class NodeCore : public EventEmitter, private FrameSink {
 public:
  NodeCore(const NodeConfig& cfg, NodeTransport& tx, NodeApp& app);

  // boot_id: random per boot; id_seed: random start for event ids / sequences.
  void begin(uint32_t now, uint32_t boot_id, uint32_t id_seed);
  // Every received ESP-NOW frame (already copied out of the radio callback).
  void onFrame(const uint8_t* src_mac, const uint8_t* data, size_t len, int8_t rssi, uint32_t now);
  void tick(uint32_t now);

  // Queues an event for the hub. Returns the event id (0 if not queued:
  // no session or outbox full).
  uint32_t emitEvent(const EventData& e, uint32_t now);
  uint32_t emit(const EventData& e, uint32_t now) override { return emitEvent(e, now); }
  size_t pending() const override { return outbox_.size(); }

  uint32_t session() const { return session_; }
  bool sessionValid() const { return session_ != 0; }
  bool hubLinkUp() const { return link_.up(); }
  uint32_t bootId() const { return boot_id_; }
  uint32_t uptime(uint32_t now) const { return now - boot_ms_; }
  const NodeCounters& counters() const { return counters_; }
  size_t pendingEvents() const { return outbox_.size(); }
  int8_t lastRssi() const { return last_rssi_; }
  uint8_t combinedFlags() const;

 private:
  bool sendFrame(uint8_t dest_id, const uint8_t* data, size_t len) override;
  void handleCommand(const Packet& p, uint32_t now);
  void handleAck(const Packet& p);
  void handleHeartbeat(const Packet& p, uint32_t now);
  void sendAck(const Packet& p, uint8_t result, uint8_t flags, uint8_t detail);
  void sendStatus(uint32_t now);
  void sendPacket(Packet& p);
  void setSession(uint32_t s, uint32_t now);
  void fillHeader(Packet& p, MsgType t, uint32_t txn);

  NodeConfig cfg_;
  NodeTransport& tx_;
  NodeApp& app_;
  LinkMonitor link_;
  DedupCache dedup_;
  ReliableQueueN<8> outbox_;
  NodeCounters counters_;
  uint32_t session_ = 0;
  uint32_t boot_id_ = 0;
  uint32_t boot_ms_ = 0;
  uint32_t next_event_id_ = 1;
  uint16_t next_seq_ = 1;
  int8_t last_rssi_ = 0;
};

}  // namespace ufo
