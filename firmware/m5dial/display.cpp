#if defined(ARDUINO)

#include "display.h"

#include <M5Dial.h>
#include <string.h>

namespace ufo {

void DialDisplay::begin(const char* mac) {
  mac_ = mac;
  M5Dial.Display.fillScreen(TFT_BLACK);
  drawn_ = false;
}

void DialDisplay::update(const DisplayModel& m, uint32_t now) {
  const bool back = settings_drawn_ || menu_drawn_;  // coming back from another screen: draw at once
  settings_drawn_ = false;
  menu_drawn_ = false;
  if (drawn_ && !back && now - last_frame_ < kFrameMs) return;
  last_frame_ = now;
  drawn_ = true;
  DisplayModel model = m;
  model.mac = mac_;
  scene_.render(model, now);
  pushScene();
}

// One scene pixel = kZoom x kZoom display pixels, three display rows per transfer.
void DialDisplay::pushScene() {
  constexpr int kZoom = 3;
  constexpr int n = DialScene::kSize;
  static lgfx::rgb565_t rows[n * kZoom * kZoom];
  auto& d = M5Dial.Display;
  const int x0 = (d.width() - n * kZoom) / 2, y0 = (d.height() - n * kZoom) / 2;
  const uint16_t* src = scene_.pixels();
  d.startWrite();
  for (int y = 0; y < n; ++y, src += n) {
    lgfx::rgb565_t* p = rows;
    for (int x = 0; x < n; ++x) {
      lgfx::rgb565_t c;
      c.raw = src[x];
      for (int k = 0; k < kZoom; ++k) *p++ = c;
    }
    for (int k = 1; k < kZoom; ++k) memcpy(rows + k * n * kZoom, rows, sizeof(rows[0]) * n * kZoom);
    d.pushImage(x0, y0 + y * kZoom, n * kZoom, kZoom, rows);
  }
  d.endWrite();
}

bool SettingsModel::operator==(const SettingsModel& o) const {
  return item == o.item && adjusting == o.adjusting && servo_deg == o.servo_deg && servo_busy == o.servo_busy &&
         led_pattern == o.led_pattern && switch_on == o.switch_on && switch_count == o.switch_count &&
         receiver_pin == o.receiver_pin;
}

const char* settingsItemLabel(SettingsItem i) {
  switch (i) {
    case SettingsItem::Release: return "SERVO TEST";
    case SettingsItem::Angle: return "SERVO ANGLE";
    case SettingsItem::Led: return "LED TEST";
    case SettingsItem::Switch: return "SWITCH TEST";
    case SettingsItem::Exit: return "EXIT";
    default: return "?";
  }
}

void DialDisplay::updateSettings(const SettingsModel& s) {
  drawn_ = false;  // the normal screen is redrawn on return
  menu_drawn_ = false;
  if (settings_drawn_ && s == last_settings_) return;
  last_settings_ = s;
  settings_drawn_ = true;
  drawSettings(s);
}

bool MenuModel::operator==(const MenuModel& o) const {
  return item == o.item && result == o.result && result_ok == o.result_ok && link == o.link &&
         servo_busy == o.servo_busy;
}

void DialDisplay::updateMenu(const MenuModel& m) {
  drawn_ = false;  // the normal screen is redrawn on return
  settings_drawn_ = false;
  if (menu_drawn_ && m == last_menu_) return;
  last_menu_ = m;
  menu_drawn_ = true;
  drawMenu(m);
}

void DialDisplay::drawMenu(const MenuModel& m) {
  auto& d = M5Dial.Display;
  const int cx = d.width() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_YELLOW);
  d.drawString("MENU", cx, 30);

  // previous / current / next item
  const int n = static_cast<int>(MenuItem::Count);
  const int cur = static_cast<int>(m.item);
  d.setTextColor(TFT_DARKGREY);
  d.drawString(menuItemLabel(static_cast<MenuItem>((cur + n - 1) % n)), cx, 62);
  d.drawString(menuItemLabel(static_cast<MenuItem>((cur + 1) % n)), cx, 138);
  d.setFont(&fonts::FreeSansBold12pt7b);
  const bool danger = m.item == MenuItem::Reset || m.item == MenuItem::PowerOff || m.item == MenuItem::GameEnd;
  d.setTextColor(danger ? TFT_ORANGE : TFT_WHITE);
  d.drawString(menuItemLabel(m.item), cx, 100);

