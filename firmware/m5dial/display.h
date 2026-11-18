// M5Dial round display. The main screen is the animated pixel art of
// dial_scene.h (80 x 80, shown 3 x); the menu and the settings mode are plain
// text screens that redraw only when their model changes.
#pragma once

#if defined(ARDUINO)

#include <stdint.h>

#include "dial_menu.h"
#include "dial_scene.h"
#include "messages.h"

namespace ufo {

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

// Menu screen (turn the dial outside the settings mode).
struct MenuModel {
  MenuItem item = MenuItem::Close;
  const char* result = nullptr;  // outcome of the last press, shown instead of the hint
  bool result_ok = false;
  bool link = false;     // hub link + session: GAME RESTART / GAME END can be sent
  bool servo_busy = false;
  bool operator==(const MenuModel& o) const;
};

class DialDisplay {
 public:
  static constexpr uint32_t kFrameMs = 50;  // main screen: 20 frames per second at most
  void begin(const char* mac);
  void update(const DisplayModel& m, uint32_t now);  // call every loop: draws a frame every kFrameMs
  void updateSettings(const SettingsModel& s);
  void updateMenu(const MenuModel& m);
  void message(const char* line1, const char* line2);  // full screen notice (reset / power off)

 private:
  void pushScene();
  void drawSettings(const SettingsModel& s);
  void drawMenu(const MenuModel& m);
  DialScene scene_;
  uint32_t last_frame_ = 0;
  SettingsModel last_settings_;
  MenuModel last_menu_;
  bool menu_drawn_ = false;
  bool drawn_ = false;
  bool settings_drawn_ = false;
  const char* mac_ = "";
};

}  // namespace ufo

#endif
