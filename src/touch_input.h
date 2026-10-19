#pragma once
#include <stdint.h>

class TouchInput {
 public:
  enum class Event { NONE, SHORT_PRESS, LONG_PRESS };
  bool isArmed() const { return armed; }
  bool isPressed() const { return armed && stable; }
  Event update(bool pressed, uint32_t now) {
    if (!initialized || pressed != raw) {
      initialized = true;
      raw = pressed;
      changed = now;
    }
    if (static_cast<uint32_t>(now - changed) < 35) return Event::NONE;
    // An absent module is pulled HIGH. Arm only after a stable idle LOW,
    // including when a connected sensor finishes its power-on calibration.
    if (!armed) {
      if (!raw) { armed = true; stable = false; }
      return Event::NONE;
    }
    if (raw != stable) {
      stable = raw;
      if (stable) { started = now; longHandled = false; }
      else {
        const bool shortPress = !longHandled && static_cast<uint32_t>(now - started) < 5000;
        longHandled = true;
        return shortPress ? Event::SHORT_PRESS : Event::NONE;
      }
    }
    if (stable && !longHandled && static_cast<uint32_t>(now - started) >= 5000) {
      longHandled = true;
      return Event::LONG_PRESS;
    }
    return Event::NONE;
  }
 private:
  bool initialized = false, raw = false, stable = false, armed = false, longHandled = true;
  uint32_t changed = 0, started = 0;
};
