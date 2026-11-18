#include "dial_scene.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace ufo {

const char* dialStateLabel(DialState s) {
  switch (s) {
    case DialState::Disabled: return "STANDBY";
    case DialState::Ready: return "READY";
    case DialState::Armed: return "WAITING";       // 受取待ち
    case DialState::BallDetected: return "GOT BALL";  // 受取済み
    case DialState::Releasing: return "RELEASE";   // 排出中
    case DialState::Returning: return "RETURN";
    case DialState::Complete: return "DONE";
    case DialState::Error: return "ERROR";
    case DialState::Settings: return "SETTINGS";
  }
  return "?";
}

namespace {
constexpr int N = DialScene::kSize;
constexpr float kCentre = (N - 1) / 2.0f;
constexpr float kPi = 3.14159265f;

// 3 x 5 pixel font: five rows of three bits, top row first (M, N, W: wideGlyph).
constexpr uint16_t G(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e) {
  return static_cast<uint16_t>((a << 12) | (b << 9) | (c << 6) | (d << 3) | e);
}
uint16_t glyph(char ch) {
  switch (ch) {
    case 'A': return G(2, 5, 7, 5, 5);
    case 'B': return G(6, 5, 6, 5, 6);
    case 'C': return G(3, 4, 4, 4, 3);
    case 'D': return G(6, 5, 5, 5, 6);
    case 'E': return G(7, 4, 6, 4, 7);
    case 'F': return G(7, 4, 6, 4, 4);
    case 'G': return G(3, 4, 5, 5, 3);
    case 'H': return G(5, 5, 7, 5, 5);
    case 'I': return G(7, 2, 2, 2, 7);
    case 'J': return G(1, 1, 1, 5, 2);
    case 'K': return G(5, 5, 6, 5, 5);
    case 'L': return G(4, 4, 4, 4, 7);
    case 'M': return G(5, 7, 7, 5, 5);
    case 'N': return G(6, 5, 5, 5, 5);
    case 'O': return G(2, 5, 5, 5, 2);
    case 'P': return G(6, 5, 6, 4, 4);
    case 'Q': return G(2, 5, 5, 7, 3);
    case 'R': return G(6, 5, 6, 5, 5);
    case 'S': return G(3, 4, 2, 1, 6);
    case 'T': return G(7, 2, 2, 2, 2);
    case 'U': return G(5, 5, 5, 5, 7);
    case 'V': return G(5, 5, 5, 5, 2);
    case 'W': return G(5, 5, 5, 7, 5);
    case 'X': return G(5, 5, 2, 5, 5);
    case 'Y': return G(5, 5, 2, 2, 2);
    case 'Z': return G(7, 1, 2, 4, 7);
    case '0': return G(7, 5, 5, 5, 7);
    case '1': return G(2, 6, 2, 2, 7);
    case '2': return G(6, 1, 2, 4, 7);
    case '3': return G(6, 1, 2, 1, 6);
    case '4': return G(5, 5, 7, 1, 1);
    case '5': return G(7, 4, 6, 1, 6);
    case '6': return G(3, 4, 7, 5, 7);
    case '7': return G(7, 1, 2, 2, 2);
    case '8': return G(7, 5, 7, 5, 7);
    case '9': return G(7, 5, 7, 1, 6);
    case ':': return G(0, 2, 0, 2, 0);
    case '#': return G(5, 7, 5, 7, 5);
    case '-': return G(0, 0, 7, 0, 0);
    case '.': return G(0, 0, 0, 0, 2);
    case '!': return G(2, 2, 2, 0, 2);
    case '/': return G(1, 1, 2, 4, 4);
    default: return 0;
  }
}

// M, N and W do not fit in three columns: five columns, five rows of five bits.
const uint8_t* wideGlyph(char ch) {
  static const uint8_t m[5] = {0x11, 0x1B, 0x15, 0x11, 0x11};
  static const uint8_t n[5] = {0x11, 0x19, 0x15, 0x13, 0x11};
  static const uint8_t w[5] = {0x11, 0x11, 0x15, 0x15, 0x0A};
  switch (ch) {
    case 'M': return m;
    case 'N': return n;
    case 'W': return w;
    default: return nullptr;
  }
}
int glyphWidth(char ch) { return wideGlyph(ch) ? 5 : 3; }

uint8_t tri(uint8_t p) { return p < 128 ? static_cast<uint8_t>(p * 2) : static_cast<uint8_t>((255 - p) * 2); }
uint32_t hash32(uint32_t h) {
  h ^= h >> 15;
  h *= 2246822519u;
  h ^= h >> 13;
  h *= 3266489917u;
  h ^= h >> 16;
  return h;
}
uint16_t pack(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
uint8_t sat(int v) { return v > 255 ? 255 : (v < 0 ? 0 : static_cast<uint8_t>(v)); }
}  // namespace

DialScene::DialScene() {
  memset(fb_, 0, sizeof(fb_));
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      float dx = x - kCentre, dy = y - kCentre;
      float r = sqrtf(dx * dx + dy * dy) * 4.0f;
      rad_[y * N + x] = r > 162.0f ? 255 : static_cast<uint8_t>(r);
      ang_[y * N + x] = static_cast<uint8_t>(static_cast<int>(atan2f(dy, dx) * (128.0f / kPi)) & 0xFF);
    }
}

