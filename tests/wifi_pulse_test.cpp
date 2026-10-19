#include "../src/wifi_pulse.h"
#include <cassert>
int main() {
  for (int r = 94; r <= 118; r += 2) {
    assert(WifiPulse::color(0, r) == WifiPulse::color(18000, r));
    assert(WifiPulse::color(0, r) != WifiPulse::color(1800, r));
  }
  assert(WifiPulse::color(1800, 106) != WifiPulse::color(7800, 106));
  assert(WifiPulse::color(1800, 106) != WifiPulse::color(13800, 106));
  // Exercise the complete cycle and timer rollover without dynamic buffers.
  for (uint32_t t = 0; t < 18000; t += 50) WifiPulse::color(t, 106);
  WifiPulse::color(0xffffffffUL, 106);
}
