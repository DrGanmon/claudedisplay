#include "../src/display_layout.h"
#include <assert.h>
#include <stdlib.h>

int main() {
  using namespace DisplayLayout;
  const TextArea areas[] = {NAME, TEMPERATURE, SESSION, WEEKLY, TRUECONF, NETWORK};
  // Ring-Innenkante samt Glättung beginnt bei Radius 90. Alle Sprite-Ecken
  // müssen innerhalb Radius 88 liegen, nicht nur die Schriftmittelpunkte.
  for (const TextArea &area : areas) {
    const int x = area.width / 2;
    const int y = abs(area.y - 120) + area.height / 2;
    assert(x * x + y * y < 88 * 88);
  }
  for (unsigned i = 1; i < sizeof(areas) / sizeof(areas[0]); ++i)
    assert(areas[i - 1].y + areas[i - 1].height / 2 < areas[i].y - areas[i].height / 2);
}
