#pragma once
#include <FS.h>
#include <SD_MMC.h>
#include "CameraCrypto.h"
#include "MediaBuffer.h"
#include "AviFormat.h"
#include <cstring>
#include <algorithm>
class AviVerifier {
 public:
  bool begin(const String& path,uint32_t frames,uint32_t duration,uint64_t bytes,const char* expected,bool legacy=false){
    if(!workspace.reserve(1024*16+8192))return fail();entries=workspace.data();buffer=entries+1024*16;
    file.close();finished=false;valid=false;count=0;left=0;peak=0;offset=224;wantedFrames=frames;
    expectedHash[0]=0;if(expected&&CameraCrypto::validHex(expected,64))std::memcpy(expectedHash,expected,65);else if(!legacy)return fail();
    file=SD_MMC.open(path,FILE_READ);if(!file||!frames||frames>1024||!duration)return fail();
    size=file.size();if((bytes&&bytes!=size)||size>128ULL*1024*1024||size<224+frames*24ULL)return fail();
    if(file.read(header,sizeof(header))!=sizeof(header))return fail();
    if(std::memcmp(header,"RIFF",4)||std::memcmp(header+8,"AVI ",4)||get(header+4)+8ULL!=size||
       std::memcmp(header+12,"LIST",4)||get(header+16)!=192||std::memcmp(header+20,"hdrlavih",8)||get(header+28)!=56||
       get(header+48)!=frames||get(header+56)!=1||get(header+64)!=640||get(header+68)!=480||
       std::memcmp(header+88,"LIST",4)||std::memcmp(header+96,"strlstrh",8)||std::memcmp(header+108,"vidsMJPG",8)||
       get(header+128)!=duration||get(header+132)!=frames*1000||get(header+140)!=frames||
       get(header+32)!=uint32_t(uint64_t(duration)*1000/frames)||
       std::memcmp(header+212,"LIST",4)||std::memcmp(header+220,"movi",4))return fail();
    moviEnd=220ULL+get(header+216);
    if(moviEnd<224||moviEnd+8+frames*16ULL!=size||!file.seek(moviEnd))return fail();
    AviFormat::header(buffer,640,480,frames,duration,get(header+60),uint32_t(moviEnd));
    if(std::memcmp(header,buffer,sizeof(header)))return fail();
    uint8_t index[8];if(file.read(index,8)!=8||std::memcmp(index,"idx1",4)||get(index+4)!=frames*16||file.read(entries,frames*16)!=frames*16||!file.seek(224)||!hash.begin())return fail();
    return true;
  }
  void step(){
    if(finished)return;
    if(!left){
      if(count==wantedFrames){
        if(offset!=moviEnd||get(header+60)!=peak||get(header+144)!=peak||!hash.finish(actualHash)||
           (expectedHash[0]&&!CameraCrypto::equal(expectedHash,actualHash,64))){fail();return;}
        valid=true;finished=true;file.close();return;
      }
      uint8_t chunk[8];if(offset+8>moviEnd||file.read(chunk,8)!=8||std::memcmp(chunk,"00dc",4)){fail();return;}
      length=get(chunk+4);const uint8_t* entry=entries+count*16;
      if(length<4||length>1024*1024||offset+8+length+(length&1)>moviEnd||
         std::memcmp(entry,"00dc",4)||get(entry+4)!=0x10||get(entry+8)!=offset-220||get(entry+12)!=length||!hash.add(chunk,8)){fail();return;}
      offset+=8;left=length;first=last=0;consumed=0;peak=std::max(peak,length);
    }
    const size_t n=std::min(size_t(left),size_t(8192));
    if(file.read(buffer,n)!=n||!hash.add(buffer,n)){fail();return;}
    if(consumed<2)for(size_t i=0;i<n&&consumed+i<2;++i)first=uint16_t((first<<8)|buffer[i]);
    if(n>=2)last=uint16_t(buffer[n-2]<<8|buffer[n-1]);else if(n)last=uint16_t((last<<8)|buffer[0]);
    consumed+=n;left-=n;offset+=n;
    if(!left){
      if(first!=0xffd8||last!=0xffd9){fail();return;}
      if(length&1){uint8_t pad;if(file.read(&pad,1)!=1||pad!=0||!hash.add(&pad,1)){fail();return;}++offset;}
      ++count;
    }
  }
  bool done()const{return finished;}
  bool good()const{return finished&&valid;}
  const char* digest()const{return actualHash;}
  void cancel(){file.close();finished=true;valid=false;}
 private:
  File file;CameraCrypto::Hash hash;uint8_t header[224]{};MediaBuffer workspace;uint8_t *entries=nullptr,*buffer=nullptr;
  char expectedHash[65]{},actualHash[65]{};
  uint64_t size=0,moviEnd=0,offset=0;uint32_t count=0,wantedFrames=0,left=0,length=0,peak=0,consumed=0;
  uint16_t first=0,last=0;bool finished=true,valid=false;
  static uint32_t get(const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
  bool fail(){file.close();finished=true;valid=false;return false;}
};
