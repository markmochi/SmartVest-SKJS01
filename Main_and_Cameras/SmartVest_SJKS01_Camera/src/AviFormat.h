#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace AviFormat {
inline void put(uint8_t* out,uint32_t value){for(unsigned i=0;i<4;++i)out[i]=uint8_t(value>>(8*i));}
inline void header(uint8_t* out,uint32_t width,uint32_t height,uint32_t frames=0,uint32_t duration=0,uint32_t peak=0,uint32_t moviEnd=0){
  size_t at=0;auto four=[&](const char* value){std::memcpy(out+at,value,4);at+=4;};
  auto u32=[&](uint32_t value){put(out+at,value);at+=4;};
  auto u16=[&](uint16_t value){out[at++]=uint8_t(value);out[at++]=uint8_t(value>>8);};
    four("RIFF"); u32(0); four("AVI ");
    four("LIST"); u32(192); four("hdrl");
    four("avih"); u32(56); u32(125000); u32(0); u32(0); u32(0x10);
    u32(0); u32(0); u32(1); u32(0); u32(width); u32(height);
    for (int i=0;i<4;++i) u32(0);
    four("LIST"); u32(116); four("strl");
    four("strh"); u32(56); four("vids"); four("MJPG");
    u32(0); u32(0); u32(0); u32(1); u32(8); u32(0); u32(0);
    u32(0); u32(0xffffffff); u32(0);
    u16(0); u16(0); u16(width); u16(height);
    four("strf"); u32(40); u32(40); u32(width); u32(height);
    u16(1); u16(24); four("MJPG"); u32(width*height*3);
    u32(0); u32(0); u32(0); u32(0);
    four("LIST"); u32(0); four("movi");

  if(frames&&duration){
    put(out+4,moviEnd+8+frames*16-8);put(out+32,uint32_t(uint64_t(duration)*1000/frames));
    put(out+48,frames);put(out+60,peak);put(out+128,duration);put(out+132,frames*1000);
    put(out+140,frames);put(out+144,peak);put(out+216,moviEnd-220);
  }
}
}
