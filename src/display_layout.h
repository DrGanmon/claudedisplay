#pragma once
#include <stdint.h>

namespace DisplayLayout {
struct TextArea { int y; int width; int height; uint8_t font; };
constexpr TextArea NAME = {58, 104, 16, 2};
constexpr TextArea TEMPERATURE = {76, 120, 16, 2};
constexpr TextArea CLOCK = {100, 144, 28, 4};
constexpr TextArea SESSION = {124, 160, 16, 2};
constexpr TextArea SESSION_RESET = {139, 150, 10, 1};
constexpr TextArea WEEKLY = {154, 150, 16, 2};
constexpr TextArea WEEKLY_RESET = {169, 132, 10, 1};
constexpr TextArea TRUECONF = {181, 108, 8, 1};
constexpr TextArea NETWORK = {191, 92, 8, 1};
// Wetterseite: schmale Kopf-/Fusszeilen, breite Mitte. Alle Sprite-Ecken
// bleiben innerhalb Radius 112, also mindestens 8 Pixel vom runden Rand.
constexpr TextArea WEATHER_TITLE = {26, 80, 16, 2};
constexpr TextArea WEATHER_LOCATION = {46, 132, 16, 2};
constexpr TextArea WEATHER_ICON = {80, 64, 44, 1};
constexpr TextArea WEATHER_TEMPERATURE = {122, 196, 28, 4};
constexpr TextArea WEATHER_CONDITION = {148, 196, 18, 2};
constexpr TextArea WEATHER_HUMIDITY = {170, 180, 16, 2};
constexpr TextArea WEATHER_WIND = {190, 150, 16, 2};
constexpr TextArea WEATHER_STATUS = {208, 118, 10, 1};
constexpr TextArea WEATHER_SOURCE = {223, 64, 8, 1};
}
