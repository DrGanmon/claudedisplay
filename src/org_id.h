#pragma once

#include <stddef.h>

// Wie der UUID-RegEx im PS1: erster Treffer, auch in "Name [UUID]".
inline size_t findOrganizationUuid(const char *text, size_t length) {
  for (size_t start = 0; start + 36 <= length; ++start) {
    bool valid = true;
    for (size_t i = 0; i < 36; ++i) {
      const char c = text[start + i];
      const bool dash = i == 8 || i == 13 || i == 18 || i == 23;
      const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                       (c >= 'A' && c <= 'F');
      if (dash ? c != '-' : !hex) {
        valid = false;
        break;
      }
    }
    if (valid) return start;
  }
  return length;
}
