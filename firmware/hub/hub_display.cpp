#if defined(ARDUINO) && UFO_HUB_DISPLAY

#include "hub_display.h"

#include <M5Unified.h>
#include <string.h>

namespace ufo {

namespace {
HubScreen g_last;
bool g_drawn = false;
const char* const kChildNames[] = {"ILL", "D1", "D2", "D3", "D4", "D5", "D6", "D7"};
uint16_t systemColor(uint8_t id) { return id == 1 ? TFT_CYAN : (id == 2 ? TFT_ORANGE : TFT_MAGENTA); }
}  // namespace

bool HubScreen::operator==(const HubScreen& o) const {
  return system_id == o.system_id && channel == o.channel && radio_ok == o.radio_ok && pc_session == o.pc_session &&
         online_mask == o.online_mask && child_count == o.child_count && example_keys == o.example_keys &&
         (problem == o.problem || (problem && o.problem && strcmp(problem, o.problem) == 0));
}

void hubDisplayBegin() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;  // Serial belongs to the JSON Lines link
  cfg.clear_display = true;
  M5.begin(cfg);
  M5.Display.setBrightness(80);
  M5.Display.fillScreen(TFT_BLACK);
}

void hubDisplayShow(const HubScreen& s) {
  if (g_drawn && s == g_last) return;
  g_last = s;
  g_drawn = true;
  auto& d = M5.Display;
  const uint16_t col = systemColor(s.system_id);
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  // header band in the system colour
  d.fillRect(0, 0, d.width(), 44, col);
  d.setTextColor(TFT_BLACK, col);
  d.setTextDatum(middle_center);
  d.setFont(&fonts::Font4);
  char buf[32];
  snprintf(buf, sizeof(buf), "SYSTEM %u", s.system_id);
  d.drawString(buf, d.width() / 2, 22);

  d.setFont(&fonts::Font2);
  d.setTextDatum(top_left);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "CH %u  %s", s.channel, s.radio_ok ? "radio ok" : "RADIO OFF");
  d.drawString(buf, 4, 50);
  d.setTextColor(s.pc_session ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
  d.drawString(s.pc_session ? "PC: session" : "PC: waiting", 4, 68);

  int x = 4;
  for (uint8_t i = 0; i < s.child_count && i < 8; ++i) {
    bool on = (s.online_mask >> i) & 1;
    d.fillCircle(x + 5, 95, 5, on ? TFT_GREEN : TFT_DARKGREY);
    d.setTextColor(on ? TFT_WHITE : TFT_DARKGREY, TFT_BLACK);
    d.drawString(kChildNames[i], x + 13, 88);
    x += 42;
  }
  if (s.problem || s.example_keys) {
    d.fillRect(0, 108, d.width(), 20, TFT_RED);
    d.setTextColor(TFT_WHITE, TFT_RED);
    d.setFont(&fonts::Font0);
    d.drawString(s.problem ? s.problem : "EXAMPLE KEYS", 2, 114);
  }
  d.endWrite();
}

}  // namespace ufo

#endif