void DialScene::put(int x, int y, Rgb c) {
  if (x < 0 || y < 0 || x >= N || y >= N) return;
  fb_[y * N + x] = pack(c.r, c.g, c.b);
}

void DialScene::add(int x, int y, Rgb c, uint8_t level) {
  if (x < 0 || y < 0 || x >= N || y >= N || level == 0) return;
  uint16_t p = fb_[y * N + x];
  int r = ((p >> 11) << 3) + (c.r * level >> 8);
  int g = (((p >> 5) & 0x3F) << 2) + (c.g * level >> 8);
  int b = ((p & 0x1F) << 3) + (c.b * level >> 8);
  fb_[y * N + x] = pack(sat(r), sat(g), sat(b));
}

// Portal: three spiral arms (strong at the rim, dark in the middle where the
// text is), a rim glow and a white core. dir +1 pulls inwards, -1 blows outwards.
void DialScene::background(Rgb c, int dir, uint8_t swirl, uint8_t ring, uint8_t core, bool stars, uint32_t t) {
  const uint8_t turn = static_cast<uint8_t>(dir * static_cast<int32_t>(t / 5));
  for (int i = 0; i < N * N; ++i) {
    const uint8_t r = rad_[i];
    if (r == 255) {
      fb_[i] = 0;
      continue;
    }
    int lr = 0, lg = 0, lb = 0;
    if (swirl) {
      uint8_t w = tri(static_cast<uint8_t>(ang_[i] * 3 + r * 2 + turn));
      uint32_t arm = static_cast<uint32_t>(w) * w >> 8;
      arm = arm * arm >> 8;
      uint32_t env = r < 48 ? 0 : (r - 48) * 255u / 112u;
      uint32_t v = arm * env * swirl >> 16;
      lr += c.r * v >> 8;
      lg += c.g * v >> 8;
      lb += c.b * v >> 8;
    }
    if (ring && r > 132) {
      uint32_t v = static_cast<uint32_t>(r - 132) * 9 * ring >> 8;
      if (v > 255) v = 255;
      lr += c.r * v >> 8;
      lg += c.g * v >> 8;
      lb += c.b * v >> 8;
    }
    if (core && r < 72) {
      uint32_t k = 72 - r;  // 0..72
      uint32_t v = k * k * core / (72 * 72);
      lr += (c.r + 2 * 255) / 3 * v >> 8;  // whiter towards the middle
      lg += (c.g + 2 * 255) / 3 * v >> 8;
      lb += (c.b + 2 * 255) / 3 * v >> 8;
    }
    if (stars) {
      uint32_t h = hash32(static_cast<uint32_t>(i) * 2654435761u);
      if ((h & 0x3F) == 0) {
        uint8_t tw = tri(static_cast<uint8_t>(t / 12 + (h >> 8)));
        int v = 40 + (tw * 150 >> 8);
        lr += v;
        lg += v;
        lb += v;
      }
    }
    fb_[i] = pack(sat(lr), sat(lg), sat(lb));
  }
}

