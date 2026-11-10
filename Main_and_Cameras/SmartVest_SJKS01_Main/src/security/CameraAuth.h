#pragma once
#include "CameraCrypto.h"
#include <cstdio>
#include <cstdlib>
#include <climits>

namespace CameraAuth {
constexpr uint32_t PENDING_MS=15000,REKEY_MS=30*60*1000;
struct Exchange {char client[33]{},session[33]{};uint32_t sequence=0;};
inline bool role(const char* s){return s&&(!std::strcmp(s,"front")||!std::strcmp(s,"rear"));}
inline bool number(const char* s,uint32_t& result) {
  if(!s||!*s||std::strlen(s)>10||s[0]=='0')return false;
  uint64_t value=0;for(const char* p=s;*p;++p){if(*p<'0'||*p>'9')return false;value=value*10+unsigned(*p-'0');if(value>UINT32_MAX)return false;}
  result=uint32_t(value);return result!=0;
}
inline bool hello(const char* key,const char* who,const char* client,char* output) {
  CameraCrypto::Mac mac;return role(who)&&CameraCrypto::validHex(client,32)&&mac.begin(key)&&
    mac.field("SMARTVEST CAMERA CLIENT 2")&&mac.field(who)&&mac.field(client)&&mac.finish(output);
}
inline bool proof(const char* key,const char* who,const Exchange& x,char* output) {
  CameraCrypto::Mac mac;
  return role(who)&&CameraCrypto::validHex(x.client,32)&&CameraCrypto::validHex(x.session,32)&&
    mac.begin(key)&&mac.field("SMARTVEST CAMERA SESSION 2")&&mac.field(who)&&mac.field(x.client)&&mac.field(x.session)&&mac.finish(output);
}
inline bool message(const char* key,const char* who,const Exchange& x,const char* method,const char* path,
                    const char* body,size_t size,int status,char* output) {
  if(!role(who)||!CameraCrypto::validHex(x.client,32)||!CameraCrypto::validHex(x.session,32)||!x.sequence||size>4096)return false;
  char seq[11],code[12];std::snprintf(seq,sizeof(seq),"%lu",(unsigned long)x.sequence);std::snprintf(code,sizeof(code),"%d",status);
  CameraCrypto::Mac mac;
  return mac.begin(key)&&mac.field(status?"SMARTVEST CAMERA RESPONSE 2":"SMARTVEST CAMERA REQUEST 2")&&mac.field(who)&&
    mac.field(x.client)&&mac.field(x.session)&&mac.field(seq)&&mac.field(method)&&mac.field(path)&&mac.field(code)&&
    mac.add(body,size)&&mac.finish(output);
}
class Peer {
 public:
  bool offer(const char* client,const char* nonce,uint32_t now,Exchange& result) {
    if(!CameraCrypto::validHex(client,32)||!CameraCrypto::validHex(nonce,32))return false;
    if(pending && uint32_t(now-offeredAt)<PENDING_MS) {
      if(!CameraCrypto::equal(client,candidate.client,32))return false;
    }else{
      std::memcpy(candidate.client,client,33);std::memcpy(candidate.session,nonce,33);candidate.sequence=0;
      offeredAt=now;pending=true;
    }
    result=candidate;return true;
  }
  bool accept(const char* key,const char* who,const Exchange& x,const char* method,const char* path,
              const char* body,size_t size,const char* signature,uint32_t now) {
    const bool current=CameraCrypto::equal(x.client,active.client,32)&&CameraCrypto::equal(x.session,active.session,32);
    const bool next=pending&&uint32_t(now-offeredAt)<PENDING_MS&&x.sequence==1&&
      CameraCrypto::equal(x.client,candidate.client,32)&&CameraCrypto::equal(x.session,candidate.session,32);
    if((!current&&!next)||(current&&x.sequence<=active.sequence))return false;
    char expected[65];if(!message(key,who,x,method,path,body,size,0,expected)||!CameraCrypto::equal(expected,signature,64))return false;
    active=x;if(next)pending=false;return true;
  }
 private:
  Exchange active{},candidate{};uint32_t offeredAt=0;bool pending=false;
};
}
