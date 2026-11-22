// Staff remote: M5StopWatch (ESP32-S3, 466x466 round AMOLED, touch, BMI270, two keys).
// Device ID 0x30. It reports to the hub; the game (CoreXY_UFO_Game) decides what happens.
//
// A ring menu on the round screen: five segments around a centre, pixel-art icons that move.
// Two pages; the blue key (B, G1) switches between them (the ring builds up again).
//
//   STAFF  ring: つぎへ F6 / とばす F7 / ふっき F9 (the arm moves) / メニュー F8 / きりかえ F10,
//          centre: ストップ. Yellow key (A, G2) = つぎへ.
//   PLAY   ring: つかむ / はなす / ポータル / ストップ / じょうげ (Z on / off),
//          centre: the pixel UFO is the stick. Hold the yellow key and tilt = move (the tilt
//          when the key went down is the centre; let go = stop). つかむ / はなす / ポータル show
//          a hand shape (fist / peace / one finger) for a moment, as the camera would see it;
//          the UFO shines its beam meanwhile.
//
// Every touch is a press-and-hold: the segment fills while the finger stays on it, with a
// click at each third, and the operation goes when it is full (a firm double click and a
// burst of pixels). Let go early or slide off: nothing. ふっき needs the longest hold. The
// screen dims after REMOTE_DIM_S without input; the first touch / key then only wakes it.
#include <Arduino.h>
#include <M5Unified.h>
#include <math.h>

#include "device_ids.h"
#include "node_runtime.h"
#include "remote_controller.h"
#include "remote_ui.h"
#include "ufo_config.h"
#include "version.h"

// The StopWatch's IMU is turned 90 degrees to the screen and its forward axis is reversed
// (checked on the device 2026-11-22): these defaults make "tilt right" = right on the screen.
#ifndef REMOTE_TILT_SWAP_XY
#define REMOTE_TILT_SWAP_XY 1
#endif
#ifndef REMOTE_TILT_INVERT_X
#define REMOTE_TILT_INVERT_X 0
#endif
#ifndef REMOTE_TILT_INVERT_Y
#define REMOTE_TILT_INVERT_Y 1
#endif
#ifndef REMOTE_TILT_DEAD_DEG
#define REMOTE_TILT_DEAD_DEG 4
#endif
#ifndef REMOTE_TILT_FULL_DEG
#define REMOTE_TILT_FULL_DEG 25
#endif
#ifndef REMOTE_HOLD_MS
#define REMOTE_HOLD_MS 450  // every touch operation: press this long
#endif
#ifndef REMOTE_STOP_HOLD_MS
#define REMOTE_STOP_HOLD_MS 250  // ストップ: short, but still not a brush
#endif
#ifndef REMOTE_RECOVER_HOLD_MS
#define REMOTE_RECOVER_HOLD_MS 1200  // ふっき moves the arm
#endif
#ifndef REMOTE_DIM_S
#define REMOTE_DIM_S 60
#endif
#ifndef REMOTE_BRIGHTNESS
#define REMOTE_BRIGHTNESS 160
#endif
#ifndef REMOTE_VIBRATION
#define REMOTE_VIBRATION 255  // strength of the clicks, 0 = no vibration
#endif

using namespace ufo;
namespace ui = remote_ui;