// Sparks on spiral paths, pulled in (dir +1) or thrown out (dir -1).
void DialScene::particles(Rgb c, int dir, uint8_t level, uint32_t t) {
  for (uint32_t k = 0; k < 14; ++k) {
    uint32_t h = hash32(k * 7919u + 17u);
    uint32_t period = 900 + (h & 0x3FF);  // ms from the rim to the centre
    float u = static_cast<float>((t + (h >> 10)) % period) / period;  // 0..1
    if (dir < 0) u = 1.0f - u;
    float rho = 39.0f * (1.0f - u);
    float th = (h >> 20) * (2 * kPi / 4096.0f) + u * 3.0f * (dir < 0 ? -1.0f : 1.0f);
    int x = static_cast<int>(kCentre + rho * cosf(th) + 0.5f);
    int y = static_cast<int>(kCentre + rho * sinf(th) + 0.5f);
    uint8_t lv = static_cast<uint8_t>(level * (0.35f + 0.65f * (1.0f - u)));
    add(x, y, Rgb{255, 255, 255}, lv);
    add(x + 1, y, c, lv / 2);
    add(x - 1, y, c, lv / 2);
    add(x, y + 1, c, lv / 2);
    add(x, y - 1, c, lv / 2);
  }
}

// One ring running from the centre to the rim in `dur` ms.
void DialScene::burst(Rgb c, uint32_t t, uint32_t dur) {
  if (t >= dur) return;
  const int rb = static_cast<int>(t * 168 / dur);  // 1/4 pixel
  const uint8_t fade = static_cast<uint8_t>(255 - t * 255 / dur);
  for (int i = 0; i < N * N; ++i) {
    if (rad_[i] == 255) continue;
    int d = rad_[i] - rb;
    if (d < 0) d = -d;
    if (d >= 10) continue;
    uint8_t lv = static_cast<uint8_t>((10 - d) * 25 * fade >> 8);
    add(i % N, i / N, Rgb{255, 255, 255}, lv);
    add(i % N, i / N, c, lv);
  }
}

// Gacha capsule: upper half in the unit colour, lower half white, dark seam
// and rim, a highlight. Any radius (it shrinks into / grows out of the portal).
void DialScene::capsule(float cx, float cy, float radius, Rgb c) {
  if (radius < 0.6f) return;
  const int x0 = static_cast<int>(floorf(cx - radius)), x1 = static_cast<int>(ceilf(cx + radius));
  const int y0 = static_cast<int>(floorf(cy - radius)), y1 = static_cast<int>(ceilf(cy + radius));
  const float r2 = radius * radius;
  const float hx = cx - radius * 0.38f, hy = cy - radius * 0.45f, hr = radius * 0.24f;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const float dx = x - cx, dy = y - cy;
      const float d2 = dx * dx + dy * dy;
      if (d2 > r2) continue;
      if (x < 0 || y < 0 || x >= N || y >= N || rad_[y * N + x] == 255) continue;
      Rgb p = dy < 0 ? c : Rgb{235, 235, 245};
      if (radius >= 3.0f) {
        if (fabsf(dy) < 0.6f) p = Rgb{30, 30, 50};  // seam
        const float inner = (radius - 1.0f) * (radius - 1.0f);
        if (d2 > inner) p = Rgb{static_cast<uint8_t>(p.r * 5 / 10), static_cast<uint8_t>(p.g * 5 / 10),
                                static_cast<uint8_t>(p.b * 5 / 10)};  // rim
      }
      if (radius >= 4.0f && (x - hx) * (x - hx) + (y - hy) * (y - hy) <= hr * hr) p = Rgb{255, 255, 255};
      put(x, y, p);
    }
}

void DialScene::drawGlyph(char ch, int x, int y, Rgb c, int scale) {
  const uint8_t* wide = wideGlyph(ch);
  const uint16_t g = glyph(ch);
  const int w = wide ? 5 : 3;
  for (int row = 0; row < 5; ++row)
    for (int col = 0; col < w; ++col)
      if (wide ? (wide[row] >> (4 - col)) & 1 : (g >> ((4 - row) * 3 + (2 - col))) & 1)
        for (int sy = 0; sy < scale; ++sy)
          for (int sx = 0; sx < scale; ++sx) put(x + col * scale + sx, y + row * scale + sy, c);
}

