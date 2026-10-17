#include "../src/wifi_recovery.h"
#include <assert.h>
#include <memory>

struct Manager {
  bool active = false;
  std::unique_ptr<int> server;
  int processed = 0, stopped = 0;
  bool getConfigPortalActive() { return active; }
  void process() { assert(server && active); ++processed; }
  void stopConfigPortal() {
    assert(server && active);
    ++stopped;
    server.reset();
    active = false;
  }
};

int main() {
  // Erfolgreicher autoConnect: kein Portal und kein Portal-Webserver.
  Manager manager;
  processWifiPortalIfRunning(manager);
  stopWifiPortalIfRunning(manager);
  assert(manager.processed == 0 && manager.stopped == 0);
  // Auch ein inkonsistenter Zustand darf keinen Nullzeiger dereferenzieren.
  manager.active = true;
  processWifiPortalIfRunning(manager);
  stopWifiPortalIfRunning(manager);
  assert(manager.processed == 0 && manager.stopped == 0);
  // Wirklich gestartetes Portal bedienen und genau einmal beenden.
  manager.server.reset(new int(1));
  processWifiPortalIfRunning(manager);
  stopWifiPortalIfRunning(manager);
  stopWifiPortalIfRunning(manager);
  processWifiPortalIfRunning(manager);
  assert(manager.processed == 1 && manager.stopped == 1);
  WifiRecovery recovery;
  assert(!recovery.needsPortal(true, 0));
  assert(!recovery.needsPortal(false, 0));
  assert(!recovery.needsPortal(false, 59999));
  assert(recovery.needsPortal(false, 60000));
  assert(!recovery.needsPortal(true, 60001));
  assert(!recovery.needsPortal(false, 60002));
  assert(!recovery.needsPortal(false, 120001));
  assert(recovery.needsPortal(false, 120002));
  assert(!recovery.needsPortal(true, 0xFFFF0000));
  assert(!recovery.needsPortal(false, 0xFFFFFF00));
  assert(!recovery.needsPortal(false, static_cast<uint32_t>(0xFFFFFF00U + 59999U)));
  assert(recovery.needsPortal(false, static_cast<uint32_t>(0xFFFFFF00U + 60000U)));
}