namespace {

RemoteConfig makeConfig() {
  RemoteConfig c;
  c.swap_xy = REMOTE_TILT_SWAP_XY != 0;
  c.invert_x = REMOTE_TILT_INVERT_X != 0;
  c.invert_y = REMOTE_TILT_INVERT_Y != 0;
  c.dead_deg = REMOTE_TILT_DEAD_DEG;
  c.full_deg = REMOTE_TILT_FULL_DEG;
  return c;
}

RemoteController g_app(makeConfig());
NodeRuntime g_runtime(static_cast<uint8_t>(DeviceId::StaffRemote), g_app);
M5Canvas g_canvas(&M5.Display);
bool g_canvas_ok = false;
ui::Haptics g_haptics(REMOTE_VIBRATION);
ui::Burst g_burst;

// ---- geometry: a ring of five segments around a centre ----------------------------
int CX = 233, CY = 233;           // set from the display in setup()
constexpr int kInner = 112;       // ring inner radius
constexpr int kOuter = 220;       // ring outer radius
constexpr int kCentre = 62;       // centre button radius (the status lines go above / below it)
constexpr int kStick = 52;        // the play page's stick radius
constexpr int kSegments = 5;
constexpr float kSegDeg = 360.0f / kSegments;
constexpr float kGapDeg = 2.5f;
constexpr float kFirstDeg = -90.0f - kSegDeg / 2;  // segment 0 centred at the top
constexpr float kDeg = 57.2957795f;
constexpr uint32_t kIntroStepMs = 45, kIntroGrowMs = 140;

// ---- palette (AMOLED: true black, muted segments, the colour shows when it matters) ----
struct Rgb {
  uint8_t r, g, b;
};
uint16_t shade(Rgb c, float k) {
  if (k > 1) k = 1;
  return M5.Display.color565(static_cast<uint8_t>(c.r * k), static_cast<uint8_t>(c.g * k),
                             static_cast<uint8_t>(c.b * k));
}
constexpr Rgb kTeal{38, 196, 170}, kBlue{80, 140, 255}, kSlate{150, 160, 185}, kViolet{170, 110, 255},
    kAmber{255, 170, 40}, kRed{240, 64, 64}, kGreen{70, 210, 110}, kCyan{60, 200, 240};

enum class Page : uint8_t { Staff, Play };
enum class Act : uint8_t { None, Next, Skip, Menu, Recover, Switch, Stop, Fist, Peace, Point, ZMode };
// how an icon moves: slide right, bob up and down, spin, swap (half turns), pulse
enum class Anim : uint8_t { Slide, Bob, Spin, Swap, Pulse };

struct Item {
  Act act;
  const char* label;
  const char* sub;  // the game's key / the hand shape
  const char* const* sprite;
  Anim anim;
  Rgb color;
};

const Item kStaffRing[kSegments] = {
    {Act::Next, "つぎへ", "F6", ui::kNext, Anim::Slide, kTeal},
    {Act::Skip, "とばす", "F7", ui::kSkip, Anim::Slide, kBlue},
    {Act::Recover, "ふっき", "F9", ui::kRecover, Anim::Spin, kAmber},
    {Act::Menu, "メニュー", "F8", ui::kHome, Anim::Bob, kSlate},
    {Act::Switch, "きりかえ", "F10", ui::kSwitch, Anim::Swap, kViolet},
};
const Item kPlayRing[kSegments] = {
    {Act::Fist, "つかむ", "グー", ui::kFist, Anim::Bob, kGreen},
    {Act::Peace, "はなす", "パー", ui::kPeace, Anim::Bob, kCyan},
    {Act::Point, "ポータル", "ゆび1", ui::kPoint, Anim::Bob, kViolet},
    {Act::Stop, "ストップ", "", ui::kStop, Anim::Pulse, kRed},
    {Act::ZMode, "じょうげ", "", ui::kUpDown, Anim::Bob, kSlate},
};
const Item kStaffCentre = {Act::Stop, "ストップ", "", ui::kStop, Anim::Pulse, kRed};

Page g_page = Page::Staff;
uint32_t g_page_ms = 0;        // when the page was shown (the ring builds up)
const Item* g_hold = nullptr;  // the item under the finger since the touch began
uint32_t g_hold_ms = 0;
int g_detents = 0;             // clicks given during this hold
bool g_fired = false;
char g_msg[48] = "";
bool g_msg_ok = true;
uint32_t g_msg_until = 0;
const Item* g_flash = nullptr;  // the item just sent (a short white flash)
uint32_t g_flash_until = 0;
uint32_t g_last_input = 0;
bool g_dim = false;
bool g_dirty = true;
uint32_t g_last_draw = 0;

bool linkOk() { return g_runtime.core().hubLinkUp() && g_runtime.core().sessionValid(); }

uint32_t holdMs(const Item& it) {
  if (it.act == Act::Recover) return REMOTE_RECOVER_HOLD_MS;
  if (it.act == Act::Stop) return REMOTE_STOP_HOLD_MS;
  return REMOTE_HOLD_MS;
}

const Item* ring() { return g_page == Page::Staff ? kStaffRing : kPlayRing; }
int segIndex(const Item* it) {
  const Item* r = ring();
  return it >= r && it < r + kSegments ? static_cast<int>(it - r) : -1;
}
float segMidDeg(int i) { return kFirstDeg + (i + 0.5f) * kSegDeg; }

void say(const char* text, bool ok, uint32_t now) {
  snprintf(g_msg, sizeof(g_msg), "%s", text);
  g_msg_ok = ok;
  g_msg_until = now + 1600;
  g_dirty = true;
}

StaffAction staffOf(Act a) {
  switch (a) {
    case Act::Next: return StaffAction::Next;
    case Act::Skip: return StaffAction::Skip;
    case Act::Menu: return StaffAction::Menu;
    case Act::Recover: return StaffAction::Recover;
    case Act::Switch: return StaffAction::Switch;
    default: return StaffAction::Stop;
  }
}

void notSent(uint32_t now) {
  say(linkOk() ? "おくれない もういちど" : "つながっていない", false, now);
  g_haptics.play(ui::kError, now);
}

// The burst of pixels from where `it` is on the screen.
void celebrate(const Item& it, uint32_t now) {
  g_flash = &it;
  g_flash_until = now + 200;
  const int i = segIndex(&it);
  if (i < 0) {
    g_burst.fire(CX, CY, 0, 0, shade(it.color, 1), now);
    return;
  }
  const float a = segMidDeg(i) / kDeg, rm = (kInner + kOuter) * 0.5f;
  g_burst.fire(CX + static_cast<int>(rm * cosf(a)), CY + static_cast<int>(rm * sinf(a)), cosf(a), sinf(a),
               shade(it.color, 1), now);
}

bool sendStaff(Act a, const char* label, uint32_t now) {
  uint32_t id = g_app.staffAction(staffOf(a), now);
  if (id) {
    say(label, true, now);
    g_haptics.play(ui::kClick, now);
  } else {
    notSent(now);
  }
  Serial.printf("staff %s: %s\n", staffActionName(static_cast<uint8_t>(staffOf(a))), id ? "sent" : "NOT sent");
  return id != 0;
}

void run(const Item& it, uint32_t now) {
  bool ok = true;
  switch (it.act) {
    case Act::Next:
    case Act::Skip:
    case Act::Menu:
    case Act::Switch:
    case Act::Recover:
    case Act::Stop:
      ok = sendStaff(it.act, it.label, now);
      break;
    case Act::Fist:
    case Act::Peace:
    case Act::Point:
      if (!linkOk()) {
        notSent(now);
        ok = false;
        break;
      }
      g_app.showPose(it.act == Act::Fist    ? RemotePose::Fist
                     : it.act == Act::Peace ? RemotePose::Peace
                                            : RemotePose::Point,
                     now);
      say(it.label, true, now);
      g_haptics.play(ui::kClick, now);
      break;
    case Act::ZMode:
      g_app.setZMode(!g_app.zMode());
      say(g_app.zMode() ? "じょうげ オン" : "じょうげ オフ", true, now);
      g_haptics.play(ui::kClick, now);
      break;
    default:
      break;
  }
  if (ok) celebrate(it, now);
}

// The item under (x, y): a ring segment, or the staff page's centre (ストップ).
const Item* hit(int x, int y) {
  const float dx = x - CX, dy = y - CY;
  const float r = sqrtf(dx * dx + dy * dy);
  if (r <= kCentre + 8) return g_page == Page::Staff ? &kStaffCentre : nullptr;
  if (r < kInner - 6 || r > kOuter + 14) return nullptr;
  float a = atan2f(dy, dx) * kDeg - kFirstDeg;  // screen angle, clockwise
  while (a < 0) a += 360.0f;
  while (a >= 360.0f) a -= 360.0f;
  const int i = static_cast<int>(a / kSegDeg);
  const float within = a - i * kSegDeg;
  if (within < kGapDeg || within > kSegDeg - kGapDeg) return nullptr;  // on a gap: nothing
  return &ring()[i];
}

bool wake(uint32_t now) {  // true = this input only woke the screen
  g_last_input = now;
  if (!g_dim) return false;
  g_dim = false;
  M5.Display.setBrightness(REMOTE_BRIGHTNESS);
  g_dirty = true;
  return true;
}

void showPage(Page p, uint32_t now) {
  g_page = p;
  g_page_ms = now;
  g_hold = nullptr;
  g_dirty = true;
}

void pollInput(uint32_t now) {
  if (M5.BtnB.wasPressed() && !wake(now)) {  // the blue key: the other page
    showPage(g_page == Page::Staff ? Page::Play : Page::Staff, now);
    g_haptics.play(ui::kSoft, now);
  }
  if (M5.BtnA.wasPressed()) {  // the yellow key: つぎへ on the staff page, move on the play page
    if (!wake(now) && g_page == Page::Staff && sendStaff(Act::Next, "つぎへ", now)) celebrate(kStaffRing[0], now);
    if (g_page == Page::Play) g_haptics.play(ui::kTick, now);
  }
  if (M5.BtnA.isPressed()) g_last_input = now;

  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    if (wake(now)) {
      g_hold = nullptr;
      return;
    }
    g_hold = hit(t.x, t.y);
    g_hold_ms = now;
    g_detents = 0;
    g_fired = false;
    if (g_hold) g_haptics.play(ui::kTick, now);
    g_dirty = true;
  } else if (t.isPressed() && g_hold) {
    g_last_input = now;
    const uint32_t held = now - g_hold_ms, need = holdMs(*g_hold);
    if (hit(t.x, t.y) != g_hold) {
      g_hold = nullptr;  // slid off: nothing
    } else if (!g_fired && held >= need) {
      g_fired = true;
      run(*g_hold, now);
    } else if (!g_fired && need >= 400 && g_detents < 2 && held >= need * (g_detents + 1) / 3) {
      ++g_detents;  // a click at each third: the hold is getting there
      g_haptics.play(ui::kDetent, now);
    }
    g_dirty = true;
  } else if (!t.isPressed() && g_hold) {
    if (!g_fired && now - g_hold_ms > 60) say("ながおし で おくる", false, now);
    g_hold = nullptr;
    g_dirty = true;
  }
}

