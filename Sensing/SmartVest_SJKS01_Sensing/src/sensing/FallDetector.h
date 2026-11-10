#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

// Balanced motion rule. Incident transport is owned by the sensing sketch.
class FallDetector {
 public:
  enum State : uint8_t { Unavailable, NeedReference, Waiting, Evaluating, Candidate, Recovering };
  enum Reason : uint8_t { Continuous, Starting, Gap, Rejected, Stale, Calibration, SensorLost, Clipped };
  enum Flag : uint16_t { HighG=1, FastRotation=2, AbnormalTilt=4, LowActivity=8, FallCandidate=16, LowG=32, Severe=64, FallRate=128, Event=256, Reference=512 };
  static constexpr uint32_t MAX_INTERVAL_US=7500, WINDOW_US=4500000;
  State state=Unavailable;Reason reason=Starting;
  uint32_t count=0,rotationHoldUs=0,tiltHoldUs=0;
  uint16_t flags=0;
  float change=0,peakG=0,peakDps=0,peakTilt=0;
  uint64_t eventStartUs=0,decisionUs=0;
  bool latched=false;
  // One bounded RAM record: latest candidate survives recovery/reset/invalidation.
  struct Record {uint32_t startUs=0,decisionUs=0,rotationUs=0,tiltUs=0;float peakG=0,peakDps=0,change=0;} last;
  uint32_t maxRotationUs=0;
  void invalidate(Reason why) {
    reason=why;state=Unavailable;active=false;reference=false;quiet=false;
    rotation=tilted=recovery=low=false;sawRotation=false;haveTime=false;
    rotationHoldUs=tiltHoldUs=0;flags=latched?FallCandidate:0;change=0;
  }
  void reset() {latched=false;haveFinished=false;invalidate(Starting);}
  void feed(uint64_t now,float g,float rate,float side,float forward,float activity,float baseline=1) {
    if(!std::isfinite(g)||!std::isfinite(rate)||!std::isfinite(side)||!std::isfinite(forward)||!std::isfinite(activity)){invalidate(Rejected);return;}
    if(haveTime && (now<=lastTime || now-lastTime>MAX_INTERVAL_US))invalidate(Gap);
    haveTime=true;lastTime=now;reason=Continuous;
    const float tilt=std::max(std::fabs(side),std::fabs(forward));
    flags=(g>2.5f?HighG:0)|(rate>240?FastRotation:0)|(tilt>=60?AbnormalTilt:0)|(g<.35f?LowG:0)|((g>5||rate>500)?Severe:0)|(rate>=120?FallRate:0);
    if(activity<.05f && rate<5){if(!low){low=true;lowAt=now;}if(now-lowAt>=2000000)flags|=LowActivity;}else low=false;
    if(latched){
      flags|=FallCandidate;state=Candidate;
      if(tilt<15 && rate<15 && std::fabs(g-baseline)<.15f){
        if(!recovery){recovery=true;recoveryAt=now;}state=Recovering;
        if(now-recoveryAt>=2000000){latched=false;active=false;reference=false;quiet=false;recovery=false;haveFinished=true;finished=now;}
      }else recovery=false;
      return;
    }
    if(active && now-eventStartUs>WINDOW_US){finish(now);state=Waiting;return;}
    if(!active){
      if(rate<15 && std::fabs(g-baseline)<.15f){
        if(!quiet){quiet=true;quietAt=now;}
        if(now-quietAt>=150000){refSide=side;refForward=forward;reference=true;}
      }else quiet=false;
      state=reference?Waiting:NeedReference;
      if(!reference)return;
      flags|=Reference;
      if(haveFinished && now-finished<1500000)return;
      if(!(flags&(HighG|FastRotation|LowG|Severe|FallRate)))return;
      active=true;eventStartUs=now;startSide=refSide;startForward=refForward;
      change=peakG=peakDps=peakTilt=0;maxRotationUs=0;sawRotation=false;rotation=tilted=false;rotationHoldUs=tiltHoldUs=0;
    }
    state=Evaluating;flags|=Event|Reference;
    change=std::max(change,std::max(std::fabs(wrap(side-startSide)),std::fabs(wrap(forward-startForward))));
    peakG=std::max(peakG,g);peakDps=std::max(peakDps,rate);peakTilt=std::max(peakTilt,tilt);
    if(rate>=120){if(!rotation){rotation=true;rotationAt=now;}rotationHoldUs=uint32_t(now-rotationAt);maxRotationUs=std::max(maxRotationUs,rotationHoldUs);if(rotationHoldUs>=100000)sawRotation=true;}
    else {rotation=false;rotationHoldUs=0;}
    if(tilt>=60){if(!tilted){tilted=true;tiltAt=now;}tiltHoldUs=uint32_t(now-tiltAt);}else{tilted=false;tiltHoldUs=0;}
    if(sawRotation && change>=30 && tilted && tiltHoldUs>=800000){
      last={uint32_t(eventStartUs),uint32_t(now),maxRotationUs,tiltHoldUs,peakG,peakDps,change};
      latched=true;decisionUs=now;++count;state=Candidate;flags|=FallCandidate;finish(now);
    }
  }
 private:
  bool active=false,reference=false,quiet=false,rotation=false,tilted=false,sawRotation=false;
  bool recovery=false,low=false,haveTime=false,haveFinished=false;
  uint64_t lastTime=0,quietAt=0,rotationAt=0,tiltAt=0,recoveryAt=0,lowAt=0,finished=0;
  float refSide=0,refForward=0,startSide=0,startForward=0;
  static float wrap(float a){while(a>180)a-=360;while(a< -180)a+=360;return a;}
  void finish(uint64_t now){active=false;haveFinished=true;finished=now;sawRotation=false;rotation=tilted=false;}
};
