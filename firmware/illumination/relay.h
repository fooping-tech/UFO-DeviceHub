// Steam generator relay control (pure logic; GPIO access through RelayOutput).
//
// Safety rules (docs/hardware.md):
//   * OFF at boot (inactive level written before the pin becomes an output)
//   * one pulse is limited to max_on_ms (longer requests are clamped, ACK flag CLAMPED)
//   * re-fire prohibited for cooldown_ms after a pulse ends (ACK COOLDOWN)
//   * a running pulse is not extended (ACK BUSY)
//   * OFF on RELAY_OFF / STOP_ALL / session end / hub link loss
//   * watchdog: forced OFF if a relay is ever ON longer than max_on_ms
// LED rendering is completely separate (led_renderer.*).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "messages.h"
#include "node_core.h"

namespace ufo {

class RelayOutput {
 public:
  virtual ~RelayOutput() {}
  virtual void setRelay(uint8_t id, bool on) = 0;
};

class RelayListener {
 public:
  virtual ~RelayListener() {}
  virtual void onRelayDone(uint8_t id, uint32_t on_ms, RelayEndReason reason, uint32_t now) = 0;
};

class RelayController {
 public:
  static constexpr uint8_t kMaxRelays = 4;

  RelayController(RelayOutput& out, uint8_t count, const bool* configured, uint32_t max_on_ms,
                  uint32_t cooldown_ms);
  void setListener(RelayListener* l) { listener_ = l; }
  void begin();  // forces every relay OFF

  CommandOutcome pulse(uint8_t id, uint16_t duration_ms, uint32_t now);
  // id 0xFF = all relays. Returns InvalidParams for an unknown id.
  CommandOutcome off(uint8_t id, uint32_t now, RelayEndReason reason);
  void allOff(uint32_t now, RelayEndReason reason) { off(0xFF, now, reason); }
  void tick(uint32_t now);

  bool anyOn() const;
  uint8_t onMask() const;
  uint16_t remainingMs(uint32_t now) const;  // max over active relays
  uint16_t cooldownMs(uint32_t now) const;   // max over relays
  uint16_t pulses() const { return pulses_; }
  bool configured(uint8_t id) const { return id < count_ && relays_[id].configured; }
  uint8_t count() const { return count_; }
  uint32_t maxOnMs() const { return max_on_ms_; }

 private:
  struct Relay {
    bool configured = false;
    bool on = false;
    uint32_t on_at = 0;
    uint32_t duration = 0;
    uint32_t cooldown_until = 0;
    bool cooldown_active = false;
  };
  void switchOff(uint8_t id, uint32_t now, RelayEndReason reason);

  RelayOutput& out_;
  RelayListener* listener_ = nullptr;
  Relay relays_[kMaxRelays];
  uint8_t count_;
  uint32_t max_on_ms_;
  uint32_t cooldown_ms_;
  uint16_t pulses_ = 0;
};

}  // namespace ufo