// ---- drawing -------------------------------------------------------------------
// The icon's offset (in sprite pixels) and turn now; faster while it is held.
void animOf(const Item& it, uint32_t now, bool held, int& dx, int& dy, int& rot, float& bright) {
  const uint32_t t = held ? now * 2 : now;
  dx = dy = rot = 0;
  bright = 1.0f;
  switch (it.anim) {
    case Anim::Slide: {
      static const int8_t k[] = {0, 1, 2, 1};
      dx = k[(t / 140) % 4];
      break;
    }
    case Anim::Bob: dy = ((t / 260) & 1) ? -1 : 0; break;
    case Anim::Spin: rot = held ? static_cast<int>((t / 120) % 4) : 0; break;
    case Anim::Swap: rot = ((t / 700) & 1) ? 2 : 0; break;
    case Anim::Pulse: bright = ((t / 400) & 1) ? 0.65f : 1.0f; break;
  }
}

void drawSegment(LovyanGFX& d, int i, const Item& it, uint32_t now) {
  const uint32_t age = now - g_page_ms, appear = i * kIntroStepMs;
  if (age < appear) return;  // the ring builds up after a page change
  float grow = static_cast<float>(age - appear) / kIntroGrowMs;
  if (grow > 1) grow = 1;
  const int outer = kInner + static_cast<int>((kOuter - kInner) * (0.35f + 0.65f * grow));
  const float a0 = kFirstDeg + i * kSegDeg + kGapDeg, a1 = kFirstDeg + (i + 1) * kSegDeg - kGapDeg;
  const bool on = it.act == Act::ZMode && g_app.zMode();
  const bool flash = g_flash == &it && static_cast<int32_t>(g_flash_until - now) > 0;
  const bool held = g_hold == &it && !g_fired;
  d.fillArc(CX, CY, kInner, outer, a0, a1, flash ? TFT_WHITE : shade(it.color, on ? 0.5f : (held ? 0.26f : 0.15f)));
  d.drawArc(CX, CY, outer, outer - 2, a0, a1, shade(it.color, held ? 1.0f : 0.55f));  // a thin rim
  if (held) {  // the hold fills outward
    float p = static_cast<float>(now - g_hold_ms) / holdMs(it);
    if (p > 1) p = 1;
    d.fillArc(CX, CY, kInner, kInner + static_cast<int>((kOuter - kInner) * p), a0, a1, shade(it.color, 0.85f));
  }
  if (grow < 1) return;
  const float mid = (a0 + a1) * 0.5f / kDeg;
  const int rm = (kInner + kOuter) / 2;
  const int x = CX + static_cast<int>(rm * cosf(mid)), y = CY + static_cast<int>(rm * sinf(mid));
  int dx, dy, rot;
  float bright;
  animOf(it, now, held, dx, dy, rot, bright);
  const int scale = held ? 5 : 4;
  const uint16_t fg = flash ? TFT_BLACK : (on || held ? TFT_WHITE : shade(it.color, bright));
  ui::drawSprite(d, it.sprite, x + dx * scale, y - 22 + dy * scale, scale, fg, shade(it.color, 0.45f), rot);
  d.setTextDatum(middle_center);
  d.setFont(&fonts::lgfxJapanGothicP_24);
  d.setTextColor(flash ? TFT_BLACK : TFT_WHITE);
  d.drawString(it.label, x, y + 26);
  if (it.sub[0]) {
    d.setFont(&fonts::lgfxJapanGothicP_16);
    d.setTextColor(flash ? TFT_BLACK : shade(kSlate, 0.8f));
    d.drawString(it.sub, x, y + 48);
  }
}

