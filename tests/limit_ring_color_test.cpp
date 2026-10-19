#include "../src/limit_ring_color.h"
#include <cassert>
int main() {
  using namespace LimitRingColor;
  assert(consumed(-10) == GREY);
  assert(consumed(0) == GREY);
  assert(consumed(50) == GREY);
  assert(consumed(75) == CREAM_YELLOW);
  assert(consumed(100) == CREAM_RED);
  assert(consumed(150) == CREAM_RED);
  for (int i = 0; i <= 500; ++i) assert(consumed(i / 10.0f) == GREY);
  assert(consumed(62.5f) != GREY && consumed(62.5f) != CREAM_YELLOW);
  assert(consumed(87.5f) != CREAM_YELLOW && consumed(87.5f) != CREAM_RED);
  // Channels progress monotonically on each side of the yellow anchor.
  for (int i = 501; i <= 750; ++i) {
    const uint16_t a = consumed((i - 1) / 10.0f), b = consumed(i / 10.0f);
    assert((b >> 11) >= (a >> 11));
    assert(((b >> 5) & 63) >= ((a >> 5) & 63));
    assert((b & 31) >= (a & 31));
  }
  for (int i = 751; i <= 1000; ++i) {
    const uint16_t a = consumed((i - 1) / 10.0f), b = consumed(i / 10.0f);
    assert(((b >> 5) & 63) <= ((a >> 5) & 63));
  }
}
