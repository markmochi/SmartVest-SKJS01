#pragma once
#include <cstdint>
namespace CameraHttp {
enum class Request { None, Ack, Heartbeat, Poll };
inline Request choose(bool ackDue,bool heartbeatDue,bool pollDue,bool lastWasHeartbeat) {
  if(ackDue)return Request::Ack;
  if(heartbeatDue && (!lastWasHeartbeat || !pollDue))return Request::Heartbeat;
  return pollDue?Request::Poll:Request::None;
}
class Pacing {
 public:
  bool ready(uint32_t now)const{return !waiting || int32_t(now-next)>=0;}
  void result(uint32_t now,int code,uint32_t jitter){
    uint32_t delayMs=75;
    if(code>=200&&code<300){failures=0;}
    else if(code==401||code==403){delayMs=10000;failures=0;}
    else {if(failures<4)++failures;delayMs=500U<<failures;}
    next=now+delayMs+jitter%251;waiting=true;
  }
 private:
  bool waiting=false;uint8_t failures=0;uint32_t next=0;
};
}
