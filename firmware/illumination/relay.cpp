#include "relay.h"

namespace ufo {

namespace {
// wrap-safe "a is at or after b"
bool reached(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }
}  // namespace

RelayController::RelayController(RelayOutput& out, uint8_t count, const bool* configured, uint32_t max_on_ms,
                                 uint32_t cooldown_ms)
    : out_(out), count_(count > kMaxRelays ? kMaxRelays : count), max_on_ms_(max_on_ms), cooldown_ms_(cooldown_ms) {
  for (uint8_t i = 0; i < count_; ++i) relays_[i].configured = configured ? configured[i] : false;
}

void RelayController::begin() {
  for (uint8_t i = 0; i < count_; ++i) {
    relays_[i].on = false;
    relays_[i].cooldown_active = false;
    if (relays_[i].configured) out_.setRelay(i, false);
  }
}

CommandOutcome RelayController::pulse(uint8_t id, uint16_t duration_ms, uint32_t now) {
  CommandOutcome o;
  if (id >= count_ || duration_ms == 0) {
    o.result = AckResult::InvalidParams;
    return o;
  }
  Relay& r = relays_[id];
  if (!r.configured) {
    o.result = AckResult::NotConfigured;
    return o;
  }
  if (r.on) {
    o.result = AckResult::Busy;
    return o;
  }
  if (r.cooldown_active && !reached(now, r.cooldown_until)) {
    o.result = AckResult::Cooldown;
    return o;
  }
  uint32_t d = duration_ms;
  if (d > max_on_ms_) {
    d = max_on_ms_;
    o.flags |= u8(AckFlag::Clamped);
  }
  r.on = true;
  r.on_at = now;
  r.duration = d;
  r.cooldown_active = false;
  ++pulses_;
  out_.setRelay(id, true);
  return o;
}

void RelayController::switchOff(uint8_t id, uint32_t now, RelayEndReason reason) {
  Relay& r = relays_[id];
  if (r.configured) out_.setRelay(id, false);  // always re-assert the OFF level
  if (!r.on) return;
  r.on = false;
  r.cooldown_active = true;
  r.cooldown_until = now + cooldown_ms_;
  if (listener_) listener_->onRelayDone(id, now - r.on_at, reason, now);
}

CommandOutcome RelayController::off(uint8_t id, uint32_t now, RelayEndReason reason) {
  CommandOutcome o;
  if (id == 0xFF) {
    for (uint8_t i = 0; i < count_; ++i) switchOff(i, now, reason);
    return o;
  }
  if (id >= count_) {
    o.result = AckResult::InvalidParams;
    return o;
  }
  if (!relays_[id].configured) {
    o.result = AckResult::NotConfigured;
    return o;
  }
  switchOff(id, now, reason);
  return o;
}

void RelayController::tick(uint32_t now) {
  for (uint8_t i = 0; i < count_; ++i) {
    Relay& r = relays_[i];
    if (!r.on) continue;
    if (now - r.on_at > max_on_ms_) {
      switchOff(i, now, RelayEndReason::Watchdog);
    } else if (now - r.on_at >= r.duration) {
      switchOff(i, now, RelayEndReason::Completed);
    }
  }
}

bool RelayController::anyOn() const { return onMask() != 0; }

uint8_t RelayController::onMask() const {
  uint8_t m = 0;
  for (uint8_t i = 0; i < count_; ++i)
    if (relays_[i].on) m |= static_cast<uint8_t>(1u << i);
  return m;
}

uint16_t RelayController::remainingMs(uint32_t now) const {
  uint32_t best = 0;
  for (uint8_t i = 0; i < count_; ++i) {
    const Relay& r = relays_[i];
    if (!r.on) continue;
    uint32_t elapsed = now - r.on_at;
    uint32_t rem = elapsed >= r.duration ? 0 : r.duration - elapsed;
    if (rem > best) best = rem;
  }
  return best > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(best);
}

uint16_t RelayController::cooldownMs(uint32_t now) const {
  uint32_t best = 0;
  for (uint8_t i = 0; i < count_; ++i) {
    const Relay& r = relays_[i];
    if (!r.cooldown_active || reached(now, r.cooldown_until)) continue;
    uint32_t rem = r.cooldown_until - now;
    if (rem > best) best = rem;
  }
  return best > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(best);
}

}  // namespace ufo
