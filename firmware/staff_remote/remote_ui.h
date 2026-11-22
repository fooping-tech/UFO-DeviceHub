// Staff remote: the look and the feel (pixel-art sprites, particles, haptic patterns).
// Drawing only: nothing here sends anything. Used by main.cpp (M5GFX / LovyanGFX).
#pragma once

#include <M5Unified.h>
#include <stdint.h>
#include <string.h>

namespace remote_ui {

// ---- pixel sprites -------------------------------------------------------------------
// Rows of characters, one per pixel: '.' = transparent, '#' = the item's colour,
// '+' = white, 'd' = the item's colour dark. The UFO has its own palette (see drawUfo).
constexpr int kIcon = 12;  // icons are 12 x 12

const char* const kNext[kIcon] = {
    "............", "..##........", "..###.......", "..####......", "..#####.....", "..######....",
    "..######....", "..#####.....", "..####......", "..###.......", "..##........", "............"};
const char* const kSkip[kIcon] = {
    "............", ".#....#...##", ".##...##..##", ".###..###.##", ".####.######", ".###########",
    ".###########", ".####.######", ".###..###.##", ".##...##..##", ".#....#...##", "............"};
const char* const kHome[kIcon] = {
    ".....##.....", "....####....", "...######...", "..########..", ".##########.", "..########..",
    "..##++####..", "..##++####..", "..##++####..", "..##++####..", "..########..", "............"};
const char* const kSwitch[kIcon] = {
    "............", "........#...", "........##..", "..#########.", "........##..", "........#...",
    "...#........", "..##........", ".#########..", "..##........", "...#........", "............"};
const char* const kRecover[kIcon] = {
    "....####....", "..##....##..", ".#.......##.", ".#......####", "#.......####", "#...........",
    "#...........", "#..........#", ".#........#.", ".#........#.", "..##....##..", "....####...."};
const char* const kFist[kIcon] = {
    "............", "............", "...######...", "..#d#d#d##..", "..########..", "..########..",
    ".+########..", ".++######...", "..+######...", "....####....", "....####....", "............"};
const char* const kPeace[kIcon] = {
    "..##...##...", "..##...##...", "..##..##....", "...##.##....", "...######...", "..########..",
    ".+########..", ".++#######..", "..+######...", "....####....", "....####....", "............"};
const char* const kPoint[kIcon] = {
    ".....##.....", ".....##.....", ".....##.....", ".....##.....", "...######...", "..########..",
    ".+########..", ".++#######..", "..+######...", "....####....", "....####....", "............"};
const char* const kUpDown[kIcon] = {
    ".....##.....", "....####....", "...######...", "..########..", ".....##.....", "............",
    "............", ".....##.....", "..########..", "...######...", "....####....", ".....##....."};
const char* const kStop[kIcon] = {
    "...######...", "..########..", ".##########.", "############", "############", "#++++++++++#",
    "#++++++++++#", "############", "############", ".##########.", "..########..", "...######..."};

// The UFO (the game's blue UFO), 16 x 9. c = dome, w = glint, b = body, L / l = lights
// (they swap every frame), k = underside.
constexpr int kUfoW = 16, kUfoH = 9;
const char* const kUfo[kUfoH] = {
    "......cccc......", ".....cwwccc.....", "....cccccccc....", "..bbbbbbbbbbbb..", ".bLbblbbLbblbbb.",
    "bbbbbbbbbbbbbbbb", ".kkkkkkkkkkkkkk.", "...kk......kk...", "................"};

// rot: 0..3 quarter turns clockwise
inline char px(const char* const* s, int n, int x, int y, int rot) {
  switch (rot & 3) {
    case 1: return s[n - 1 - x][y];
    case 2: return s[n - 1 - y][n - 1 - x];
    case 3: return s[x][n - 1 - y];
  }
  return s[y][x];
}

inline void drawSprite(LovyanGFX& d, const char* const* s, int cx, int cy, int scale, uint16_t col, uint16_t dark,
                       int rot = 0) {
  const int x0 = cx - kIcon * scale / 2, y0 = cy - kIcon * scale / 2;
  for (int y = 0; y < kIcon; ++y)
    for (int x = 0; x < kIcon; ++x) {
      char c = px(s, kIcon, x, y, rot);
      if (c == '.') continue;
      d.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, c == '+' ? TFT_WHITE : (c == 'd' ? dark : col));
    }
}

