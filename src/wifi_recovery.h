#pragma once
#include <stdint.h>

class WifiRecovery {
 public:
  bool needsPortal(bool connected, uint32_t now) {
    if (connected) { disconnected = false; return false; }
    if (!disconnected) { disconnected = true; since = now; }
    return static_cast<uint32_t>(now - since) >= 60000;
  }
 private:
  bool disconnected = false;
  uint32_t since = 0;
};