void drawStatus(LovyanGFX& d, uint32_t now) {
  // the link as a thin outer ring: green = the game is there, amber = the hub only, red = no hub.
  // A spark runs round it while the game is there.
  const bool hub = g_runtime.core().hubLinkUp(), session = g_runtime.core().sessionValid();
  const bool bad = !g_runtime.radioOk() || g_runtime.macMismatch();
  const Rgb c = bad ? kRed : (hub && session ? kGreen : (hub ? kAmber : kRed));
  d.drawArc(CX, CY, 231, 228, 0, 360, shade(c, hub && session ? 0.45f : (((now / 500) & 1) ? 1.0f : 0.4f)));
  if (hub && session) {
    const float a = static_cast<float>(now % 3000) * 360.0f / 3000.0f;
    d.fillArc(CX, CY, 232, 226, a, a + 14, shade(c, 1.0f));
  }
}

void centreText(LovyanGFX& d, const char* top, const char* bottom, uint16_t col_bottom) {
  d.setTextDatum(middle_center);
  d.setFont(&fonts::lgfxJapanGothicP_16);
  d.setTextColor(shade(kSlate, 0.9f));
  d.drawString(top, CX, CY - 86);
  d.setTextColor(col_bottom);
  d.drawString(bottom, CX, CY + 88);
}

