#pragma once
#include <stdint.h>
#include <stddef.h>

namespace GifLimits {
constexpr uint16_t SIDE = 96;
constexpr uint32_t BYTES = 128UL * 1024;
// Structural streaming validation: no image/file-sized allocation. Disposal 3
// requires a saved canvas and is deliberately unsupported by the raw renderer.
template<class Reader> bool validate(Reader &file, void (*service)() = nullptr) {
  const size_t size = file.size();
  if (size < 14 || size > BYTES || !file.seek(0)) return false;
  unsigned reads=0;
  auto readByte = [&]() -> int {
    if(service && (++reads & 63U)==0) service();
    return file.read();
  };
  auto readWord = [&]() -> int { int a = readByte(), b = readByte(); return a < 0 || b < 0 ? -1 : a | (b << 8); };
  auto skip = [&](size_t n) -> bool {
    size_t pos = file.position();
    return pos <= size && n <= size - pos && file.seek(pos + n);
  };
  auto blocks = [&]() -> bool {
    for (;;) { int n = readByte(); if (n < 0) return false; if (!n) return true; if (!skip(n)) return false; }
  };
  char header[6];
  for (char &c : header) { int v = readByte(); if (v < 0) return false; c = v; }
  if (header[0]!='G'||header[1]!='I'||header[2]!='F'||header[3]!='8'||
      (header[4]!='7'&&header[4]!='9')||header[5]!='a') return false;
  const int width=readWord(), height=readWord(), flags=readByte();
  if (width<1||height<1||width>SIDE||height>SIDE||flags<0||!skip(2)) return false;
  if ((flags&128) && !skip(3U << ((flags&7)+1))) return false;
  unsigned frames=0;
  for (;;) {
    int type=readByte();
    if (type==0x3b) return frames>0 && file.position()==size;
    if (type==0x21) {
      int label=readByte(); if (label<0) return false;
      if (label==0xf9) {
        if (readByte()!=4) return false;
        int control=readByte();
        if (control<0 || ((control>>2)&7)>2 || !skip(3) || readByte()!=0) return false;
      } else if (!blocks()) return false;
    } else if (type==0x2c) {
      int x=readWord(),y=readWord(),w=readWord(),h=readWord(),packed=readByte();
      if(x<0||y<0||w<1||h<1||x+w>width||y+h>height||packed<0) return false;
      if ((packed&128) && !skip(3U << ((packed&7)+1))) return false;
      int code=readByte(); if(code<2||code>8||!blocks()) return false;
      if(++frames>4096) return false;
    } else return false;
  }
}
} // namespace GifLimits
