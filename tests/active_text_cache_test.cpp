#include "../src/active_text_cache.h"
#include <cassert>
#include <memory>
#include <initializer_list>

struct SavedText {
  static int live;
  SavedText() { ++live; }
  ~SavedText() { --live; }
};
int SavedText::live = 0;
struct State {
  std::unique_ptr<SavedText> text;
  int color = 0;
};
int main() {
  using Cache = ActiveTextCache<State, 10>;
  using Screen = Cache::Screen;
  Cache cache;
  assert(cache.select(Screen::CLAUDE));
  for (size_t i = 0; i < 9; ++i) {
    cache[i].text.reset(new SavedText());
    cache[i].color = 42;
  }
  assert(SavedText::live == 9);
  assert(!cache.select(Screen::CLAUDE));
  assert(SavedText::live == 9 && cache[0].color == 42);
  for (Screen screen : {Screen::WEATHER, Screen::STATUS, Screen::WIFI, Screen::CLAUDE}) {
    assert(cache.select(screen));
    assert(SavedText::live == 0);
    for (size_t i = 0; i < 10; ++i) {
      assert(!cache[i].text && cache[i].color == 0);
      cache[i].text.reset(new SavedText());
    }
    assert(!cache.select(screen) && SavedText::live == 10);
  }
}