void drawStaffCentre(LovyanGFX& d, uint32_t now) {
  const Item& it = kStaffCentre;
  const bool flash = g_flash == &it && static_cast<int32_t>(g_flash_until - now) > 0;
  const bool held = g_hold == &it && !g_fired;
  d.fillCircle(CX, CY, kCentre, flash ? TFT_WHITE : shade(kRed, held ? 0.3f : 0.16f));
  d.drawCircle(CX, CY, kCentre, shade(kRed, 0.85f));
  d.drawCircle(CX, CY, kCentre - 1, shade(kRed, 0.5f));
  if (held) {
    float p = static_cast<float>(now - g_hold_ms) / holdMs(it);
    d.fillCircle(CX, CY, static_cast<int>(kCentre * (p > 1 ? 1 : p)), shade(kRed, 0.85f));
  }
  int dx, dy, rot;
  float bright;
  animOf(it, now, held, dx, dy, rot, bright);
  ui::drawSprite(d, ui::kStop, CX, CY - 14, held ? 4 : 3, flash ? TFT_BLACK : shade(kRed, bright),
                 shade(kRed, 0.4f));
  d.setTextDatum(middle_center);
  d.setFont(&fonts::lgfxJapanGothicP_20);
  d.setTextColor(flash ? TFT_BLACK : TFT_WHITE);
  d.drawString("ストップ", CX, CY + 26);
}