  d.setFont(&fonts::Font2);
  if (m.result) {
    d.setTextColor(m.result_ok ? TFT_GREEN : TFT_RED);
    d.drawString(m.result, cx, 172);
  } else {
    const char* hint = "press: run";
    bool warn = false;
    switch (m.item) {
      case MenuItem::Close: hint = "press: back"; break;
      case MenuItem::Reset: hint = "press: restart this dial"; break;
      case MenuItem::PowerOff: hint = "press: power off"; break;
      case MenuItem::Release:
        hint = m.servo_busy ? "servo running..." : "press: release now";
        warn = m.servo_busy;
        break;
      case MenuItem::GameRestart:
      case MenuItem::GameEnd:
        hint = m.link ? "press: send to the game" : "no game connected";
        warn = !m.link;
        break;
      default: break;
    }
    d.setTextColor(warn ? TFT_RED : TFT_CYAN);
    d.drawString(hint, cx, 172);
  }
  d.setTextColor(TFT_DARKGREY);
  d.drawString("turn: choose", cx, 200);
  d.endWrite();
}

void DialDisplay::message(const char* line1, const char* line2) {
  auto& d = M5Dial.Display;
  const int cx = d.width() / 2;
  drawn_ = settings_drawn_ = menu_drawn_ = false;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);
  d.setFont(&fonts::FreeSansBold12pt7b);
  d.setTextColor(TFT_WHITE);
  d.drawString(line1, cx, 105);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_LIGHTGREY);
  d.drawString(line2, cx, 140);
  d.endWrite();
}

void DialDisplay::drawSettings(const SettingsModel& s) {
  auto& d = M5Dial.Display;
  const int cx = d.width() / 2;
  char buf[48];
  d.startWrite();
  d.fillScreen(TFT_NAVY);
  d.setTextDatum(middle_center);
  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_YELLOW);
  d.drawString("SETTINGS", cx, 30);

  // previous / current / next item
  const int n = static_cast<int>(SettingsItem::Count);
  const int cur = static_cast<int>(s.item);
  d.setTextColor(TFT_DARKGREY);
  d.drawString(settingsItemLabel(static_cast<SettingsItem>((cur + n - 1) % n)), cx, 58);
  d.drawString(settingsItemLabel(static_cast<SettingsItem>((cur + 1) % n)), cx, 128);
  d.setFont(&fonts::Orbitron_Light_24);
  d.setTextColor(s.adjusting ? TFT_ORANGE : TFT_WHITE);
  d.drawString(settingsItemLabel(s.item), cx, 92);

  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_CYAN);
  switch (s.item) {
    case SettingsItem::Release: d.drawString(s.servo_busy ? "running..." : "press: release -> back", cx, 156); break;
    case SettingsItem::Angle:
      snprintf(buf, sizeof(buf), s.adjusting ? "turn: %u deg  press: done" : "press: adjust (%u deg)", s.servo_deg);
      d.drawString(buf, cx, 156);
      break;
    case SettingsItem::Led:
      snprintf(buf, sizeof(buf), "%s  press: next", s.led_pattern);
      d.drawString(buf, cx, 156);
      break;
    case SettingsItem::Switch: d.drawString("press: reset count", cx, 156); break;
    case SettingsItem::Exit: d.drawString("press / hold: exit", cx, 156); break;
    default: break;
  }

  d.setTextColor(s.switch_on ? TFT_GREEN : TFT_LIGHTGREY);
  if (s.receiver_pin >= 0)
    snprintf(buf, sizeof(buf), "switch G%d %s  x%u", s.receiver_pin, s.switch_on ? "ON" : "off", s.switch_count);
  else
    snprintf(buf, sizeof(buf), "switch not wired");
  d.drawString(buf, cx, 180);
  d.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof(buf), "servo %u deg%s", s.servo_deg, s.servo_busy ? " *" : "");
  d.drawString(buf, cx, 200);
  d.endWrite();
}

}  // namespace ufo

#endif
