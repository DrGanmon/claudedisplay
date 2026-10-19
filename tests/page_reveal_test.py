"""Run the actual iris driver against a clipped framebuffer test double."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/main.cpp').read_text()
a = source.index('void drawPageTransition() {')
function = source[a:source.index('\n}', a) + 2]
harness = r'''
#include "page_transition.h"
#include <cassert>
#include <vector>
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
void yield() {}
constexpr int TFT_BLACK = 0;
PageTransition pageTransition;
bool transitionRendering = false, displayNeedsFullRedraw = false;
struct {
  std::vector<int> pixels = std::vector<int>(240 * 240, 7);
  int left = 0, top = 0, right = 240, bottom = 240;
  void resetViewport() { left = top = 0; right = bottom = 240; }
  void setViewport(int x, int y, int w, int h, bool local) {
    assert(!local);
    left = x; top = y; right = x + w; bottom = y + h;
  }
  void fillRect(int x, int y, int w, int h, int color) {
    for (int iy = y; iy < y + h; ++iy)
      for (int ix = x; ix < x + w; ++ix)
        if (ix >= left && ix < right && iy >= top && iy < bottom)
          pixels[iy * 240 + ix] = color;
  }
} display;
int target(int x, int y) { return 1000 + y * 240 + x; }
int preparations = 0;
void prepareTransitionPage() { ++preparations; }
struct DisplayTextCache { enum class Screen { CLAUDE, WEATHER }; };
struct { DisplayTextCache::Screen screen() { return DisplayTextCache::Screen::WEATHER; } } activeTextCache;
void drawClaudeRings(bool) {}
void renderTransitionSlice(int x, int y, int width, int height) {
  assert(transitionRendering);
  display.setViewport(x, y, width, height, false);
  for (int y = display.top; y < display.bottom; ++y)
    for (int x = display.left; x < display.right; ++x)
      display.pixels[y * 240 + x] = target(x, y);
  displayNeedsFullRedraw = false;
}
'''
harness += function
harness += r'''
int main() {
  // Test normal cadence and very slow rendering; both must reveal content early.
  for (int frameTime : {40, 1000}) {
    display.pixels.assign(240 * 240, 7);
    clockMs = 0;
    preparations = 0;
    pageTransition.start(clockMs);
    for (int i = 0; i <= 14; ++i) {
      clockMs = i * frameTime;
      drawPageTransition();
      assert(display.left == 0 && display.top == 0 && display.right == 240 && display.bottom == 240);
      if (i == 7) for (int pixel : display.pixels) assert(pixel == 0);
      if (i == 8) {
        assert(pageTransition.active());
        assert(display.pixels[120 * 240 + 120] == target(120, 120));
        assert(display.pixels[0] == 0);
      }
    }
    assert(!pageTransition.active());
    assert(preparations == 1);
    for (int y = 0; y < 240; ++y)
      for (int x = 0; x < 240; ++x)
        assert(display.pixels[y * 240 + x] == target(x, y));
  }
}
'''
with tempfile.TemporaryDirectory(prefix='claudedisplay-reveal-') as directory:
    cpp, binary = Path(directory) / 'reveal.cpp', Path(directory) / 'reveal'
    cpp.write_text(harness)
    subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-I', str(root / 'src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Iris driver: content visible during opening, complete reveal and viewport restoration OK')