// The play page's centre: the pixel UFO is the stick's knob.
void drawStick(LovyanGFX& d, uint32_t now) {
  const RemoteFrame& f = g_app.frame();
  const bool moving = f.move;
  const Rgb c = g_app.zMode() ? kAmber : kCyan;
  // the stick's well: a dotted ring of pixels and a cross
  for (int k = 0; k < 24; ++k) {
    const float a = k * 15.0f / kDeg;
    d.fillRect(CX + static_cast<int>(kStick * cosf(a)) - 2, CY + static_cast<int>(kStick * sinf(a)) - 2, 4, 4,
               moving ? shade(c, (k + now / 60) % 4 == 0 ? 1.0f : 0.5f) : shade(kSlate, 0.3f));
  }
  if (g_app.zMode()) {
    for (int y = -kStick + 6; y <= kStick - 6; y += 8) d.fillRect(CX - 1, CY + y, 3, 3, shade(kSlate, 0.3f));
  } else {
    for (int k = -kStick + 6; k <= kStick - 6; k += 8) {
      d.fillRect(CX + k, CY - 1, 3, 3, shade(kSlate, 0.3f));
      d.fillRect(CX - 1, CY + k, 3, 3, shade(kSlate, 0.3f));
    }
  }
  const int dx = g_app.zMode() ? 0 : f.x * kStick / 100, dy = -f.y * kStick / 100;
  const int bob = moving ? 0 : (((now / 400) & 1) ? -2 : 0);  // floats while idle
  ui::drawUfo(d, CX + dx, CY + dy + bob - 8, 4, now, f.pose != RemotePose::Open);
}