inline void drawUfo(LovyanGFX& d, int cx, int cy, int scale, uint32_t now, bool beam) {
  const bool blink = (now / 180) & 1;
  const uint16_t dome = d.color565(140, 225, 255), glint = TFT_WHITE, body = d.color565(50, 120, 235),
                 lit = d.color565(255, 220, 80), off = d.color565(30, 70, 150), under = d.color565(25, 55, 130);
  const int x0 = cx - kUfoW * scale / 2, y0 = cy - kUfoH * scale / 2;
  if (beam) {  // a flickering pixel beam under it
    const uint16_t b1 = d.color565(255, 230, 120), b2 = d.color565(120, 90, 20);
    for (int row = 0; row < 7; ++row) {
      const int w = 4 + row * 2;
      for (int i = 0; i < w; ++i) {
        const bool on = ((i + row + now / 60) % 3) != 0;
        d.fillRect(cx - w * scale / 2 + i * scale, y0 + (kUfoH - 1 + row) * scale, scale, scale, on ? b1 : b2);
      }
    }
  }
  for (int y = 0; y < kUfoH; ++y)
    for (int x = 0; x < kUfoW; ++x) {
      uint16_t c;
      switch (kUfo[y][x]) {
        case 'c': c = dome; break;
        case 'w': c = glint; break;
        case 'b': c = body; break;
        case 'k': c = under; break;
        case 'L': c = blink ? lit : off; break;
        case 'l': c = blink ? off : lit; break;
        default: continue;
      }
      d.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, c);
    }
}

// ---- particles: a pixel burst when something is sent ----------------------------------
struct Particle {
  float x, y, vx, vy;
  uint32_t born;
  uint16_t color;
  bool live;
};
constexpr int kParticles = 40;
constexpr uint32_t kParticleMs = 520;

class Burst {
 public:
  void fire(int x, int y, float dir_x, float dir_y, uint16_t color, uint32_t now) {
    for (int n = 0; n < 16; ++n) {
      Particle& p = p_[next_++ % kParticles];
      const float a = (esp_random() % 628) / 100.0f;
      const float s = 1.5f + (esp_random() % 100) / 25.0f;
      p.x = x;
      p.y = y;
      p.vx = cosf(a) * s + dir_x * 2.5f;
      p.vy = sinf(a) * s + dir_y * 2.5f;
      p.born = now;
      p.color = (n % 3 == 0) ? TFT_WHITE : color;
      p.live = true;
    }
  }
  bool active(uint32_t now) const {
    for (const auto& p : p_)
      if (p.live && now - p.born < kParticleMs) return true;
    return false;
  }
  void draw(LovyanGFX& d, uint32_t now) {
    for (auto& p : p_) {
      if (!p.live) continue;
      const uint32_t age = now - p.born;
      if (age >= kParticleMs) {
        p.live = false;
        continue;
      }
      const float t = age / 16.0f;  // ~frames
      const int size = age < kParticleMs / 2 ? 6 : (age < kParticleMs * 3 / 4 ? 4 : 2);
      const int x = static_cast<int>(p.x + p.vx * t), y = static_cast<int>(p.y + p.vy * t + 0.04f * t * t);
      d.fillRect(x - size / 2, y - size / 2, size, size, p.color);
    }
  }

 private:
  Particle p_[kParticles] = {};
  int next_ = 0;
};

// ---- haptics: short, strong pulses (a click), never a long buzz ----------------------
struct Step {
  uint8_t level;  // 0 = off
  uint16_t ms;
};
const Step kTick[] = {{230, 14}};                                   // a finger lands
const Step kDetent[] = {{170, 10}};                                 // the hold passes a third
const Step kClick[] = {{255, 22}, {0, 55}, {255, 18}};              // sent: a firm double click
const Step kSoft[] = {{200, 12}};                                   // page / toggle
const Step kError[] = {{200, 70}, {0, 70}, {200, 70}};              // not sent

class Haptics {
 public:
  explicit Haptics(uint8_t max_level) : max_(max_level) {}
  template <size_t N>
  void play(const Step (&s)[N], uint32_t now) {
    if (max_ == 0) return;
    n_ = N > kMax ? kMax : N;
    memcpy(steps_, s, n_ * sizeof(Step));
    i_ = 0;
    start(now);
  }
  void service(uint32_t now) {
    if (i_ >= n_) return;
    if (static_cast<int32_t>(now - until_) < 0) return;
    ++i_;
    if (i_ >= n_) {
      M5.Power.setVibration(0);
      return;
    }
    start(now);
  }

 private:
  void start(uint32_t now) {
    const Step& s = steps_[i_];
    M5.Power.setVibration(s.level ? static_cast<uint8_t>(s.level * max_ / 255) : 0);
    until_ = now + s.ms;
  }
  static constexpr size_t kMax = 6;
  Step steps_[kMax] = {};
  size_t n_ = 0, i_ = 0;
  uint32_t until_ = 0;
  uint8_t max_;
};

}  // namespace remote_ui
