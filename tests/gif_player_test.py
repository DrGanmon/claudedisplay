"""Run the actual GIF decoder/player against a fake TFT and flash filesystem."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
lib = root / 'lib/AnimatedGIF/src'
if not (lib / 'AnimatedGIF.cpp').exists():
    raise SystemExit('Project-local GIF decoder missing')
with tempfile.TemporaryDirectory(prefix='gif-player-') as directory:
    d = Path(directory)
    (d / 'clock.h').write_text('''#pragma once
#include <stdint.h>
extern uint32_t fakeTime;
inline uint32_t millis() { return fakeTime; }
inline void delay(int) {}
inline void yield() {}
''')
    (d / 'LittleFS.h').write_text('''#pragma once
#include <fstream>
#include <memory>
#include <stdint.h>
class File {
 std::shared_ptr<std::ifstream> stream;
 public:
 File() {}
 File(const char *path):stream(new std::ifstream(path,std::ios::binary)) {}
 operator bool() const { return stream && stream->is_open(); }
 void close() { if(stream)stream->close(); }
 size_t size() { auto p=stream->tellg();stream->seekg(0,std::ios::end);auto n=stream->tellg();stream->seekg(p);return n; }
 size_t position() { return stream->tellg(); }
 bool seek(size_t p) { stream->clear();stream->seekg(p);return bool(*stream); }
 int read() { return stream->get(); }
 int read(uint8_t *buffer,int n) { if(n<=0)return 0;stream->read(reinterpret_cast<char*>(buffer),n);return stream->gcount(); }
};
struct FS { File open(const char *path,const char*) { return File(path); } };
extern FS LittleFS;
''')
    (d / 'TFT_eSPI.h').write_text('''#pragma once
#include <stdint.h>
#include <cassert>
struct TFT_eSPI {
 uint16_t pixels[240*240]={}; bool swap=false; int windows=0,fills=0,writes=0,wx=0,wy=0,ww=0,offset=0;
 void startWrite(){++writes;assert(writes==1);}
 void endWrite(){--writes;assert(writes==0);}
 void setAddrWindow(int x,int y,int w,int h){assert(writes);assert(h>0);wx=x;wy=y;ww=w;offset=0;++windows;}
 void pushPixels(const void *data,uint32_t n){assert(writes&&swap);const uint16_t*p=static_cast<const uint16_t*>(data);
  for(uint32_t i=0;i<n;++i){int x=wx+offset%ww,y=wy+offset/ww;assert(x<240&&y<240);pixels[y*240+x]=p[i];++offset;}
  fakeTime+=10; // Simulated rendering cost must count towards the frame delay.
 }
 bool getSwapBytes() const { return swap; }
 void setSwapBytes(bool v) { swap=v; }
 void fillRect(int x,int y,int w,int h,uint16_t c) {
  ++fills;assert(x>=0&&y>=0&&x+w<=240&&y+h<=240);
  for(int j=y;j<y+h;++j)for(int i=x;i<x+w;++i)pixels[j*240+i]=c;
 }
 void pushImage(int x,int y,int w,int h,uint16_t *p) {
  assert(writes&&swap);assert(h==1);for(int i=0;i<w;++i)pixels[y*240+x+i]=p[i];
 }
};
''')
    # Two 1-pixel subframes in a 2x1 canvas. First white pixel is disposed to
    # black before the second white pixel appears, then the animation loops.
    header = b'GIF89a\x02\x00\x01\x00\x80\x00\x00\x00\x00\x00\xff\xff\xff'
    def frame(x, disposal, value=1, transparent=False):
        return (bytes([0x21,0xf9,4,disposal*4+int(transparent),10,0,0,0,0x2c,x,0,0,0,1,0,1,0,0,2,2,
                       0x44+(value*8),1,0]))
    (d / 'animation.gif').write_bytes(header+frame(0,2)+frame(1,1)+b';')
    # Disposal 1 + transparency preserves the existing white pixel.
    # Repeated clear codes keep this 2x2 test stream at three bits per code.
    codes=[4,1,4,1,4,1,4,1,5]
    bits=sum(code << (3*i) for i,code in enumerate(codes))
    opaque_header=bytearray(header);opaque_header[8]=2
    (d/'opaque.gif').write_bytes(bytes(opaque_header)+bytes([0x21,0xf9,4,0,10,0,0,0,
        0x2c,0,0,0,0,2,0,2,0,0,2,4])+bits.to_bytes(4,'little')+bytes([0,0x3b]))
    (d / 'transparent.gif').write_bytes(header+frame(0,1)+frame(0,1,0,True)+b';')
    (d / 'test.cpp').write_text('''#include "gif_player.h"
#include <cassert>
#include <iostream>
uint32_t fakeTime=0;
FS LittleFS;
int main(int argc,char **argv) {
 assert(argc>=3);
 // External dictionary is mandatory; absent storage must fail safely.
 AnimatedGIF missing; missing.begin();
 uint8_t data[1]={0};
 assert(!missing.open(data,1,[](GIFDRAW*){}));
 assert(missing.getLastError()==GIF_ERROR_MEMORY);
 assert(sizeof(AnimatedGIF)<16384); // main block fits the reported 19136-byte IRAM heap
 TFT_eSPI display;
 GifPlayer player(display);
 assert(player.verify(argv[1]));
 assert(display.pixels[119*240+119]==0); // validation never draws
 assert(player.start(argv[1]));
 player.tick(0); assert(!player.error());
 assert(display.pixels[119*240+119]==65535);
 assert(display.pixels[119*240+120]==0);
 fakeTime=99; player.tick(fakeTime);
 assert(display.pixels[119*240+119]==65535);
 fakeTime=100; player.tick(fakeTime); assert(!player.error());
 assert(display.pixels[119*240+119]==0);
 assert(display.pixels[119*240+120]==65535); // last frame is held, not erased
 fakeTime=200; player.tick(fakeTime); assert(!player.error());
 assert(display.pixels[119*240+119]==65535);
 assert(display.pixels[119*240+120]==0);
 player.release(); fakeTime=300; player.tick(fakeTime);
 assert(display.pixels[119*240+119]==65535);
 assert(!display.swap);assert(display.writes==0);
 assert(player.start(argv[2]));
 player.tick(fakeTime); fakeTime+=100;player.tick(fakeTime);
 assert(!player.error());assert(display.pixels[119*240+119]==65535);
 player.release();
 assert(!player.start("/nonexistent/gif"));assert(player.error());assert(!player.error());
 if(argc>4) { assert(player.verify(argv[4])); assert(player.start(argv[4]));
  for(int i=0;i<100;++i) {fakeTime+=100;player.tick(fakeTime);assert(!player.error());}
  player.release(); }
 if(argc>3) {
  assert(player.start(argv[3]));
  int windows=display.windows,fills=display.fills;
  uint32_t started=fakeTime;
  player.tick(fakeTime);assert(!player.error());
  assert(display.windows==windows+1); // both rows use one window
  assert(display.fills==fills); // no black erase before opaque frames
  fakeTime=started+99;player.tick(fakeTime);assert(display.windows==windows+1);
  fakeTime=started+100;player.tick(fakeTime);assert(display.windows==windows+2);
  assert(display.fills==fills);assert(display.writes==0);
  player.release();
 }
 std::cout<<"GIF playback: timing, disposal, transparency, loop, close and RGB565 OK; host decoder "<<sizeof(AnimatedGIF)<<" + "<<GIF_DICTIONARY_BYTES<<" bytes\\n";
}
''')
    binary=d/'test'
    subprocess.run(['g++','-DPICO_BUILD','-std=c++11','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-function','-Wno-shift-negative-value',
                    '-include',str(d/'clock.h'),'-I',str(d),'-I',str(root/'src'),'-I',str(lib),
                    str(d/'test.cpp'),str(lib/'AnimatedGIF.cpp'),'-o',str(binary)],check=True,
                   stdout=subprocess.PIPE)
    args=[str(binary),str(d/'animation.gif'),str(d/'transparent.gif'),str(d/'opaque.gif')]
    extra=root/'assets/snorlax-pixel-96x96.gif'
    if extra.exists(): args.append(str(extra))
    subprocess.run(args,check=True)
