#pragma once
#include <stdint.h>
#include <math.h>

class PageTransition {
 public:
  struct Frame { bool ready, opening, switchPage, done; int radius; };
  bool active() const { return running; }
  void start(uint32_t now) { running = true; switched = false; started = now; last = now - 40; }
  Frame update(uint32_t now) {
    if (!running || static_cast<uint32_t>(now - last) < 40) return {false, false, false, false, 0};
    last = now;
    const uint32_t elapsed = now - started;
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
 private:
  bool running = false, switched = false;
  uint32_t started = 0, last = 0;
};
