#pragma once
#include <stdint.h>

namespace LimitRingColor {
constexpr uint16_t GREY = 0x8410;
constexpr uint16_t CREAM_YELLOW = 0xFF14;
constexpr uint16_t CREAM_RED = 0xFD54;

inline uint16_t blend(uint16_t from, uint16_t to, float fraction) {
  const int r = (from >> 11) & 31, g = (from >> 5) & 63, b = from & 31;
  return static_cast<uint16_t>(
      (static_cast<int>(r + (((to >> 11) & 31) - r) * fraction + 0.5f) << 11) |
      (static_cast<int>(g + (((to >> 5) & 63) - g) * fraction + 0.5f) << 5) |
       static_cast<int>(b + ((to & 31) - b) * fraction + 0.5f));
}

inline uint16_t consumed(float used) {
  if (!(used > 50)) return GREY;
  if (used < 75) return blend(GREY, CREAM_YELLOW, (used - 50.0f) / 25.0f);
  if (used < 100) return blend(CREAM_YELLOW, CREAM_RED, (used - 75.0f) / 25.0f);
  return CREAM_RED;
}
}
