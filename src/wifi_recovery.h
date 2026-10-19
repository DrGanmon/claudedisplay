#pragma once
#include <stdint.h>

template <typename Manager>
void processWifiPortalIfRunning(Manager &manager) {
  if (manager.getConfigPortalActive() && manager.server) manager.process();
}

template <typename Manager>
void stopWifiPortalIfRunning(Manager &manager) {
  // WiFiManager 2.0.17 greift beim Stoppen auch ohne gestartetes Portal auf
  // server zu. Ein erfolgreicher autoConnect() hat keinen Server angelegt.
  if (manager.getConfigPortalActive() && manager.server) manager.stopConfigPortal();
}

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

// The initial connection attempt must not block the display's animation loop.
class WifiStartup {
 public:
  void begin(uint32_t now) { waiting = true; since = now; }
  void finish() { waiting = false; }
  bool active() const { return waiting; }
  bool needsPortal(uint32_t now) const {
    return waiting && static_cast<uint32_t>(now - since) >= 20000;
  }
 private:
  bool waiting = false;
  uint32_t since = 0;
};
