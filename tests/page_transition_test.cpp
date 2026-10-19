#include "../src/page_transition.h"
#include <cassert>
int main() {
  PageTransition t;
  assert(!t.active());
  t.start(100);
  auto f = t.update(100);
  assert(f.ready && !f.opening && f.radius == 172);
  assert(!t.update(120).ready);
  int switches = 0, openingFrames = 0;
  for (int i = 1; i <= 14; ++i) {
    f = t.update(100 + i * 40);
    assert(f.ready);
    switches += f.switchPage;
    openingFrames += f.opening;
    if (i < 7) assert(!f.opening);
    if (i == 7) assert(f.switchPage && f.radius == 0);
    if (i == 14) assert(f.done && f.radius == 172 && !t.active());
  }
  assert(switches == 1 && openingFrames == 8);
  for (int y = 0; y < 240; y += 4) {
    assert(PageTransition::halfWidth(0, y) == 0);
    assert(PageTransition::halfWidth(172, y) == 120);
    int previous = 0;
    for (int r = 0; r <= 172; ++r) {
      const int width = PageTransition::halfWidth(r, y);
      assert(width >= previous && width <= 120);
      previous = width;
    }
  }
  // Slow draws must still show all opening steps, rather than jump to done.
  t.start(0);
  t.update(0);
  for (int i = 1; i <= 14; ++i) {
    f = t.update(i * 1000);
    assert(f.ready && (i == 14 || !f.done));
    if (i == 7) assert(f.opening && f.switchPage && f.radius == 0);
    if (i == 10) assert(f.opening && f.radius > 0 && f.radius < 172);
  }
  assert(f.done);
  t.start(0xffffff00UL);
  for (int i = 0; i <= 14; ++i) f = t.update(static_cast<uint32_t>(0xffffff00UL + i * 40));
  assert(f.done);
  assert(!PageTransition::intersectsRing(119, 104, 100, 100, 40, 40));
  assert(PageTransition::intersectsRing(119, 104, 0, 116, 8, 8));
  assert(!PageTransition::intersectsRing(119, 104, 0, 0, 4, 4));
  assert(PageTransition::intersectsRing(98, 91, 22, 120, 4, 4));
}
