#pragma once

namespace TextStrips {
constexpr int HEIGHT = 4;

// Sprite is a reusable width x 4, 16-bit buffer with centered text datum.
// Keep the original full-row text origin while moving the strip underneath it.
template <typename Sprite, typename Screen, typename Text>
void draw(Sprite &strip, Screen &screen, const Text &text,
          int x, int y, int width, int height) {
  for (int offset = 0; offset < height; offset += HEIGHT) {
    const int count = height - offset < HEIGHT ? height - offset : HEIGHT;
    if (!screen.checkViewport(x, y + offset, width, count)) continue;
    strip.fillSprite(0);
    strip.drawString(text, width / 2, height / 2 - offset);
    // Crop the final partial strip so adjacent rows cannot be overwritten.
    strip.pushSprite(x, y + offset, 0, 0, width, count);
  }
}
}
