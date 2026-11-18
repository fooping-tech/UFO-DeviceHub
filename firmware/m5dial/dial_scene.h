// M5Dial main screen as pixel art (hardware independent, host-testable).
//
// The round display is the portal between the real machine and the screen
// game. DialScene renders an 80 x 80 RGB565 picture; DialDisplay shows it three
// times as large (240 x 240), so one scene pixel is a 3 x 3 dot.
//
//   STANDBY   stars, a faint ring
//   READY     the ring breathes in the unit colour
//   WAITING   the portal turns and pulls light towards the centre
//   GOT BALL  flash, the capsule spirals into the portal, burst, glowing core
//   RELEASE   burst, the capsule comes out of the portal and drops out of the
//   RETURN    bottom of the screen, then the portal keeps blowing outwards
//   DONE      sparkles
//   ERROR / no hub link: red
//
// tests/tools/scene_preview renders the same code to image files on a PC.
#pragma once

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
  uint8_t r = 0, g = 255, b = 255;  // unit colour
  const char* mac = "";             // shown while no PC session exists (set-up)
};

const char* dialStateLabel(DialState s);

// Animation times (ms from the state change)
constexpr uint32_t kSceneFlashMs = 150;     // GOT BALL: white flash (as the LED)
constexpr uint32_t kSceneIntakeMs = 1200;   // GOT BALL: capsule spirals into the portal
constexpr uint32_t kSceneBurstMs = 400;     // ring burst after the intake / at the release start
constexpr uint32_t kSceneEmergeMs = 1300;   // RELEASE: capsule has grown out of the portal
constexpr uint32_t kSceneDropMs = 2000;     // RELEASE: capsule has left the screen

class DialScene {
 public:
  static constexpr int kSize = 80;
  DialScene();
  // Renders the frame for `now`. State changes are noticed here (animation start).
  void render(const DisplayModel& m, uint32_t now);
  const uint16_t* pixels() const { return fb_; }  // kSize * kSize, RGB565, row by row
  uint16_t pixel(int x, int y) const { return fb_[y * kSize + x]; }
  // True while a one-shot animation (intake / release) covers the text.
  bool animating() const { return animating_; }

 private:
  struct Rgb {
    uint8_t r, g, b;
  };
  void add(int x, int y, Rgb c, uint8_t level);
  void put(int x, int y, Rgb c);
  void background(Rgb c, int dir, uint8_t swirl, uint8_t ring, uint8_t core, bool stars, uint32_t t);
  void particles(Rgb c, int dir, uint8_t level, uint32_t t);
  void burst(Rgb c, uint32_t t, uint32_t dur);
  void capsule(float cx, float cy, float radius, Rgb c);
  void text(const char* s, int cy, Rgb c, int scale = 1);
  void drawGlyph(char ch, int x, int y, Rgb c, int scale);
  void labels(const DisplayModel& m, Rgb unit, bool title_only);

  uint16_t fb_[kSize * kSize];
  uint8_t rad_[kSize * kSize];  // distance from the centre, 1/4 pixel (255 = outside the round screen)
  uint8_t ang_[kSize * kSize];  // angle, 256 = one turn
  DialState state_ = DialState::Disabled;
  bool first_ = true;
  uint32_t state_at_ = 0;    // when the shown state began
  uint32_t release_at_ = 0;  // RELEASING began (RETURNING continues its animation)
  bool animating_ = false;
};

}  // namespace ufo
