#pragma once
#include <stddef.h>
#include <stdint.h>
#include <new>

// One set of text slots for the visible screen. Measurement data lives elsewhere.
// Reconstruct slots on screen changes to release String heap capacity as well.
template <typename State, size_t Count>
class ActiveTextCache {
 public:
  enum class Screen : uint8_t { NONE, CLAUDE, WEATHER, STATUS, WIFI };
  bool select(Screen screen) {
    if (screen == current) return false;
    for (auto &slot : slots) {
      slot.~State();
      new (&slot) State();
    }
    current = screen;
    return true;
  }
  State &operator[](size_t index) { return slots[index]; }
 private:
  Screen current = Screen::NONE;
  State slots[Count];
};
