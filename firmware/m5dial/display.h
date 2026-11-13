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

class DialDisplay {
 public:
  void begin(const char* mac);
  void update(const DisplayModel& m);

 private:
  void draw(const DisplayModel& m);
  DisplayModel last_;
  bool drawn_ = false;
  const char* mac_ = "";
};

const char* dialStateLabel(DialState s);

}  // namespace ufo

#endif
