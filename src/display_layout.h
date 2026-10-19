#pragma once
#include <stdint.h>

namespace DisplayLayout {
struct TextArea { int y; int width; int height; uint8_t font; };
constexpr TextArea NAME = {58, 104, 16, 2};
constexpr TextArea TEMPERATURE = {82, 140, 28, 4};
constexpr TextArea CLOCK = {108, 120, 16, 2};
constexpr TextArea SESSION = {128, 160, 18, 2};
constexpr TextArea WEEKLY = {149, 152, 18, 2};
constexpr TextArea TRUECONF = {169, 120, 16, 2};
constexpr TextArea NETWORK = {183, 108, 10, 1};
// Wetterseite: schmale Kopf-/Fusszeilen, breite Mitte. Alle Sprite-Ecken
// bleiben innerhalb Radius 112, also mindestens 8 Pixel vom runden Rand.
constexpr TextArea WEATHER_TITLE = {32, 100, 20, 2};
constexpr TextArea WEATHER_LOCATION = {56, 160, 20, 2};
constexpr TextArea WEATHER_TEMPERATURE = {86, 196, 30, 4};
constexpr TextArea WEATHER_CONDITION = {117, 210, 20, 2};
constexpr TextArea WEATHER_HUMIDITY = {143, 196, 20, 2};
constexpr TextArea WEATHER_WIND = {168, 176, 20, 2};
constexpr TextArea WEATHER_STATUS = {191, 152, 14, 1};
constexpr TextArea WEATHER_SOURCE = {209, 100, 12, 1};
}
