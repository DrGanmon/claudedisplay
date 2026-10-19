#include "../src/ring_startup.h"
#include <cassert>
int main() {
  RingStartup fill;
  assert(fill.angle(360, 100) == 0);
  int last = 0;
  for (uint32_t t = 140; t < 15100; t += 40) {
    const int angle = fill.angle(360, t);
    assert(angle >= last && angle <= 360);
    last = angle;
  }
  assert(fill.angle(360, 15100) == 360 && fill.finished());
  assert(fill.angle(240, 16000) == 240); // No replay on refresh.
  RingStartup pause;
  pause.angle(300, 0);
  assert(pause.angle(300, 7500) == 150);
  pause.pause();
  assert(pause.angle(300, 100000) == 150); // Hidden time is excluded.
  assert(pause.angle(300, 107500) == 300 && pause.finished());
  RingStartup wrap;
  wrap.angle(360, 0xfffffff0UL);
  assert(wrap.angle(360, 7484) == 180);
  assert(wrap.angle(360, 14984) == 360);
  RingStartup empty;
  assert(empty.angle(0, 0) == 0);
  assert(empty.angle(0, 15000) == 0 && empty.finished());
}
