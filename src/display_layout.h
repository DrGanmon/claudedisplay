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
}
