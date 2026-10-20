#pragma once
#include <stddef.h>
#include <stdint.h>

// Playback/upload validation run without TLS. Keep 4 KiB for the webserver,
// flash file handle and transient allocations after placing decoder and dictionary in DRAM.
constexpr size_t GIF_DICTIONARY_BYTES = 4096U * sizeof(uint16_t);
inline bool hasGifDramMemory(uint32_t freeBytes, uint32_t largestBlock, size_t decoderBytes, bool dictionaryInDram = true) {
  return freeBytes >= decoderBytes + (dictionaryInDram ? GIF_DICTIONARY_BYTES : 0) + 4096 && largestBlock >= decoderBytes + 16;
}
