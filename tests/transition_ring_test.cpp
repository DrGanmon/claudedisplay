#include "../src/transition_ring.h"
#include <cassert>
int main() {
  TransitionRing ring{119, 104, 90, 0xffff, 0x8410};
  assert(ring.pixel(120, 120) == 0);
  assert(ring.pixel(0, 0) == 0);
  assert(ring.pixel(120, 230) == 0xffff); // zero degrees at bottom
  assert(ring.pixel(10, 120) == 0xffff);  // 90 degrees at left
  assert(ring.pixel(120, 10) == 0x8410); // track at top
  assert(ring.pixel(230, 120) == 0x8410);
  ring.angle = 360;
  assert(ring.pixel(230, 120) == 0xffff);
  ring.angle = 0;
  assert(ring.pixel(120, 230) == 0x8410);
}
