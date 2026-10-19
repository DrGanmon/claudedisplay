#include "../src/text_strips.h"
#include <cassert>
#include <vector>
#include <algorithm>

struct Screen {
  static const int WIDTH = 40, HEIGHT = 40;
  std::vector<int> pixels = std::vector<int>(WIDTH * HEIGHT, 99);
  int left = 0, top = 0, right = WIDTH, bottom = HEIGHT;
  bool checkViewport(int x, int y, int w, int h) {
    return x < right && x + w > left && y < bottom && y + h > top;
  }
  void pixel(int x, int y, int color) {
    if (x >= left && x < right && y >= top && y < bottom) pixels[y * WIDTH + x] = color;
  }
};
struct Sprite {
  Screen &screen;
  int width, height;
  std::vector<int> pixels;
  Sprite(Screen &s, int w, int h) : screen(s), width(w), height(h), pixels(w * h) {}
  void fillSprite(int color) { std::fill(pixels.begin(), pixels.end(), color); }
  // Asymmetric mock glyph catches incorrect centering and vertical offsets.
  void drawString(bool visible, int cx, int cy) {
    if (!visible) return;
    for (int y = cy - 5; y < cy + 6; ++y)
      for (int x = cx - 3; x < cx + 4; ++x)
        if (x >= 0 && x < width && y >= 0 && y < height)
          pixels[y * width + x] = 1 + (y - cy + 5) * 7 + x - cx + 3;
  }
  void pushSprite(int x, int y, int sx, int sy, int w, int h) {
    assert(sx >= 0 && sy >= 0 && sx + w <= width && sy + h <= height);
    for (int iy = 0; iy < h; ++iy)
      for (int ix = 0; ix < w; ++ix)
        screen.pixel(x + ix, y + iy, pixels[(sy + iy) * width + sx + ix]);
  }
};
int main() {
  // Compare strip output pixel-for-pixel with a complete centered row,
  // including partial strips, empty text clearing and an iris-like viewport.
  for (int height : {8, 10, 16, 18, 28})
    for (bool visible : {false, true})
      for (bool clipped : {false, true}) {
        Screen expected, actual;
        if (clipped) {
          expected.left = actual.left = 13; expected.right = actual.right = 22;
          expected.top = actual.top = 10; expected.bottom = actual.bottom = 19;
        }
        Sprite full(expected, 20, height), strip(actual, 20, TextStrips::HEIGHT);
        full.fillSprite(0);
        full.drawString(visible, 10, height / 2);
        full.pushSprite(8, 4, 0, 0, 20, height);
        TextStrips::draw(strip, actual, visible, 8, 4, 20, height);
        assert(expected.pixels == actual.pixels);
      }
}
