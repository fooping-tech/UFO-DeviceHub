// Where every WS2811 pixel of the shelf is (pure logic, no FastLED: host tested).
//
// The electrical order (the data line from the ATOM on top of the shelf) and the
// physical place are kept apart: kRuns below is the ONLY place that knows the
// wiring. Everything else asks for a place ("front-right pillar, 2nd from the
// top") or reads the per-pixel geometry (height, position around the shelf).
//
// Top view (the front is where the player stands):
//
//          back
//    BL ●────────● BR        around (0..255, clockwise from above):
//       │        │             FL = 0, FR = 64, BR = 128, BL = 192
//       │        │           the front face spans 0..64 (left -> right)
//    FL ●════════● FR
//          front
//
// One "pixel" is one WS2811 IC (one colour for its LEDs). The wiring today:
//   1. front-left pillar   top -> bottom   4
//   2. front face          left -> right   7
//   3. front-right pillar  bottom -> top   4
//   4. back-right pillar   top -> bottom   4
//   5. back-left pillar    bottom -> top   4      = 23 pixels
// A stretch of the data line with pixels that are not part of the show (e.g. if
// a jumper between two pillars turns out to carry ICs) is a Part::Unused run: it
// keeps its place in the chain and stays dark.
#pragma once

#include <stdint.h>

namespace ufo {
namespace shelf {

enum class Part : uint8_t {
  FrontLeft = 0,   // pillar
  Front = 1,       // the front face of the shelf (horizontal)
  FrontRight = 2,  // pillar
  BackRight = 3,   // pillar
  BackLeft = 4,    // pillar
  Unused = 5,      // pixels on the line that are not part of the show
};
constexpr uint8_t kPartCount = 5;  // the real parts (without Unused)

// How a run is wired, seen from the data input of that run.
enum class Wiring : uint8_t { TopDown, BottomUp, LeftToRight, RightToLeft };

// From which end a position along a part is counted, whatever the wiring.
enum class From : uint8_t { Top, Bottom, Left, Right };

struct Run {
  Part part;
  uint8_t count;
  Wiring wiring;
};

// ---- the shelf as it is wired (change only this when the wiring changes) ----
constexpr Run kRuns[] = {
    {Part::FrontLeft, 4, Wiring::TopDown},
    {Part::Front, 7, Wiring::LeftToRight},
    {Part::FrontRight, 4, Wiring::BottomUp},
    {Part::BackRight, 4, Wiring::TopDown},
    {Part::BackLeft, 4, Wiring::BottomUp},
};
constexpr uint8_t kRunCount = sizeof(kRuns) / sizeof(kRuns[0]);

// Height of the front face (0 = foot of the pillars, 255 = top). The wiring goes
// down the front-left pillar, along the front face and up the front-right pillar,
// so the face is taken to be at the foot. UNVERIFIED: set it where it really is.
constexpr uint8_t kFrontHeight = 0;

constexpr uint16_t ledCount() {
  uint16_t n = 0;
  for (uint8_t i = 0; i < kRunCount; ++i) n += kRuns[i].count;
  return n;
}
constexpr uint16_t kLedCount = ledCount();

constexpr bool isPillar(Part p) {
  return p == Part::FrontLeft || p == Part::FrontRight || p == Part::BackRight || p == Part::BackLeft;
}

// The four pillars in the order they stand around the shelf (clockwise from above).
constexpr Part kPillars[4] = {Part::FrontLeft, Part::FrontRight, Part::BackRight, Part::BackLeft};

struct Pixel {
  Part part = Part::Unused;
  uint8_t pos = 0;     // along the part: from the top (pillar) / from the left (front face)
  uint8_t height = 0;  // 0 = bottom .. 255 = top
  uint8_t around = 0;  // 0..255 clockwise from above, front-left pillar = 0
};

// Pixels of a part (0 if the shelf has no such part).
uint8_t partCount(Part p);

// Electrical index of the pos-th pixel of a part, counted from the given end
// (Top / Bottom for a pillar, Left / Right for the front face). -1 = no such pixel
// (or an end that does not fit the part, e.g. From::Left on a pillar).
int16_t index(Part p, uint8_t pos, From from);

// Geometry of the pixel at an electrical index (Part::Unused when out of range).
Pixel pixel(uint16_t index);

}  // namespace shelf
}  // namespace ufo
