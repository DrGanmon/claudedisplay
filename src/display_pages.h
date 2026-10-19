#pragma once
#include <stdint.h>
#include "touch_input.h"

class DisplayPages {
 public:
  enum class Page { CLAUDE, WEATHER, STATUS };
  Page current() const { return page; }
  bool weather() const { return page == Page::WEATHER; }
  bool status() const { return page == Page::STATUS; }
  bool requestsPaused() const { return page != Page::CLAUDE; }
  bool tapPending() const { return pending; }
  // Returns true exactly when the displayed page changes.
  bool update(TouchInput::Event event, bool pressed, uint32_t now) {
    if (event == TouchInput::Event::LONG_PRESS) { pending = false; return false; }
    if (event == TouchInput::Event::SHORT_PRESS) {
      if (status()) { page = previous; pending = false; return true; }
      if (pending && static_cast<uint32_t>(now - firstRelease) <= 350) {
        previous = page; page = Page::STATUS; pending = false; return true;
      }
      // A delayed release of a second press is a new single tap.
      if (pending) {
        page = weather() ? Page::CLAUDE : Page::WEATHER;
        firstRelease = now;
        return true;
      }
      firstRelease = now; pending = true;
    }
    if (pending && !pressed && static_cast<uint32_t>(now - firstRelease) >= 350) {
      pending = false;
      page = weather() ? Page::CLAUDE : Page::WEATHER;
      return true;
    }
    return false;
  }
 private:
  Page page = Page::CLAUDE, previous = Page::CLAUDE;
  bool pending = false;
  uint32_t firstRelease = 0;
};
