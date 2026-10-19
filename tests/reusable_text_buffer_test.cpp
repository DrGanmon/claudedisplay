#include "../src/reusable_text_buffer.h"
#include <cassert>
struct Sprite {
  int allocations = 0, releases = 0;
  bool fail = false;
  void setColorDepth(int depth) { assert(depth == 16); }
  void *createSprite(int width, int height) {
    assert(width == 210 && height == 4);
    ++allocations;
    return fail ? nullptr : this;
  }
  void deleteSprite() { ++releases; }
};
int main() {
  Sprite sprite;
  ReusableTextBuffer<Sprite> buffer(sprite);
  for (int i = 0; i < 100; ++i) assert(buffer.ensure());
  assert(sprite.allocations == 1);
  buffer.release();
  buffer.release();
  assert(sprite.releases == 1);
  assert(buffer.ensure() && sprite.allocations == 2);
  buffer.release();
  sprite.fail = true;
  assert(!buffer.ensure());
  buffer.release();
  assert(sprite.releases == 2); // Failed allocation has no owned buffer.
  sprite.fail = false;
  assert(buffer.ensure());
}
