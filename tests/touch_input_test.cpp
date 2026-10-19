#include "../src/touch_input.h"
#include <cassert>
int main() {
  using E = TouchInput::Event;
  TouchInput touch;
  // HIGH at power-on or absent module must never trigger a reset.
  assert(touch.update(true, 0) == E::NONE);
  assert(touch.update(true, 6000) == E::NONE);
  assert(touch.update(false, 6010) == E::NONE);
  assert(touch.update(false, 6045) == E::NONE);
  // Bounce is ignored; a short press triggers once on release.
  assert(touch.update(true, 6100) == E::NONE);
  assert(touch.update(false, 6110) == E::NONE);
  assert(touch.update(false, 6150) == E::NONE);
  assert(touch.update(true, 6200) == E::NONE);
  assert(touch.update(true, 6235) == E::NONE);
  assert(touch.update(false, 6400) == E::NONE);
  assert(touch.update(false, 6435) == E::SHORT_PRESS);
  assert(touch.update(false, 6500) == E::NONE);
  // Long press fires once and does not switch pages on release.
  touch.update(true, 7000);
  touch.update(true, 7035);
  assert(touch.update(true, 12034) == E::NONE);
  assert(touch.update(true, 12035) == E::LONG_PRESS);
  assert(touch.update(true, 13000) == E::NONE);
  touch.update(false, 14000);
  assert(touch.update(false, 14035) == E::NONE);
  // millis() rollover.
  TouchInput wrap;
  wrap.update(false, 0xfffffe00UL);
  wrap.update(false, 0xfffffe30UL);
  wrap.update(true, 0xffffff00UL);
  wrap.update(true, 0xffffff30UL);
  wrap.update(false, 10);
  assert(wrap.update(false, 45) == E::SHORT_PRESS);
}
