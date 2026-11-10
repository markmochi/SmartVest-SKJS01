#pragma once
#include <FS.h>
#include <SD_MMC.h>
#include <stdint.h>
#include <string.h>
#include "CameraCrypto.h"
#include "MediaBuffer.h"
#include "AviFormat.h"
// Classic RIFF AVI, MJPG video, no audio. A bounded 16 KiB RAM index avoids
// per-clip .idx files (1024 frames; normal clips contain about 480 frames).
// Unfinished footage stays in the system folder until successfully finalized.
class AviWriter {
 public:
  bool begin(const String& stem, uint32_t width, uint32_t height) {
    if(avi || SD_MMC.exists(stem+".part")||!index.reserve(MAX_FRAMES*16))return false;
    entries=reinterpret_cast<uint8_t(*)[16]>(index.data());
    base = stem; frames = 0; peak = 0; good = digest.begin();hashing=false;streamLeft=0;finalDigest[0]=0;finalBytes=0;
    avi = SD_MMC.open(base + ".part", FILE_WRITE);
    if (!avi) return good = false;
    uint8_t header[224];AviFormat::header(header,width,height);write(header,sizeof(header));
    // Header length is 224; movi FOURCC is at offset 220.
    return good && avi.position() == 224;
  }
  bool startFrame(size_t size) {
    if(!good||!avi||streamLeft||frames>=MAX_FRAMES||size>1024*1024||size<4)return good=false;
    frameSize=size;streamLeft=size;prefix=0;tail=0;frameOffset=avi.position()-220;
    hashing=true;four("00dc");u32(size);return good;
  }
  bool appendFrame(const uint8_t* bytes,size_t size){
    if(!good||!bytes||!streamLeft||size>streamLeft)return good=false;
    const size_t already=frameSize-streamLeft;
    for(size_t i=0;i<size&&already+i<2;++i)prefix=uint16_t((prefix<<8)|bytes[i]);
    if(size>=2)tail=uint16_t(bytes[size-2]<<8|bytes[size-1]);else if(size)tail=uint16_t((tail<<8)|bytes[0]);
    write(bytes,size);streamLeft-=size;return good;
  }
  bool endFrame(){
    if(!good||streamLeft||!hashing||prefix!=0xffd8||tail!=0xffd9)return good=false;
    if(frameSize&1){const uint8_t zero=0;write(&zero,1);}hashing=false;
    uint8_t entry[16]={'0','0','d','c',0x10,0,0,0};put(entry+8,frameOffset);put(entry+12,frameSize);
    memcpy(entries[frames++],entry,16);if(frameSize>peak)peak=frameSize;return good;
  }
  bool frame(const uint8_t* jpeg,size_t size){return startFrame(size)&&appendFrame(jpeg,size)&&endFrame();}
  bool checkpoint(char* output){if(!good||streamLeft||hashing)return false;avi.flush();return digest.peek(output);}
  const char* payloadDigest()const{return finalDigest;}
  uint64_t finalizedSize()const{return finalBytes;}
  bool close(uint32_t durationMs, const String& destination = String()) {
    if (!avi) return false;
    const uint32_t moviEnd = avi.position();
    if(good && !streamLeft && !hashing && frames && durationMs) {
      four("idx1"); u32(frames*16);
      write(&entries[0][0],frames*16);
      const uint32_t end=avi.position();finalBytes=end;
      patch(4,end-8); patch(32,uint32_t(uint64_t(durationMs)*1000/frames));
      patch(48,frames); patch(60,peak);
      patch(128,durationMs); patch(132,frames*1000);
      patch(140,frames); patch(144,peak); patch(216,moviEnd-220);
    } else good=false;
    if(good&&!digest.finish(finalDigest))good=false;
    avi.flush(); avi.close();
    if(good) {
      File check=SD_MMC.open(base+".part",FILE_READ);
      good=check && check.size()>=224+frames*24ULL;
      check.close();
    }
    const String target=destination.length()?destination:base+".avi";
    if(good) good=!SD_MMC.exists(target) && SD_MMC.rename(base+".part",target);
    return good;
  }
  // Release handles without finalizing/renaming a broken recording. Leave
  // .part on disk for inspection; never claim incomplete footage is AVI.
  void abort() {good=false;avi.close();}
  uint32_t count() const {return frames;}
  uint64_t size() {return avi ? avi.position() : 0;}
 private:
  static constexpr uint32_t MAX_FRAMES=1024;
  MediaBuffer index;uint8_t (*entries)[16]=nullptr;
  CameraCrypto::Hash digest;char finalDigest[65]{};uint64_t finalBytes=0;
  size_t streamLeft=0,frameSize=0;uint32_t frameOffset=0;uint16_t prefix=0,tail=0;bool hashing=false;
  File avi; String base; uint32_t frames=0,peak=0; bool good=false;
  static void put(uint8_t* p,uint32_t v) {for(int i=0;i<4;++i)p[i]=v>>(8*i);}
  void write(const uint8_t* p,size_t n) {if(avi.write(p,n)!=n)good=false;if(hashing&&!digest.add(p,n))good=false;}
  void four(const char* p) {write(reinterpret_cast<const uint8_t*>(p),4);}
  void u32(uint32_t v) {uint8_t p[4];put(p,v);write(p,4);}
  void u16(uint16_t v) {uint8_t p[2]={uint8_t(v),uint8_t(v>>8)};write(p,2);}
  void patch(uint32_t offset,uint32_t v) {if(!avi.seek(offset))good=false;u32(v);}
};
