#pragma once
#include <stdint.h>

// Checked after creating the secure client and HTTP headers, before GET.
// BearSSL still needs its 16 KiB record buffer, engine and validator.
// Keep a conservative additional reserve for lwIP and the WiFi SDK.
inline bool hasClaudeTlsMemory(uint32_t freeHeap, uint32_t largestBlock) {
  return freeHeap >= 28000 && largestBlock >= 18000;
}
