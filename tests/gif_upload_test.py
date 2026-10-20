"""Exercise the actual multipart handlers: auth, limits, atomic replacement, abort."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'src/main.cpp').read_text()
def function(signature):
    start=source.index(signature)
    return source[start:source.index('\n}',start)+2]
harness=r'''
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cassert>
#include <cstring>
#include "gif_validation.h"
#define F(x) x
struct String:std::string {using std::string::string;String(int v):std::string(std::to_string(v)){} };
#define PSTR(x) x
#define snprintf_P snprintf
std::map<std::string,std::shared_ptr<std::vector<uint8_t>>> files;
struct File {
 std::shared_ptr<std::vector<uint8_t>> data; size_t pos=0;
 operator bool()const{return bool(data);}
 size_t size() const {return data?data->size():0;}
 size_t position() const {return pos;}
 bool seek(size_t n){if(n>size())return false;pos=n;return true;}
 int read(){return data&&pos<size()?(*data)[pos++]:-1;}
 size_t write(const uint8_t*b,size_t n){if(!data)return 0;data->insert(data->end(),b,b+n);return n;}
 void close(){data.reset();}
 void flush(){}
 bool getWriteError(){return false;}
};
struct {
 bool renameFails=false;
 File open(const char*p,const char*m){
  if(*m=='w')files[p]=std::make_shared<std::vector<uint8_t>>();
  File f; if(files.count(p))f.data=files[p];return f;
 }
 bool remove(const char*p){return files.erase(p);}
 bool rename(const char*a,const char*b){if(renameFails||!files.count(a))return false;files[b]=files[a];files.erase(a);return true;}
} LittleFS;
enum {UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload{int status;size_t currentSize=0;const uint8_t*buf=nullptr;};
struct {bool authorized=true;HTTPUpload item;HTTPUpload &upload(){return item;}
 bool authenticate(const char*,const char*){return authorized;}}web;
struct {String webPassword="dummy";}config;
struct {bool decoding=true;void release(){}bool verify(const char*){return decoding;}int lastError(){return -1;}}gifPlayer;
struct {void release(){}}weatherAnimation;
struct {bool visible=true;bool gif(){return visible;}}pages;
void releaseDisplayScratch(){}
void yield(){}
bool displayNeedsFullRedraw=false;
const char *GIF_PATH="/display.gif",*GIF_TEMP="/display.gif.tmp";
File gifUploadFile;
bool gifUploadActive=false,gifUploadAuthorized=false,gifUploadOk=false;
uint32_t gifUploadBytes=0;
String gifUploadError,result;
bool requireSettingsAuth(){return web.authorized;}
void settingsResult(const String&s){result=s;}
void updateGifAvailability(){}
'''
harness+='\n'+function('void handleGifUpload(')+'\n'+function('void handleGifFinished(')
harness+=r'''
void event(int status,const std::vector<uint8_t>&data={}) {
 web.item.status=status;web.item.currentSize=data.size();web.item.buf=data.data();handleGifUpload();
}
int main(){
 const std::vector<uint8_t> good={'G','I','F','8','9','a',1,0,1,0,0x80,0,0,
  0,0,0,255,255,255,0x21,0xf9,4,0,10,0,0,0,0x2c,0,0,0,0,1,0,1,0,0,2,2,0x44,1,0,0x3b};
 const std::vector<uint8_t> old={1,2,3};
 files[GIF_PATH]=std::make_shared<std::vector<uint8_t>>(old);
 web.authorized=false;event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,good);event(UPLOAD_FILE_END);handleGifFinished();
 assert(!files.count(GIF_TEMP)&&*files[GIF_PATH]==old);
 web.authorized=true;
 event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,{0,1,2});event(UPLOAD_FILE_END);handleGifFinished();
 assert(*files[GIF_PATH]==old && !files.count(GIF_TEMP));
 event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,good);event(UPLOAD_FILE_ABORTED);
 assert(*files[GIF_PATH]==old && !files.count(GIF_TEMP)&&!gifUploadActive&&displayNeedsFullRedraw);
 event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,std::vector<uint8_t>(GifLimits::BYTES+1));event(UPLOAD_FILE_END);handleGifFinished();
 assert(*files[GIF_PATH]==old && !files.count(GIF_TEMP));
 gifPlayer.decoding=false;
 event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,good);event(UPLOAD_FILE_END);handleGifFinished();
 assert(*files[GIF_PATH]==old);
 gifPlayer.decoding=true;LittleFS.renameFails=true;
 event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,good);event(UPLOAD_FILE_END);handleGifFinished();
 assert(*files[GIF_PATH]==old && !files.count(GIF_TEMP));
 LittleFS.renameFails=false;
 event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,good);event(UPLOAD_FILE_END);handleGifFinished();
 assert(*files[GIF_PATH]==good && !files.count(GIF_TEMP)&&!gifUploadActive);
 assert(result.find("GIF gespeichert.")==0);
 // Multiple file parts cannot replace the old GIF with an unchecked second file.
 event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,good);event(UPLOAD_FILE_END);
 event(UPLOAD_FILE_START);event(UPLOAD_FILE_WRITE,{0,1,2});event(UPLOAD_FILE_END);handleGifFinished();
 assert(*files[GIF_PATH]==good && result.find("GIF gespeichert.")!=0);
}
'''
with tempfile.TemporaryDirectory(prefix='gif-upload-') as directory:
    d=Path(directory);cpp=d/'test.cpp';binary=d/'test';cpp.write_text(harness)
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-I',str(root/'src'),str(cpp),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('GIF upload: authentication, malformed/oversized files, decode failure, abort and atomic replacement OK')
