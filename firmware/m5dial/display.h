// M5Dial round display: device id, hub link, receiver / dispenser state, errors.
// Redraws only when the shown model changes (no flicker, cheap in loop()).
#pragma once

#if defined(ARDUINO)

#include <stdint.h>

#include "messages.h"

namespace ufo {

struct DisplayModel {
  uint8_t device_id = 0;
  uint8_t dial_number = 0;
  bool link = false;
  bool session = false;
  DialState state = DialState::Disabled;
  OperationMode op = OperationMode::Standby;
  uint16_t cycle = 0;
  bool switch_on = false;
  bool sim_button = false;
  bool example_keys = false;
  bool mac_mismatch = false;
  bool radio_ok = true;
  ErrorCode error = ErrorCode::None;
  bool operator==(const DisplayModel& o) const;
};

// Settings (hardware test) screen.
enum class SettingsItem : uint8_t { Release, Angle, Led, Switch, Exit, Count };
const char* settingsItemLabel(SettingsItem i);

struct SettingsModel {
  SettingsItem item = SettingsItem::Release;
  bool adjusting = false;     // ANGLE: the dial turns the servo
  uint8_t servo_deg = 0;
  bool servo_busy = false;
  const char* led_pattern = "";
  bool switch_on = false;
  uint16_t switch_count = 0;
  int8_t receiver_pin = -1;
  bool operator==(const SettingsModel& o) const;
};

class DialDisplay {
 public:
  void begin(const char* mac);
  void update(const DisplayModel& m);
  void updateSettings(const SettingsModel& s);

 private:
  void draw(const DisplayModel& m);
  void drawSettings(const SettingsModel& s);
  DisplayModel last_;
  SettingsModel last_settings_;
  bool drawn_ = false;
  bool settings_drawn_ = false;
  const char* mac_ = "";
};

const char* dialStateLabel(DialState s);

}  // namespace ufo

#endif