void draw(uint32_t now) {
  LovyanGFX& d = g_canvas_ok ? static_cast<LovyanGFX&>(g_canvas) : static_cast<LovyanGFX&>(M5.Display);
  if (!g_canvas_ok) M5.Display.startWrite();
  d.fillScreen(TFT_BLACK);
  drawStatus(d, now);
  const Item* items = ring();
  for (int i = 0; i < kSegments; ++i) drawSegment(d, i, items[i], now);

  char top[40];
  const int bat = M5.Power.getBatteryLevel();
  snprintf(top, sizeof(top), "%s  S%u  %d%%", g_page == Page::Staff ? "スタッフ" : "そうさ", systemId(), bat);
  const bool msg = g_msg[0] && static_cast<int32_t>(g_msg_until - now) > 0;
  const char* hint;
  if (g_page == Page::Staff) {
    drawStaffCentre(d, now);
    hint = !linkOk() ? (g_runtime.core().hubLinkUp() ? "ゲーム まち" : "ハブ まち") : "ながおし で おくる";
  } else {
    drawStick(d, now);
    hint = !linkOk()             ? (g_runtime.core().hubLinkUp() ? "ゲーム まち" : "ハブ まち")
           : g_app.moveLatched() ? "黄を いちど はなす"
           : g_app.frame().move  ? (g_app.zMode() ? "奥=アーム↑ 手前=↓" : "かたむけて いどう")
                                 : "黄を おしながら かたむける";
  }
  const uint16_t col = msg ? (g_msg_ok ? shade(kGreen, 1.0f) : shade(kRed, 1.0f)) : shade(kSlate, 0.7f);
  centreText(d, top, msg ? g_msg : hint, col);
  g_burst.draw(d, now);
  if (g_canvas_ok)
    g_canvas.pushSprite(0, 0);
  else
    M5.Display.endWrite();
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.internal_spk = false;
  cfg.internal_mic = false;
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setBrightness(REMOTE_BRIGHTNESS);
  CX = M5.Display.width() / 2;
  CY = M5.Display.height() / 2;
  g_canvas.setPsram(true);
  g_canvas.setColorDepth(16);
  g_canvas_ok = g_canvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  g_app.setEmitter(&g_runtime.core());
  g_runtime.begin();
  if (M5.getBoard() != m5::board_t::board_M5StopWatch) Serial.println("WARNING: this is not an M5StopWatch");
  Serial.printf("staff remote fw %s, canvas %s; tilt dead %d full %d deg, swap %d inv %d/%d; hold %d ms\n",
                UFO_FW_VERSION_STR, g_canvas_ok ? "PSRAM" : "direct", REMOTE_TILT_DEAD_DEG, REMOTE_TILT_FULL_DEG,
                REMOTE_TILT_SWAP_XY, REMOTE_TILT_INVERT_X, REMOTE_TILT_INVERT_Y, REMOTE_HOLD_MS);
  g_last_input = millis();
  g_page_ms = millis();
}

// Debug over the USB serial: "S" -> "SHOT w h\n" + the screen as raw RGB565 (big endian)
// (tests/tools/remote_screenshot.py), "P" -> the other page.
void serviceSerial(uint32_t now) {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'P') showPage(g_page == Page::Staff ? Page::Play : Page::Staff, now);
    if (c != 'S' || !g_canvas_ok) continue;
    draw(millis());
    Serial.printf("SHOT %d %d\n", g_canvas.width(), g_canvas.height());
    Serial.write(static_cast<const uint8_t*>(g_canvas.getBuffer()), g_canvas.bufferLength());
    Serial.flush();
  }
}

void loop() {
  M5.update();
  uint32_t now = millis();
  serviceSerial(now);
  g_runtime.loop();
  pollInput(now);
  g_haptics.service(now);

  static float ax = 0, ay = 0, az = 1;  // the last IMU sample (not every loop has a new one)
  if (M5.Imu.update()) {
    auto data = M5.Imu.getImuData();
    ax = data.accel.x;
    ay = data.accel.y;
    az = data.accel.z;
  }
  const bool move_key = g_page == Page::Play && M5.BtnA.isPressed() && !g_dim;
  g_app.tick(move_key, ax, ay, az, now);

  if (!g_dim && now - g_last_input > REMOTE_DIM_S * 1000UL && !g_app.streaming()) {
    g_dim = true;
    M5.Display.setBrightness(8);
  }
  if (g_flash && static_cast<int32_t>(now - g_flash_until) >= 0) g_flash = nullptr;
  if (g_msg_until && static_cast<int32_t>(now - g_msg_until) >= 0) g_msg_until = 0;
  // the icons always move: ~20 frames a second; dimmed: once a second
  const uint32_t period = g_dim ? 1000 : 50;
  if ((g_dirty && now - g_last_draw >= 30) || now - g_last_draw >= period) {
    g_dirty = false;
    g_last_draw = now;
    draw(now);
  }
  delay(1);
}
