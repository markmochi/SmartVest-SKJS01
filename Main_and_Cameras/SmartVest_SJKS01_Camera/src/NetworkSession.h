#pragma once
#include "HttpPacing.h"
#include "LinkPolicy.h"

namespace CameraHttp {
// One owner, no allocations. Radio retries remain independent in CameraLink.
// New IP connectivity starts a fresh HTTP schedule; HTTP failures never restart
// Wi-Fi. A bounded ACK burst leaves room for status and new incident commands.
class Session {
 public:
  Session(uint32_t heartbeatMs,uint32_t retryMs,uint32_t pollMs,uint32_t phaseMs)
      :heartbeatInterval(heartbeatMs),retryInterval(retryMs),pollInterval(pollMs),phase(phaseMs){}
  bool observe(uint32_t now,bool up,bool gotIp=false) {
    const bool joined=up && (!wasUp || gotIp);
    wasUp=up;
    if(joined){
      pacing=Pacing();announce=true;ackBurst=0;lastWasHeartbeat=false;
      nextHeartbeat=nextPoll=nextAck=now+phase;
    }
    return joined;
  }
  // A storage/recording state transition should not wait for the ordinary
  // five-second heartbeat cadence. Pacing still applies, so this cannot turn a
  // failing HTTP link into a tight retry loop.
  void statusChanged(uint32_t now) {
    if(wasUp && !CameraLink::due(now,nextHeartbeat)) nextHeartbeat=now;
  }
  Request next(uint32_t now,bool haveAck) const {
    if(!wasUp || !pacing.ready(now))return Request::None;
    const bool hb=CameraLink::due(now,nextHeartbeat),poll=CameraLink::due(now,nextPoll);
    const bool ack=haveAck && CameraLink::due(now,nextAck);
    if(announce && hb)return Request::Heartbeat;
    if(ack && (ackBurst<2 || (!hb && !poll)))return Request::Ack;
    return choose(false,hb,poll,lastWasHeartbeat);
  }
  void completed(Request request,uint32_t now,int code,bool heartbeatMatched,uint32_t jitter) {
    // A 200 reply for the wrong camera is a configuration failure, not success.
    const int pacingCode=request==Request::Heartbeat && code==200 && !heartbeatMatched?403:code;
    pacing.result(now,pacingCode,jitter);
    if(request==Request::Heartbeat){
      announce=false;ackBurst=0;lastWasHeartbeat=true;
      const bool configError=code==401 || code==403 || (code==200 && !heartbeatMatched);
      nextHeartbeat=now+(heartbeatMatched || configError?heartbeatInterval:retryInterval)+jitter%101;
    }else if(request==Request::Poll){
      ackBurst=0;lastWasHeartbeat=false;nextPoll=now+pollInterval+jitter%101;
    }else if(request==Request::Ack){
      if(ackBurst<2)++ackBurst;
      nextAck=now+pollInterval+jitter%101;
    }
  }
 private:
  Pacing pacing;
  uint32_t heartbeatInterval,retryInterval,pollInterval,phase;
  uint32_t nextHeartbeat=0,nextPoll=0,nextAck=0;
  bool wasUp=false,announce=false,lastWasHeartbeat=false;
  uint8_t ackBurst=0;
};
}