// Centred text with a black outline (readable on the portal). cy = top row.
void DialScene::text(const char* s, int cy, Rgb c, int scale) {
  const int n = static_cast<int>(strlen(s));
  if (n == 0) return;
  int w = -scale;
  for (int i = 0; i < n; ++i) w += (glyphWidth(s[i]) + 1) * scale;
  const int x0 = (N - w) / 2;
  static const int8_t off[9][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}, {0, 0}};
  for (const auto& o : off) {  // eight times in black around it, then the text itself
    const bool last = o[0] == 0 && o[1] == 0;
    int x = x0;
    for (int i = 0; i < n; ++i) {
      drawGlyph(s[i], x + o[0], cy + o[1], last ? c : Rgb{0, 0, 0}, scale);
      x += (glyphWidth(s[i]) + 1) * scale;
    }
  }
}

void DialScene::labels(const DisplayModel& m, Rgb unit, bool title_only) {
  char buf[24];
  const Rgb white{255, 255, 255}, grey{170, 170, 180}, red{255, 60, 60}, orange{255, 170, 0}, green{80, 255, 120};
  Rgb title = white;
  switch (m.state) {
    case DialState::Armed: title = unit; break;
    case DialState::BallDetected: title = green; break;
    case DialState::Releasing:
    case DialState::Returning: title = Rgb{255, 230, 60}; break;
    case DialState::Error: title = red; break;
    case DialState::Disabled: title = grey; break;
    default: break;
  }
  text(dialStateLabel(m.state), 31, title, 2);
  if (title_only) return;

  snprintf(buf, sizeof(buf), "DIAL %u", m.dial_number);
  text(buf, 9, unit);
  if (!m.radio_ok)
    text("RADIO ERR", 17, red);
  else if (!m.link)
    text("NO HUB", 17, red);
  else if (!m.session)
    text("NO SESSION", 17, orange);
  else
    text("HUB OK", 17, green);

  if (m.op == OperationMode::Game)
    snprintf(buf, sizeof(buf), "GAME %u", m.cycle);
  else
    snprintf(buf, sizeof(buf), "STANDBY");
  if (m.state != DialState::Disabled) text(buf, 47, grey);  // STANDBY is the title already

  // one line for the most important notice, one below it
  int y = 55;
  if (m.mac_mismatch) {
    text("MAC MISMATCH", y, red);
    y += 8;
  }
  if (m.error != ErrorCode::None) {
    snprintf(buf, sizeof(buf), "ERROR %u", static_cast<unsigned>(m.error));
    text(buf, y, red);
    y += 8;
  }
  if (m.example_keys && y <= 63) {
    text("EXAMPLE KEYS", y, orange);
    y += 8;
  }
  if (y == 55 && !m.session && m.mac && m.mac[0]) {
    text(m.mac, y, grey);  // set-up: which unit is this
    y += 8;
  }
  if (y <= 63) text(m.switch_on ? (m.sim_button ? "SW ON SIM" : "SW ON") : (m.sim_button ? "SW OFF SIM" : "SW OFF"), y,
                    m.switch_on ? green : Rgb{110, 110, 120});
}

