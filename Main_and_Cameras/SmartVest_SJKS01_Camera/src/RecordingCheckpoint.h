#pragma once
#include "CameraCrypto.h"
#include <cstdint>
#include <cstring>
namespace RecordingCheckpoint {
constexpr size_t SIZE=304;
struct State {
  uint64_t session=0,start=0,first=0,last=0,utc=0,bytes=0,at=0;
  uint32_t frames=0,gap=0;char payload[65]{},video[81]{};
};
inline void put(uint8_t* out,uint64_t n,size_t size){for(size_t i=0;i<size;++i)out[i]=uint8_t(n>>(8*i));}
inline uint64_t get(const uint8_t* in,size_t size){uint64_t n=0;for(size_t i=0;i<size;++i)n|=uint64_t(in[i])<<(8*i);return n;}
inline bool seal(const char* key,const char* role,const char* identity,const uint8_t* bytes,char* output){
  CameraCrypto::Mac mac;return mac.begin(key)&&mac.field("SMARTVEST RECORDING CHECKPOINT 1")&&mac.field(role)&&mac.field(identity)&&mac.add(bytes,240)&&mac.finish(output);
}
inline bool encode(const State& s,const char* key,const char* role,const char* identity,uint8_t* out){
  if(!CameraCrypto::validHex(s.payload,64))return false;
  std::memset(out,0,SIZE);std::memcpy(out,"SVR1",4);
  const uint64_t fields[]={s.session,s.start,s.first,s.last,s.utc,s.bytes,s.at};
  for(size_t i=0;i<7;++i)put(out+4+8*i,fields[i],8);
  put(out+60,s.frames,4);put(out+64,s.gap,4);std::memcpy(out+80,s.payload,64);
  std::memcpy(out+144,s.video,81);
  char tag[65];if(!seal(key,role,identity,out,tag))return false;std::memcpy(out+240,tag,64);return true;
}
inline bool decode(const uint8_t* in,const char* key,const char* role,const char* identity,State& s){
  if(std::memcmp(in,"SVR1",4))return false;
  char tag[65],saved[65];std::memcpy(saved,in+240,64);saved[64]=0;
  if(!seal(key,role,identity,in,tag)||!CameraCrypto::equal(tag,saved,64))return false;
  uint64_t* fields[]={&s.session,&s.start,&s.first,&s.last,&s.utc,&s.bytes,&s.at};
  for(size_t i=0;i<7;++i)*fields[i]=get(in+4+8*i,8);
  s.frames=uint32_t(get(in+60,4));s.gap=uint32_t(get(in+64,4));std::memcpy(s.payload,in+80,64);s.payload[64]=0;std::memcpy(s.video,in+144,81);s.video[80]=0;
  return s.session&&s.frames<=1024&&s.bytes>=224&&s.bytes<=128ULL*1024*1024&&s.at>=s.start&&
    (!s.frames||(s.first>=s.start&&s.last>=s.first&&s.at>=s.last))&&CameraCrypto::validHex(s.payload,64);
}
}
