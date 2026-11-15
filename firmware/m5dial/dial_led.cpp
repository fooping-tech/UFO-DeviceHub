#include "dial_led.h"

namespace ufo {

uint16_t ledPathLength(const LedGeometry& g) {
  if (g.count == 0) return 0;
  return g.layout == LedLayout::Folded ? static_cast<uint16_t>((g.count + 1) / 2) : g.count;
}

uint16_t ledPathPos(const LedGeometry& g, uint16_t i) {
  if (g.count == 0) return 0;
  i %= g.count;
  const uint16_t len = ledPathLength(g);
  // folded: pixel i (going up) and pixel count-1-i (coming down) share a height
  uint16_t pos = (g.layout == LedLayout::Folded && i >= len) ? static_cast<uint16_t>(g.count - 1 - i) : i;
  return g.reversed ? static_cast<uint16_t>(len - 1 - pos) : pos;
}

uint8_t dialEffectLevel(DialEffect effect, DialDirection direction, uint16_t pos, uint16_t len,
                        uint32_t elapsed_ms, uint16_t duration_ms) {
  if (len == 0 || pos >= len) return 0;
  // q: distance from where the light comes from (0 = source end of the flow)
  const uint16_t q = direction == DialDirection::ToDial ? static_cast<uint16_t>(len - 1 - pos) : pos;
  switch (effect) {
    case DialEffect::None: return 0;
    case DialEffect::Solid: return 255;
    case DialEffect::Breathe: {
      uint32_t ph = elapsed_ms % kBreathePeriodMs;
      uint32_t half = kBreathePeriodMs / 2;
      uint32_t v = ph < half ? ph : kBreathePeriodMs - ph;  // triangle 0..half
      return static_cast<uint8_t>(55 + v * 200 / half);
    }
    case DialEffect::Chase: {
      uint32_t step = elapsed_ms / kChaseStepMs;
      return (q % kChaseSpacing) == (step % kChaseSpacing) ? 255 : kChaseDimLevel;
    }
    case DialEffect::Comet: {
      // the head runs from the source past the far end so the tail leaves the path
      uint32_t head = (elapsed_ms % kCometPeriodMs) * (len + kCometTail) / kCometPeriodMs;
      if (q > head || head - q > kCometTail) return 0;
      return static_cast<uint8_t>(255 >> (head - q));
    }
    case DialEffect::Wipe: {
      uint32_t span = duration_ms ? duration_ms : kWipeDefaultMs;
      uint32_t ph = duration_ms ? (elapsed_ms < span ? elapsed_ms : span) : elapsed_ms % span;
      uint32_t lit = (ph * len + span - 1) / span;  // 0 at the start, len at the end
      return q < lit ? 255 : 0;
    }
    case DialEffect::Flash: return (elapsed_ms % kFlashPeriodMs) < kFlashPeriodMs / 2 ? 255 : 0;
    case DialEffect::Sparkle: {
      uint32_t h = (elapsed_ms / kSparkleFrameMs) * 2654435761u + pos * 40503u;
      h ^= h >> 15;
      h *= 2246822519u;
      h ^= h >> 13;
      return (h & 0x07) == 0 ? 255 : 12;
    }
  }
  return 0;
}

}  // namespace ufo