void DialScene::render(const DisplayModel& m, uint32_t now) {
  if (first_ || m.state != state_) {
    // RETURNING continues the release animation that began with RELEASING
    if (m.state == DialState::Releasing || (m.state == DialState::Returning && state_ != DialState::Releasing))
      release_at_ = now;
    // shown for the first time in a state that is already old: no entry animation
    state_at_ = first_ ? now - 60000 : now;
    if (first_) release_at_ = now - 60000;
    state_ = m.state;
    first_ = false;
  }
  const uint32_t t = now - state_at_;
  const Rgb unit{m.r, m.g, m.b};
  const Rgb red{255, 40, 30};
  animating_ = false;

  if (!m.radio_ok || !m.link) {  // nothing can arrive: red, slow
    uint8_t br = static_cast<uint8_t>(60 + (tri(static_cast<uint8_t>(now / 8)) * 150 >> 8));
    background(red, 0, 0, br, 0, true, now);
    labels(m, unit, false);
    return;
  }
  switch (m.state) {
    case DialState::Error: {
      uint8_t br = (now / 500) % 2 ? 255 : 60;
      background(red, 0, 0, br, 0, false, now);
      labels(m, unit, false);
      break;
    }
    case DialState::Ready: {
      uint8_t br = static_cast<uint8_t>(70 + (tri(static_cast<uint8_t>(now / 12)) * 185 >> 8));
      background(unit, 1, 60, br, 0, true, now / 3);
      labels(m, unit, false);
      break;
    }
    case DialState::Armed:
      background(unit, 1, 230, 200, 40, false, now);
      particles(unit, 1, 230, now);
      labels(m, unit, false);
      break;
    case DialState::BallDetected: {
      if (t < kSceneIntakeMs + kSceneBurstMs) {
        animating_ = true;
        background(unit, 1, 255, 255, t < kSceneIntakeMs ? 90 : 255, false, now * 2);  // fast
        particles(unit, 1, 255, now * 2);
        if (t < kSceneIntakeMs) {
          // from the bottom (the dial side) one and a half turns into the centre
          for (int k = 4; k >= 0; --k) {
            float u = (static_cast<float>(t) - k * 45.0f) / kSceneIntakeMs;
            if (u < 0) continue;
            float rho = 33.0f * powf(1.0f - u, 1.3f);
            float th = kPi / 2 + u * 3.0f * kPi;
            float cx = kCentre + rho * cosf(th), cy = kCentre + rho * sinf(th);
            float rad = 1.2f + 7.3f * (1.0f - u);
            if (k == 0) {
              capsule(cx, cy, rad, unit);
            } else {  // trail
              int tr = static_cast<int>(rad * 0.45f);
              for (int yy = -tr; yy <= tr; ++yy)
                for (int xx = -tr; xx <= tr; ++xx)
                  if (xx * xx + yy * yy <= tr * tr)
                    add(static_cast<int>(cx) + xx, static_cast<int>(cy) + yy, unit, static_cast<uint8_t>(150 / k));
            }
          }
        } else {
          burst(unit, t - kSceneIntakeMs, kSceneBurstMs);
        }
        if (t < kSceneFlashMs) {  // the same white flash as the LED
          uint8_t lv = static_cast<uint8_t>(255 - t * 255 / kSceneFlashMs);
          for (int i = 0; i < N * N; ++i)
            if (rad_[i] != 255) add(i % N, i / N, Rgb{255, 255, 255}, lv);
        }
      } else {  // the capsule is on its way to the screen
        uint8_t pulse = static_cast<uint8_t>(110 + (tri(static_cast<uint8_t>(now / 6)) * 120 >> 8));
        background(unit, 1, 150, 220, pulse, false, now);
        particles(unit, 1, 140, now);
        labels(m, unit, false);
      }
      break;
    }
    case DialState::Releasing:
    case DialState::Returning: {
      const uint32_t tr = now - release_at_;
      background(unit, -1, 255, 255, tr < kSceneEmergeMs ? 255 : 120, false, now * 2);
      particles(unit, -1, 255, now * 2);
      if (tr < kSceneDropMs) {
        animating_ = true;
        burst(unit, tr, kSceneBurstMs);
        if (tr < kSceneEmergeMs) {  // grows out of the portal, wobbling
          float u = static_cast<float>(tr) / kSceneEmergeMs;
          float rad = 0.8f + 8.2f * u * u;
          float wob = 3.0f * (1.0f - u);
          capsule(kCentre + wob * cosf(u * 14.0f), kCentre + wob * sinf(u * 14.0f), rad, unit);
        } else {  // drops out towards the dial (bottom), accelerating
          float u = static_cast<float>(tr - kSceneEmergeMs) / (kSceneDropMs - kSceneEmergeMs);
          float cy = kCentre + 52.0f * u * u;
          for (int k = 1; k <= 4; ++k) {
            int ty = static_cast<int>(cy - 9.0f - k * 4.0f * (0.3f + u));
            if (ty < static_cast<int>(kCentre)) continue;
            for (int xx = -3 + k / 2; xx <= 3 - k / 2; ++xx)
              add(static_cast<int>(kCentre + 0.5f) + xx, ty, unit, static_cast<uint8_t>(200 / k));
          }
          capsule(kCentre, cy, 9.0f, unit);
        }
      } else {
        labels(m, unit, false);
      }
      break;
    }
    case DialState::Complete: {
      uint8_t ring = t < 2000 ? static_cast<uint8_t>(255 - t * 175 / 2000) : 80;
      background(unit, 0, 0, ring, 0, true, now * 3);
      burst(unit, t, 600);
      labels(m, unit, false);
      break;
    }
    case DialState::Disabled:
    default:
      background(unit, 1, 28, 45, 0, true, now / 4);
      labels(m, unit, false);
      break;
  }
}

}  // namespace ufo
