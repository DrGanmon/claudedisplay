#include "../src/tls_memory.h"
#include <cassert>
int main() {
  assert(hasClaudeTlsMemory(28000, 18000));
  assert(hasClaudeTlsMemory(40000, 30000));
  assert(!hasClaudeTlsMemory(27999, 20000));
  // Enough total space is insufficient when the TLS record cannot fit.
  assert(!hasClaudeTlsMemory(40000, 17999));
  assert(!hasClaudeTlsMemory(0, 0));
}
