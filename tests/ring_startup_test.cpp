#include "../src/ring_startup.h"
#include <cassert>
int main() {
  RingStartup fill;
  assert(fill.angle(360, 100) == 0);
  int last = 0;
  for (uint32_t t = 140; t < 5100; t += 40) {
    const int angle = fill.angle(360, t);
    assert(angle >= last && angle <= 360);
    last = angle;
  }
  assert(!fill.finished());
  assert(fill.angle(360, 5100) == 360 && fill.finished());
  assert(fill.angle(240, 6000) == 240); // No replay on refresh.
  RingStartup pause;
  pause.angle(300, 0);
  assert(pause.angle(300, 2500) == 150);
  pause.pause();
  assert(pause.angle(300, 100000) == 150); // Hidden time is excluded.
  assert(pause.angle(300, 102500) == 300 && pause.finished());
  RingStartup wrap;
  wrap.angle(360, 0xfffffff0UL);
  assert(wrap.angle(360, 2484) == 180);
  assert(wrap.angle(360, 4984) == 360);
  RingStartup empty;
  assert(empty.angle(0, 0) == 0);
  assert(empty.angle(0, 5000) == 0 && empty.finished());
}
