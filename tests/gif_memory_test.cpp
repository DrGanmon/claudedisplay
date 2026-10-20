#include "../src/gif_memory.h"
#include <cassert>
int main() {
  const unsigned decoder=15944;
  assert(hasGifDramMemory(28528,19624,decoder,false)); // fast body in DRAM, dictionary in IRAM
  assert(!hasGifDramMemory(28528,15000,decoder,false));
  assert(hasGifDramMemory(28528,19624,decoder)); // user's fragmented DRAM can fit fallback
  assert(!hasGifDramMemory(28000,15000,decoder)); // neither block can fit the body
  assert(!hasGifDramMemory(28000,19624,decoder)); // preserve 4 KiB after both allocations
  assert(hasGifDramMemory(decoder+GIF_DICTIONARY_BYTES+4096,decoder+16,decoder));
  assert(!hasGifDramMemory(decoder+GIF_DICTIONARY_BYTES+4095,decoder+16,decoder));
}
