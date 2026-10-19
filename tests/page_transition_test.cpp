#include "../src/page_transition.h"
#include <cassert>
int main() {
  PageTransition t;
  assert(!t.active());
  t.start(100);
  auto f = t.update(100);
  assert(f.ready && !f.opening && f.radius == 172);
  assert(!t.update(120).ready);
  f = t.update(240);
  assert(f.ready && !f.opening && f.radius == 86);
  f = t.update(380);
  assert(f.switchPage && f.opening && f.radius == 0);
  f = t.update(520);
  assert(f.opening && !f.switchPage && f.radius == 86);
  f = t.update(660);
  assert(f.done && f.radius == 172 && !t.active());
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
  t.start(0xffffff00UL);
  assert(t.update(static_cast<uint32_t>(0xffffff00UL + 560)).done);
}
