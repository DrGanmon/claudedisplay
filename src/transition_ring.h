#pragma once
#include <math.h>
#include <stdint.h>
#include "limit_ring_color.h"

struct TransitionRing {
  int radius, inner, angle;
  uint16_t color, track;
  uint16_t pixel(int x, int y) const {
    const int dx = x - 120, dy = y - 120;
    const int d2 = dx * dx + dy * dy;
    if (d2 >= (radius + 1) * (radius + 1) || d2 <= (inner - 1) * (inner - 1)) return 0;
    const float d = sqrtf(static_cast<float>(d2));
    float coverage = fminf(radius + 1 - d, d - (inner - 1));
    if (coverage > 1) coverage = 1;
    bool remaining = angle >= 360;
    if (angle > 0 && angle < 360) {
      float degrees = atan2f(-static_cast<float>(dx), static_cast<float>(dy)) * 57.2957795f;
      if (degrees < 0) degrees += 360;
      remaining = degrees <= angle;
    }
    return LimitRingColor::blend(0, remaining ? color : track, coverage);
  }
};
