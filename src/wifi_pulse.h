#pragma once
#include <math.h>
#include "weather_theme.h"
#include "limit_ring_color.h"

namespace WifiPulse {
constexpr uint32_t FRAME_MS = 50;
// Slow honey -> sage -> sky -> honey, with a separate gentle breathing cycle.
inline uint16_t color(uint32_t now, int radius) {
  const uint16_t colors[] = {WeatherTheme::HONEY, WeatherTheme::SAGE, WeatherTheme::SKY};
  const uint32_t phase = now % 18000;
  const unsigned segment = phase / 6000;
  const float t = (phase % 6000) / 6000.0f;
  const float eased = t * t * (3.0f - 2.0f * t);
  const uint16_t hue = LimitRingColor::blend(colors[segment], colors[(segment + 1) % 3], eased);
  const float breath = 0.5f - 0.5f * cosf((now % 3600) * 0.001745329f);
  const float distance = fabsf(radius - 106.0f) / 16.0f;
  const float intensity = (0.14f + 0.38f * breath) * (1.0f - 0.6f * distance);
  return LimitRingColor::blend(0, hue, intensity);
}
}
