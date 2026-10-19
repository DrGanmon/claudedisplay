#pragma once
#include <stdint.h>

// Tracks visible animation time. No delays, buffers or repeated startup fills.
class RingStartup {
 public:
  bool finished() const { return complete; }
  void pause() { ticking = false; }
  int angle(int target, uint32_t now) {
    if (complete) return target;
    if (ticking) {
      const uint32_t delta = now - last;
      elapsed += delta < DURATION - elapsed ? delta : DURATION - elapsed;
    }
    last = now;
    ticking = true;
    if (elapsed >= DURATION) { complete = true; return target; }
    const float progress = elapsed / static_cast<float>(DURATION);
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    return static_cast<int>(target * eased + 0.5f);
  }
 private:
  static constexpr uint32_t DURATION = 5000;
  uint32_t elapsed = 0, last = 0;
  bool ticking = false, complete = false;
};
