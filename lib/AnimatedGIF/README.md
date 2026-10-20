# Project-local AnimatedGIF 2.2.0

Source: https://github.com/bitbank2/AnimatedGIF/releases/tag/2.2.0
Copyright Larry Bank; Apache-2.0, see LICENSE and source headers.

Local changes: the 8192-byte `usGIFTable` array is an externally owned pointer.
Call `begin()`, then `setDictionary()` with 4096 uint16_t entries before opening
an image. Keep this buffer alive until the decoder is closed. The initialization
checks the pointer; table reset uses the explicit entry count instead of sizeof
the pointer. All other decoder logic is unchanged. C initialization entry points
that clear GIFIMAGE are not used by this firmware; use the C++ API above.

GifPlayer prefers the roughly 16 KiB byte-heavy decoder in DRAM and its 8 KiB
word dictionary in IRAM for speed. The opposite placement remains available
when the DRAM block is too small. Both are freed together on leaving the GIF page or after validation.
No reduced LZW code size, extra frame buffer or turbo mode is used.
