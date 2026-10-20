#pragma once
#include <AnimatedGIF.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <new>
#if defined(ESP8266) && defined(MMU_IRAM_HEAP)
#include <umm_malloc/umm_heap_select.h>
#endif
#include "gif_validation.h"
#include "gif_memory.h"

// The large LZW decoder exists ONLY while playing; no full canvas or turbo buffer.
class GifPlayer {
 public:
  explicit GifPlayer(TFT_eSPI &target) : screen(target) {}
  ~GifPlayer() { release(); }
  void release() {
    if (decoder) { decoder->close(); delete decoder; decoder=nullptr; }
    delete[] dictionary; dictionary=nullptr;
    file.close(); failed=false; clearPrevious=false; restart=false; needsCanvas=true;
  }
  bool start(const char *path) {
    release(); failureCode=0;
#if defined(ESP8266) && defined(MMU_IRAM_HEAP)
    // Byte-heavy LZW/pixel work is much faster in DRAM; the word dictionary
    // can use IRAM instead. Total storage stays the same as the previous split.
    { HeapSelectDram heap;
      if(hasGifDramMemory(ESP.getFreeHeap(),ESP.getMaxFreeBlockSize(),sizeof(AnimatedGIF),false))
        decoder=new(std::nothrow) AnimatedGIF;
    }
    if(!decoder) { HeapSelectIram heap; decoder=new(std::nothrow) AnimatedGIF; }
#elif defined(ESP8266)
    if(hasGifDramMemory(ESP.getFreeHeap(),ESP.getMaxFreeBlockSize(),sizeof(AnimatedGIF)))
      decoder=new(std::nothrow) AnimatedGIF;
#else
    decoder=new(std::nothrow) AnimatedGIF;
#endif
    if (!decoder) { failureCode=-1; failed=true; return false; }
#if defined(ESP8266) && defined(MMU_IRAM_HEAP)
    { HeapSelectIram heap; dictionary=new(std::nothrow) uint16_t[4096]; }
    if(!dictionary) { HeapSelectDram heap;
      if(ESP.getFreeHeap()>=GIF_DICTIONARY_BYTES+4096 && ESP.getMaxFreeBlockSize()>=GIF_DICTIONARY_BYTES+16)
        dictionary=new(std::nothrow) uint16_t[4096];
    }
#else
    dictionary=new(std::nothrow) uint16_t[4096];
#endif
    if(!dictionary) { delete decoder; decoder=nullptr; failureCode=-1; failed=true; return false; }
    decoder->begin(LITTLE_ENDIAN_PIXELS);
    decoder->setDictionary(dictionary);
    active=this;
    if (!decoder->open(path, openFile, closeFile, readFile, seekFile, drawLine) ||
        decoder->getCanvasWidth()>GifLimits::SIDE || decoder->getCanvasHeight()>GifLimits::SIDE) {
      failureCode=decoder->getLastError();
      release(); failed=true; return false;
    }
    x=(240-decoder->getCanvasWidth())/2; y=(240-decoder->getCanvasHeight())/2;
    due=millis(); return true;
  }
  // Decode every frame before committing an upload, without drawing or delays.
  bool verify(const char *path) {
    if(!start(path)) return false;
    verifying=true;
    uint32_t started=millis();
    bool ok=false; failureCode=-2;
    for(unsigned frames=0;frames<4096 && millis()-started<8000;++frames) {
      int delayMs;
      int more=decoder->playFrame(false,&delayMs,this);
      if(more<0 || (decoder->getLastError()!=GIF_SUCCESS && decoder->getLastError()!=GIF_EMPTY_FRAME)) { failureCode=decoder->getLastError(); break; }
      yield();
      if(!more) { ok=true; failureCode=0; break; }
    }
    verifying=false; release(); return ok;
  }
  // -1: allocation refused/failed; -2: validation budget exceeded; otherwise GIF error code.
  int lastError() const { return failureCode; }
  bool error() { bool value=failed; failed=false; return value; }
  void tick(uint32_t now) {
    if (!decoder || static_cast<int32_t>(now-due)<0) return;
    active=this;
    if(restart) { decoder->reset(); restart=false; clearPrevious=false; needsCanvas=true; }
    firstLine=true; nextRow=-1;
    int delayMs=100;
    const int more=decoder->playFrame(false,&delayMs,this);
    if (more<0 || (decoder->getLastError()!=GIF_SUCCESS && decoder->getLastError()!=GIF_EMPTY_FRAME)) {
      failureCode=decoder->getLastError();
      release(); failed=true; return;
    }
    if (!more) restart=true;
    due=now+static_cast<uint32_t>(delayMs<40?40:delayMs);
    yield();
  }
 private:
  TFT_eSPI &screen;
  AnimatedGIF *decoder=nullptr;
  uint16_t *dictionary=nullptr;
  int failureCode=0;
  File file;
  uint32_t due=0;
  int x=0,y=0,prevX=0,prevY=0,prevW=0,prevH=0,nextRow=-1;
  uint16_t background=0,canvasBackground=0;
  bool failed=false,clearPrevious=false,restart=false,verifying=false,needsCanvas=true,firstLine=true;
  static GifPlayer *active;
  static void *openFile(const char *name,int32_t *size) {
    active->file=LittleFS.open(name,"r");
    if (!active->file) return nullptr;
    *size=active->file.size();
    // Read the global background once; local frame palettes may use index 0
    // for a different color and must not change the disposal background.
    active->canvasBackground=0;
    if(active->file.seek(10)) {
      int flags=active->file.read(), index=active->file.read();
      if((flags&128) && index>=0 && index<(1<<((flags&7)+1)) && active->file.seek(13+index*3)) {
        int r=active->file.read(), g=active->file.read(), b=active->file.read();
        if(r>=0 && g>=0 && b>=0) active->canvasBackground=((r>>3)<<11)|((g>>2)<<5)|(b>>3);
      }
    }
    active->file.seek(0); return &active->file;
  }
  static void closeFile(void *handle) { if(handle) static_cast<File*>(handle)->close(); }
  static int32_t readFile(GIFFILE *info,uint8_t *buffer,int32_t length) {
    File *f=static_cast<File*>(info->fHandle);
    int32_t n=f->read(buffer,length); info->iPos=f->position(); return n;
  }
  static int32_t seekFile(GIFFILE *info,int32_t pos) {
    File *f=static_cast<File*>(info->fHandle);
    if(pos<0 || pos>info->iSize || !f->seek(pos)) return -1;
    info->iPos=f->position(); return info->iPos;
  }
  static void drawLine(GIFDRAW *line) {
    GifPlayer *self=static_cast<GifPlayer*>(line->pUser);
    if(!self || self->verifying || line->iWidth<1 || line->iWidth>GifLimits::SIDE) return;
    if(line->iX<0 || line->iY<0 || line->iX+line->iWidth>self->decoder->getCanvasWidth() ||
       line->iY+line->y>=self->decoder->getCanvasHeight()) return;
    if(self->firstLine) {
      // Full opaque frames overwrite their own background; avoid flashing
      // black and an extra full-area SPI fill before every image.
      const bool opaque=!line->ucHasTransparency;
      if(self->clearPrevious && !(opaque && line->iX<=self->prevX && line->iY<=self->prevY &&
          line->iX+line->iWidth>=self->prevX+self->prevW && line->iY+line->iHeight>=self->prevY+self->prevH))
        self->screen.fillRect(self->x+self->prevX,self->y+self->prevY,self->prevW,self->prevH,self->background);
      if(self->needsCanvas && !(opaque && line->iX==0 && line->iY==0 &&
          line->iWidth==self->decoder->getCanvasWidth() && line->iHeight==self->decoder->getCanvasHeight()))
        self->screen.fillRect(self->x,self->y,self->decoder->getCanvasWidth(),self->decoder->getCanvasHeight(),
                              line->ucHasTransparency?0:self->canvasBackground);
      self->needsCanvas=false; self->firstLine=false;
    }
    uint16_t pixels[GifLimits::SIDE];
    bool transparent=false;
    for(int i=0;i<line->iWidth;++i) {
      const uint8_t value=line->pPixels[i];
      pixels[i]=line->pPalette[value];
      transparent|=line->ucHasTransparency && value==line->ucTransparent;
    }
    const bool swap=self->screen.getSwapBytes(); self->screen.setSwapBytes(true);
    self->screen.startWrite();
    if(!transparent) {
      // Keep the address window for consecutive rows (including safe fallback
      // for interlaced/out-of-order callbacks); push one 192-byte row at most.
      if(self->nextRow!=line->y)
        self->screen.setAddrWindow(self->x+line->iX,self->y+line->iY+line->y,
                                  line->iWidth,line->iHeight-line->y);
      self->screen.pushPixels(pixels,line->iWidth);
      self->nextRow=line->y+1;
    } else {
      self->nextRow=-1;
      for(int i=0;i<line->iWidth;) {
        if(line->pPixels[i]==line->ucTransparent) { ++i; continue; }
        int first=i;
        while(i<line->iWidth && line->pPixels[i]!=line->ucTransparent) ++i;
        self->screen.pushImage(self->x+line->iX+first,self->y+line->iY+line->y,i-first,1,pixels+first);
      }
    }
    self->screen.endWrite();
    self->screen.setSwapBytes(swap);
    self->clearPrevious=line->ucDisposalMethod==2;
    self->prevX=line->iX; self->prevY=line->iY; self->prevW=line->iWidth; self->prevH=line->iHeight;
    self->background=line->ucHasTransparency?0:self->canvasBackground;
  }
};
GifPlayer *GifPlayer::active=nullptr;
