#include "../src/gif_validation.h"
#include <cassert>
#include <vector>
struct Reader {
  std::vector<unsigned char> data; size_t pos=0;
  size_t size() const { return data.size(); }
  size_t position() const { return pos; }
  bool seek(size_t n) { if(n>data.size()) return false; pos=n; return true; }
  int read() { return pos<data.size()?data[pos++]:-1; }
};
int main() {
  Reader good; good.data={'G','I','F','8','9','a',1,0,1,0,0x80,0,0,
     0,0,0,255,255,255,0x21,0xf9,4,0,10,0,0,0,
     0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3b};
  assert(GifLimits::validate(good));
  for(size_t n=0;n<good.data.size();++n) {
    Reader truncated=good; truncated.data.resize(n);
    assert(!GifLimits::validate(truncated));
  }
  auto bad=good; bad.data[6]=97; assert(!GifLimits::validate(bad));
  bad=good; bad.data[8]=0; assert(!GifLimits::validate(bad));
  bad=good; bad.data[22]=12; assert(!GifLimits::validate(bad)); // disposal 3
  bad=good; bad.data[31]=2; assert(!GifLimits::validate(bad)); // frame outside canvas
  bad=good; bad.data[0]='J'; assert(!GifLimits::validate(bad));
  bad=good; bad.data[38]=255; assert(!GifLimits::validate(bad)); // truncated subblock
  bad=good; bad.data.push_back(0); assert(!GifLimits::validate(bad));
  bad=good; bad.data.resize(GifLimits::BYTES+1); assert(!GifLimits::validate(bad));
  // Two frames and the usual application extension remain supported.
  auto animated=good; animated.data.pop_back();
  animated.data.insert(animated.data.end(),good.data.begin()+19,good.data.end());
  assert(GifLimits::validate(animated));
}
