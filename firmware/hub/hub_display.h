// Hub status screen (M5AtomS3R, 128x128). Built only with -DUFO_HUB_DISPLAY=1.
// Shows which UFO system this hub belongs to (big, colour coded) so that the two
// installations in the venue can be told apart at a glance.
#pragma once

#if defined(ARDUINO) && UFO_HUB_DISPLAY

#include <stdint.h>

namespace ufo {

struct HubScreen {
  uint8_t system_id = 1;
  uint8_t channel = 1;
  bool radio_ok = true;
  bool pc_session = false;
  uint8_t online_mask = 0;  // bit i = child i online
  uint8_t child_count = 0;
  bool example_keys = false;
  const char* problem = nullptr;  // config error / warning text (nullptr = none)
  bool operator==(const HubScreen& o) const;
};

void hubDisplayBegin();
void hubDisplayShow(const HubScreen& s);  // redraws only when something changed

}  // namespace ufo

#endif
