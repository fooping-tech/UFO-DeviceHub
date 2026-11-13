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
  if (drawn_ && m == last_) return;
  last_ = m;
  drawn_ = true;
  draw(m);
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
