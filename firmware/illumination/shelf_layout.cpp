#include "shelf_layout.h"

namespace ufo {
namespace shelf {

namespace {

// Position counted from the top (pillar) / the left (front face) of the k-th
// pixel of a run, k counted along the data line.
uint8_t canonicalPos(const Run& r, uint8_t k) {
  bool reversed = r.wiring == Wiring::BottomUp || r.wiring == Wiring::RightToLeft;
  return reversed ? static_cast<uint8_t>(r.count - 1 - k) : k;
}

uint8_t cornerAround(Part p) {
  switch (p) {
    case Part::FrontLeft: return 0;
    case Part::FrontRight: return 64;
    case Part::BackRight: return 128;
    case Part::BackLeft: return 192;
    default: return 0;
  }
}

}  // namespace

uint8_t partCount(Part p) {
  uint16_t n = 0;
  for (uint8_t i = 0; i < kRunCount; ++i)
    if (kRuns[i].part == p) n += kRuns[i].count;
  return n > 255 ? 255 : static_cast<uint8_t>(n);
}

int16_t index(Part p, uint8_t pos, From from) {
  if (p == Part::Unused) return -1;
  bool pillar = isPillar(p);
  if (pillar && (from == From::Left || from == From::Right)) return -1;
  if (!pillar && (from == From::Top || from == From::Bottom)) return -1;
  uint8_t n = partCount(p);
  if (pos >= n) return -1;
  uint8_t canon = (from == From::Bottom || from == From::Right) ? static_cast<uint8_t>(n - 1 - pos) : pos;
  // A part is normally one run; if it is split over several runs, they are taken
  // in chain order, each covering the next stretch from the top / left.
  uint16_t first = 0;
  uint8_t seen = 0;
  for (uint8_t i = 0; i < kRunCount; ++i) {
    const Run& r = kRuns[i];
    if (r.part == p) {
      if (canon < seen + r.count) {
        uint8_t local = static_cast<uint8_t>(canon - seen);
        bool reversed = r.wiring == Wiring::BottomUp || r.wiring == Wiring::RightToLeft;
        return static_cast<int16_t>(first + (reversed ? r.count - 1 - local : local));
      }
      seen = static_cast<uint8_t>(seen + r.count);
    }
    first += r.count;
  }
  return -1;
}

Pixel pixel(uint16_t idx) {
  Pixel px;
  uint16_t first = 0;
  for (uint8_t i = 0; i < kRunCount; ++i) {
    const Run& r = kRuns[i];
    if (idx < first + r.count) {
      if (r.part == Part::Unused) return px;
      uint8_t k = static_cast<uint8_t>(idx - first);
      // the position along the whole part (runs of the same part before this one come first)
      uint8_t before = 0;
      for (uint8_t j = 0; j < i; ++j)
        if (kRuns[j].part == r.part) before = static_cast<uint8_t>(before + kRuns[j].count);
      uint8_t pos = static_cast<uint8_t>(before + canonicalPos(r, k));
      uint8_t n = partCount(r.part);
      px.part = r.part;
      px.pos = pos;
      if (isPillar(r.part)) {
        px.height = n > 1 ? static_cast<uint8_t>(255 - pos * 255 / (n - 1)) : 255;
        px.around = cornerAround(r.part);
      } else {
        px.height = kFrontHeight;
        // evenly spread between the two front pillars (they sit at 0 and 64)
        px.around = static_cast<uint8_t>((pos + 1) * 64 / (n + 1));
      }
      return px;
    }
    first += r.count;
  }
  return px;
}

}  // namespace shelf
}  // namespace ufo
