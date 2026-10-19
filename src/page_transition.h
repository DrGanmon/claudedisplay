#pragma once
#include <stdint.h>
#include <math.h>

class PageTransition {
 public:
  struct Frame { bool ready, opening, switchPage, done; int radius; };
  bool active() const { return running; }
  void start(uint32_t now) { running = true; switched = false; first = true; elapsed = 0; last = now - 40; }
  Frame update(uint32_t now) {
    if (!running || static_cast<uint32_t>(now - last) < 40) return {false, false, false, false, 0};
    last = now;
    // Advance by one visible frame, even if rendering took longer than 40 ms.
    // Never skip the opening phase just because clipped drawing is expensive.
    if (first) first = false;
    else elapsed += 40;
    const bool opening = elapsed >= 280;
    const bool change = opening && !switched;
    if (opening) switched = true;
    const bool done = elapsed >= 560;
    const float t = done ? 1.0f : (opening ? elapsed - 280 : elapsed) / 280.0f;
    const float eased = t * t * (3.0f - 2.0f * t);
    const int radius = static_cast<int>(172 * (opening ? eased : 1 - eased) + 0.5f);
    if (done) running = false;
    return {true, opening, change, done, radius};
  }
  // Conservative chord per four-pixel strip, fully inside the aperture.
  static int halfWidth(int radius, int y, int height = 4) {
    const int top = y - 120, bottom = y + height - 1 - 120;
    const int dy = (top < 0 ? -top : top) > (bottom < 0 ? -bottom : bottom)
        ? (top < 0 ? -top : top) : (bottom < 0 ? -bottom : bottom);
    if (radius <= dy) return 0;
    const int width = static_cast<int>(sqrtf(static_cast<float>(radius * radius - dy * dy)));
    return width > 120 ? 120 : width;
  }
  static bool intersectsRing(int outer, int inner, int x, int y, int width, int height) {
    const int x1 = x - 120, x2 = x + width - 1 - 120;
    const int y1 = y - 120, y2 = y + height - 1 - 120;
    const int minX = x1 > 0 ? x1 : (x2 < 0 ? -x2 : 0);
    const int minY = y1 > 0 ? y1 : (y2 < 0 ? -y2 : 0);
    const int maxX = (x1 < 0 ? -x1 : x1) > (x2 < 0 ? -x2 : x2) ? (x1 < 0 ? -x1 : x1) : (x2 < 0 ? -x2 : x2);
    const int maxY = (y1 < 0 ? -y1 : y1) > (y2 < 0 ? -y2 : y2) ? (y1 < 0 ? -y1 : y1) : (y2 < 0 ? -y2 : y2);
    return minX * minX + minY * minY <= (outer + 1) * (outer + 1) &&
           maxX * maxX + maxY * maxY >= (inner - 1) * (inner - 1);
  }
 private:
  bool running = false, switched = false, first = true;
  uint32_t elapsed = 0, last = 0;
};
