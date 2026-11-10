#pragma once
#include <stdint.h>
#include <string.h>
// One volatile lease for the entire vest. Camera leases end before Main can
// reuse this slot, including when a final heartbeat is lost.
struct DownloadLease {
  uint32_t id=0,stream=0,started=0,until=0,ended=0;
  uint64_t sent=0,size=0;
  int8_t camera=-1;
  uint8_t state=0; // 1 ready, 2 sending, 3 sent, 4 stopped, 5 preparing, 6 stopping
  bool startup=true;
  char token[33]{},owner[65]{};
  bool active()const{return state==1||state==2||state==5||state==6;}
  bool ownedBy(const char* value)const{
    if(strlen(value)!=64)return false;
    unsigned difference=0;for(unsigned i=0;i<64;++i)difference|=uint8_t(value[i])^uint8_t(owner[i]);
    return difference==0;
  }
  void finish(uint8_t value,uint32_t now){state=value;ended=now;token[0]=0;}
  void tick(uint32_t now){
    if(startup&&now>=12000)startup=false;
    if(active()&&(int32_t(now-until)>=0||uint32_t(now-started)>=900000))finish(4,now);
  }
  uint32_t allowance(uint32_t now)const{
    int32_t remaining=int32_t(until-now)-2000;
    const int32_t lifetime=900000-int32_t(uint32_t(now-started))-2000;
    if(lifetime<remaining)remaining=lifetime;
    return remaining<=0?0:remaining>10000?10000:uint32_t(remaining);
  }
};
