#include "../src/display_pages.h"
#include <cassert>
int main() {
  using E = TouchInput::Event;
  using P = DisplayPages::Page;
  DisplayPages p;
  assert(!p.update(E::SHORT_PRESS, false, 0));
  assert(p.tapPending() && p.current() == P::CLAUDE);
  assert(!p.update(E::NONE, false, 349));
  assert(p.update(E::NONE, false, 350));
  assert(p.weather());
  // Double tap opens status without briefly toggling the underlying page.
  assert(!p.update(E::SHORT_PRESS, false, 1000));
  assert(p.update(E::SHORT_PRESS, false, 1200));
  assert(p.status() && p.requestsPaused());
  assert(p.update(E::SHORT_PRESS, false, 1500));
  assert(p.weather() && !p.tapPending());
  p.update(E::SHORT_PRESS, false, 2000);
  p.update(E::NONE, false, 2350);
  assert(p.current() == P::CLAUDE && !p.requestsPaused());
  p.update(E::SHORT_PRESS, false, 3000);
  p.update(E::SHORT_PRESS, false, 3200);
  assert(p.status());
  p.update(E::SHORT_PRESS, false, 3400);
  assert(p.current() == P::CLAUDE);
  // Long press cancels a pending tap, including while held past the window.
  p.update(E::SHORT_PRESS, false, 4000);
  assert(!p.update(E::NONE, true, 4500));
  assert(!p.update(E::LONG_PRESS, true, 9500));
  assert(!p.tapPending() && p.current() == P::CLAUDE);
  // Unsigned timer rollover still recognizes a double tap.
  p.update(E::SHORT_PRESS, false, 0xffffff90UL);
  assert(p.update(E::SHORT_PRESS, false, 20));
  assert(p.status());
  // Sparse polling after the double-tap window still resolves two singles.
  DisplayPages slow;
  slow.update(E::SHORT_PRESS, false, 0);
  assert(slow.update(E::SHORT_PRESS, false, 500));
  assert(slow.weather() && slow.tapPending());
  assert(slow.update(E::NONE, false, 850));
  assert(slow.current() == P::CLAUDE);
  // Timeout across rollover resolves to one ordinary page change.
  DisplayPages wrap;
  wrap.update(E::SHORT_PRESS, false, 0xffffff90UL);
  assert(!wrap.update(E::NONE, false, 200));
  assert(wrap.update(E::NONE, false, 250));
  assert(wrap.weather());
}
