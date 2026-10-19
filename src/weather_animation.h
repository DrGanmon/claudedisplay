#pragma once
#include <TFT_eSPI.h>
#include <math.h>
#include "display_layout.h"

// Small reusable 8-bit sprite: 2816 bytes instead of a full-screen buffer.
// Release it whenever the weather page is hidden, leaving heap for TLS.
class WeatherAnimation {
 public:
  explicit WeatherAnimation(TFT_eSPI &screen) : sprite(&screen) {}
  void release() { sprite.deleteSprite(); ready = false; }
  void draw(int code, uint32_t now, bool force = false) {
    if (!force && ready && now - lastFrame < 40) return;
    if (!ready) {
      sprite.setColorDepth(8);
      if (!sprite.createSprite(64, 44)) return;
      ready = true;
    }
    lastFrame = now;
    sprite.fillSprite(TFT_BLACK);
    const int phase = (now / 80) % 24;
    if (code >= 0 && code <= 2) {
      const int cx = code == 0 ? 32 : 22, cy = 18;
      for (int i = 0; i < 8; ++i) {
        const float angle = (i * 45 + (now / 80) % 360) * 0.0174533f;
        sprite.drawLine(cx + cosf(angle) * 12, cy + sinf(angle) * 12,
                        cx + cosf(angle) * 16, cy + sinf(angle) * 16, TFT_YELLOW);
      }
      sprite.fillCircle(cx, cy, 9, TFT_YELLOW);
    }
    if (code == 45 || code == 48) {
      for (int i = 0; i < 4; ++i) {
        const int shift = ((phase + i * 5) % 12) - 6;
        sprite.drawFastHLine(12 + shift, 10 + i * 8, 40, TFT_LIGHTGREY);
      }
    } else if (code > 0 && code <= 99 && known(code)) {
      const int shift = phase < 12 ? phase / 4 : (23 - phase) / 4;
      cloud(shift);
      if (code >= 51) {
        for (int i = 0; i < 4; ++i) {
          const int x = 14 + i * 11;
          const int y = 27 + (phase + i * 7) % 15;
          if (snow(code)) {
            sprite.drawFastHLine(x - 2, y, 5, TFT_WHITE);
            sprite.drawFastVLine(x, y - 2, 5, TFT_WHITE);
          } else if (code == 96 || code == 99) {
            sprite.fillCircle(x, y, 2, TFT_CYAN);
          } else {
            sprite.drawLine(x, y, x - 2, y + (code < 60 ? 2 : 4), TFT_CYAN);
          }
        }
      }
      if (code >= 95 && phase < 5) {
        sprite.fillTriangle(34, 23, 26, 34, 34, 32, TFT_YELLOW);
        sprite.fillTriangle(30, 31, 37, 29, 28, 43, TFT_YELLOW);
      }
    } else if (code != 0) {
      sprite.setTextDatum(MC_DATUM);
      sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sprite.drawString("--", 32, 22, 2);
    }
    sprite.pushSprite(120 - DisplayLayout::WEATHER_ICON.width / 2,
                      DisplayLayout::WEATHER_ICON.y - DisplayLayout::WEATHER_ICON.height / 2);
  }
 private:
  TFT_eSprite sprite;
  bool ready = false;
  uint32_t lastFrame = 0;
  static bool snow(int code) { return (code >= 71 && code <= 77) || code == 85 || code == 86; }
  static bool known(int code) {
    switch (code) {
      case 1: case 2: case 3: case 51: case 53: case 55: case 56: case 57:
      case 61: case 63: case 65: case 66: case 67: case 71: case 73: case 75:
      case 77: case 80: case 81: case 82: case 85: case 86: case 95: case 96: case 99:
        return true;
      default: return false;
    }
  }
  void cloud(int shift) {
    const uint16_t color = TFT_LIGHTGREY;
    sprite.fillCircle(20 + shift, 20, 9, color);
    sprite.fillCircle(31 + shift, 14, 12, color);
    sprite.fillCircle(44 + shift, 20, 9, color);
    sprite.fillRect(20 + shift, 18, 25, 11, color);
  }
};
