#pragma once
#include "text_strips.h"

// A single fixed-size scratch sprite, reused for all visible text rows.
// Release explicitly before TLS; do not repeatedly resize it for each label.
template <typename Sprite>
class ReusableTextBuffer {
 public:
  static constexpr int WIDTH = 210;
  explicit ReusableTextBuffer(Sprite &value) : sprite(value) {}
  bool ensure() {
    if (ready) return true;
    sprite.setColorDepth(16);
    ready = sprite.createSprite(WIDTH, TextStrips::HEIGHT) != nullptr;
    return ready;
  }
  void release() {
    if (ready) sprite.deleteSprite();
    ready = false;
  }
 private:
  Sprite &sprite;
  bool ready = false;
};
