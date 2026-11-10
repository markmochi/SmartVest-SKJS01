#pragma once
#include <stdint.h>
// Pure retry state machine. One owner initiates joins; ordinary HTTP failures
// do not mutate the radio. Never cancel a healthy or still-young association.
namespace CameraLink {
enum class Action { None, Begin, Cancel };
inline bool due(uint32_t now,uint32_t deadline){return int32_t(now-deadline)>=0;}
class Retry {
 public:
  Retry(uint32_t joinTimeout=30000,uint32_t initial=2000,uint32_t maximum=30000)
    :timeout(joinTimeout),first(initial),limit(maximum),backoff(initial){}
  void start(uint32_t now,uint32_t phase=0){ready=false;joining=false;next=now+phase;backoff=first;}
  void failed(uint32_t now,uint32_t jitter=0){
    if(!joining && !ready)return; // duplicate/cancel callback cannot postpone retries
    ready=false;joining=false;next=now+backoff+jitter;
    backoff=backoff>=limit/2?limit:backoff*2;
  }
  Action tick(uint32_t now,bool hasIp,uint32_t jitter=0){
    if(hasIp){ready=true;joining=false;backoff=first;return Action::None;}
    if(ready){failed(now,jitter);return Action::None;}
    if(joining){
      if(uint32_t(now-began)>=timeout){failed(now,jitter);return Action::Cancel;}
      return Action::None;
    }
    if(due(now,next)){joining=true;began=now;return Action::Begin;}
    return Action::None;
  }
  void awaitDriverRetry(uint32_t now){ready=false;joining=true;began=now;}
  bool online()const{return ready;}
  bool inFlight()const{return joining;}
 private:
  uint32_t timeout,first,limit,backoff,began=0,next=0;
  bool ready=false,joining=false;
};
}
