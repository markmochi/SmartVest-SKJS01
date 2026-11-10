#pragma once
#include <cstdint>

// Presence is refreshed by every authenticated, validated heartbeat, at any
// point after main boot. Expiry is latched until another heartbeat, preventing
// cached status from appearing online again after the 32-bit clock wraps.
struct CameraPresence {
  uint32_t seen=0,connections=0;
  bool present=false;
  void expire(uint32_t now,uint32_t timeout){
    if(live && uint32_t(now-seen)>=timeout)live=false;
  }
  void heartbeat(uint32_t now,uint32_t timeout){
    expire(now,timeout);
    if(!live)++connections;
    present=true;live=true;seen=now;
  }
  bool online(uint32_t now,uint32_t timeout)const{
    return live && uint32_t(now-seen)<timeout;
  }
  const char* state(uint32_t now,uint32_t timeout)const{
    return !present?"waiting":online(now,timeout)?"online":"offline";
  }
 private:
  bool live=false;
};
