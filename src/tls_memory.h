#pragma once
#include <stdint.h>

// Checked after constructing the secure client and HTTP headers, before GET.
// BearSSL prefers IRAM for I/O: receive = 16384 + 325 bytes plus allocator
// overhead, transmit = 512 + 85. Engine/validator and WiFi still need DRAM.
inline bool hasClaudeTlsMemory(uint32_t freeHeap, uint32_t largestBlock,
                               uint32_t iramFree = 0, uint32_t iramLargest = 0) {
  const bool dramBuffers = freeHeap >= 28000 && largestBlock >= 18000;
  const bool iramBuffers = iramFree >= 18000 && iramLargest >= 17000 &&
                          freeHeap >= 18000 && largestBlock >= 6000;
  return dramBuffers || iramBuffers;
}
