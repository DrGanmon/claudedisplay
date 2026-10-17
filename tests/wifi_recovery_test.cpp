#include "../src/wifi_recovery.h"
#include <assert.h>

int main() {
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
