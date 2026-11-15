#if defined(ARDUINO)

#include "display.h"

#include <M5Dial.h>

namespace ufo {

bool DisplayModel::operator==(const DisplayModel& o) const {
  return device_id == o.device_id && link == o.link && session == o.session && state == o.state && op == o.op &&
         cycle == o.cycle && switch_on == o.switch_on && sim_button == o.sim_button &&
         example_keys == o.example_keys && mac_mismatch == o.mac_mismatch && radio_ok == o.radio_ok &&
         error == o.error;
}

const char* dialStateLabel(DialState s) {
  switch (s) {
    case DialState::Disabled: return "STANDBY";
    case DialState::Ready: return "READY";
    case DialState::Armed: return "WAITING";      // 受取待ち
    case DialState::BallDetected: return "GOT BALL";  // 受取済み
    case DialState::Releasing: return "RELEASE";  // 排出中
    case DialState::Returning: return "RETURN";
    case DialState::Complete: return "DONE";
    case DialState::Error: return "ERROR";
    case DialState::Settings: return "SETTINGS";
  }
  return "?";
}

namespace {
uint16_t stateColor(DialState s) {
  switch (s) {
    case DialState::Armed: return TFT_CYAN;
    case DialState::BallDetected: return TFT_GREEN;
    case DialState::Releasing:
    case DialState::Returning: return TFT_YELLOW;
    case DialState::Complete: return TFT_WHITE;
    case DialState::Error: return TFT_RED;
    default: return TFT_DARKGREY;
  }
}
}  // namespace

void DialDisplay::begin(const char* mac) {
  mac_ = mac;
  M5Dial.Display.fillScreen(TFT_BLACK);
  drawn_ = false;
}

void DialDisplay::update(const DisplayModel& m) {
  settings_drawn_ = false;
  if (drawn_ && m == last_) return;
  last_ = m;
  drawn_ = true;
  draw(m);
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
  if (settings_drawn_ && s == last_settings_) return;
  last_settings_ = s;
  settings_drawn_ = true;
  drawSettings(s);
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

void DialDisplay::draw(const DisplayModel& m) {
  auto& d = M5Dial.Display;
  const int cx = d.width() / 2;
  d.startWrite();
  d.fillScreen(TFT_BLACK);
  d.setTextDatum(middle_center);

  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_WHITE);
  char buf[48];
  snprintf(buf, sizeof(buf), "DIAL #%u  ID 0x%02X", m.dial_number, m.device_id);
  d.drawString(buf, cx, 34);

  const char* link = !m.radio_ok ? "RADIO ERR" : (m.link ? (m.session ? "HUB OK" : "HUB / NO SESSION") : "NO HUB LINK");
  d.setTextColor(!m.radio_ok || !m.link ? TFT_RED : (m.session ? TFT_GREEN : TFT_ORANGE));
  d.drawString(link, cx, 56);

  d.setFont(&fonts::Orbitron_Light_24);
  d.setTextColor(stateColor(m.state));
  d.drawString(dialStateLabel(m.state), cx, 110);

  d.setFont(&fonts::Font2);
  d.setTextColor(TFT_LIGHTGREY);
  snprintf(buf, sizeof(buf), "%s  cycle %u", m.op == OperationMode::Game ? "GAME" : "STANDBY", m.cycle);
  d.drawString(buf, cx, 145);
  snprintf(buf, sizeof(buf), "switch %s%s", m.switch_on ? "ON" : "off", m.sim_button ? "  [SIM BTN]" : "");
  d.drawString(buf, cx, 165);

  int y = 190;
  d.setTextColor(TFT_RED);
  if (m.mac_mismatch) {
    d.drawString("MAC != config", cx, y);
    y += 16;
  }
  if (m.error != ErrorCode::None) {
    snprintf(buf, sizeof(buf), "error %u", static_cast<unsigned>(m.error));
    d.drawString(buf, cx, y);
    y += 16;
  }
  if (m.example_keys && y < 220) {
    d.setTextColor(TFT_ORANGE);
    d.drawString("EXAMPLE KEYS", cx, y);
    y += 16;
  }
  if (y < 222) {
    d.setTextColor(TFT_DARKGREY);
    d.drawString(mac_, cx, 214);
  }
  d.endWrite();
}

}  // namespace ufo

#endif
