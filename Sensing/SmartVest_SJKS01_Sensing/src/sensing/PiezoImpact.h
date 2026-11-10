#pragma once
#include "../link/PiezoProtocol.h"
#include <algorithm>
#include <cmath>

class PiezoImpact {
 public:
  PiezoProtocol::Status data;
  void resetBaseline() {
    ++data.epoch;if(!data.epoch)++data.epoch;
    for(auto& b:bank)b=Tracker();
    data.normal=PiezoProtocol::Trial();data.taps=PiezoProtocol::Trial();trialKind=0;
    for(auto& b:data.bank){b.flags=0;b.rise=0;b.lastPeak=0;b.lastAt=0;b.supportPeak=0;b.supportAt=b.supportHit=0;}
    data.matchedBanks=0;data.matchedHits[0]=data.matchedHits[1]=0;candidateCurrent=false;haveTime=false;
  }
  bool capturing() const {return trialKind!=0;}
  bool calibrated() const {return bank[0].calibrated&&bank[1].calibrated;}
  bool ready() const {return usable(0)&&usable(1)&&data.bank[0].raw<4095&&data.bank[1].raw<4095;}
  void breakContinuity() {
    if(capturing())trial()->state=PiezoProtocol::Interrupted;
    trialKind=0;
    for(unsigned i=0;i<2;++i){bank[i]=Tracker();data.bank[i].flags=0;data.bank[i].lastPeak=0;data.bank[i].lastAt=0;data.bank[i].rise=0;clearSupport(i);}
    data.matchedBanks=0;candidateCurrent=false;
    ++data.epoch;if(!data.epoch)++data.epoch;
    haveTime=false;
  }
  void configure(uint16_t front,uint16_t rear) {
    data.threshold[0]=front;data.threshold[1]=rear;
    data.normal=PiezoProtocol::Trial();data.taps=PiezoProtocol::Trial();trialKind=0;
    data.matchedBanks=0;data.matchedHits[0]=data.matchedHits[1]=0;candidateCurrent=false;
    // Hits captured under an earlier threshold cannot corroborate a new decision.
    for(unsigned i=0;i<2;++i){data.bank[i].lastPeak=0;data.bank[i].lastAt=0;clearSupport(i);}
    for(auto& b:bank){b.active=false;b.armed=false;b.releasing=false;b.drift=false;}
  }
  bool start(uint8_t kind,uint32_t now) {
    namespace P=PiezoProtocol;
    if(kind<1||kind>2||capturing()||!ready()||nextTrial==UINT32_MAX)return false;
    if(kind==2 && (data.normal.state!=P::Complete||data.normal.epoch!=data.epoch||data.normal.gaps||data.normal.clipped))return false;
    if(kind==1){data.normal=P::Trial();data.taps=P::Trial();}else data.taps=P::Trial();
    trialKind=kind;auto* t=trial();t->id=nextTrial++;t->epoch=data.epoch;t->state=P::Recording;trialAt=now;
    for(unsigned i=0;i<2;++i){
      t->baseline[i]=data.bank[i].baseline;
      t->floor[i]=kind==1?4095:uint16_t(std::min(4094u,unsigned(data.normal.maxRise[i])+std::max(25u,unsigned(data.normal.maxRise[i])/4)));
      capture[i]=Capture();
    }
    return true;
  }
  void stop(uint32_t now,bool interrupted=false) {
    if(!capturing())return;
    auto* t=trial();t->elapsedMs=uint16_t(std::min<uint32_t>(15000u,uint32_t(now-trialAt)));
    t->state=interrupted?PiezoProtocol::Interrupted:PiezoProtocol::Complete;trialKind=0;
  }
  void tick(uint32_t now) {if(capturing()&&uint32_t(now-trialAt)>=15000)stop(now);}
  // Correlation uses hit start time, so completing its 25 ms peak window cannot shorten the 1.5 s interval.
  void candidate(uint32_t id,uint32_t at) {
    if(!id||id==data.candidate)return;
    data.candidate=id;data.candidateAt=at;candidateCurrent=true;
    data.matchedBanks=0;data.matchedHits[0]=data.matchedHits[1]=0;
    for(unsigned i=0;i<2;++i)match(i,at);
  }
  void feed(uint32_t now,uint32_t us,uint16_t front,uint16_t rear,bool enabled=true,bool configValid=true) {
    namespace P=PiezoProtocol;
    const uint32_t delta=haveTime?uint32_t(us-lastUs):1000;lastUs=us;haveTime=true;
    data.maxGapUs=std::max(data.maxGapUs,delta);
    if(delta>7500){
      if(capturing()){increment(trial()->gaps);stop(now,true);}
      for(auto& b:bank){b.active=false;b.armed=false;b.releasing=false;b.haveQuiet=false;}
      for(unsigned i=0;i<2;++i){data.bank[i].lastPeak=0;data.bank[i].lastAt=0;clearSupport(i);}
      data.matchedBanks=0;data.matchedHits[0]=data.matchedHits[1]=0;candidateCurrent=false;
    }
    tick(now);++data.samples;data.uptime=now;
    for(unsigned i=0;i<2;++i){
      const uint16_t raw=i?rear:front;auto& b=bank[i];auto& out=data.bank[i];out.raw=raw;
      if(out.lastPeak&&uint32_t(now-out.lastAt)>60000)b.old=true;
      if(raw==4095){if(capturing())increment(trial()->clipped);b.active=b.armed=b.releasing=false;b.haveQuiet=false;clearSupport(i);out.flags=P::Clipped|(b.old?P::Old:0);continue;}
      if(!b.calibrated){
        if(!b.haveQuiet||raw+200u<b.quietMax||raw>b.quietMin+200u){b.haveQuiet=true;b.quietAt=now;b.quietMin=b.quietMax=raw;b.sum=0;b.count=0;}
        b.quietMin=std::min(b.quietMin,raw);b.quietMax=std::max(b.quietMax,raw);b.sum+=raw;++b.count;
        if(uint32_t(now-b.quietAt)>=2000&&b.count>=400){b.base=float(b.sum)/b.count;b.calibrated=true;b.armed=true;}
      }
      out.baseline=uint16_t(std::lround(b.base));out.rise=raw>out.baseline?uint16_t(raw-out.baseline):0;
      if(!b.calibrated){out.flags=b.old?P::Old:0;continue;}
      const int difference=int(raw)-int(out.baseline);
      if(!capturing()&&!b.active&&std::abs(difference)<200)b.base+=(float(raw)-b.base)*float(std::min<uint32_t>(delta,7500u))/(1000000.f+float(std::min<uint32_t>(delta,7500u)));
      const uint16_t release=std::max(uint16_t(25),uint16_t(data.threshold[i]/3));
      if(std::abs(difference)<release){if(!b.releasing){b.releasing=true;b.releaseAt=now;}if(uint32_t(now-b.releaseAt)>=50)b.armed=true;}else b.releasing=false;
      if(!b.active&&out.rise<data.threshold[i]&&std::abs(difference)>=200){if(!b.drifting){b.drifting=true;b.driftAt=now;}if(uint32_t(now-b.driftAt)>=2000)b.drift=true;}else b.drifting=false;
      const bool valid=usable(i)&&enabled&&configValid;
      if(valid&&!b.active&&b.armed&&(!b.finished||uint32_t(now-b.finishedAt)>=300)&&out.rise>=data.threshold[i]){b.active=true;b.armed=false;b.releasing=false;b.at=now;b.peak=out.rise;}
      if(b.drift||!enabled||!configValid)clearSupport(i);
      if(b.active){b.peak=std::max(b.peak,out.rise);if(uint32_t(now-b.at)>=25){b.active=false;b.finished=true;b.finishedAt=now;b.old=false;out.lastPeak=b.peak;out.lastAt=b.at;++out.hits;
        if(valid&&b.peak>=P::SUPPORT_THRESHOLD){out.supportPeak=b.peak;out.supportAt=b.at;out.supportHit=out.hits;}
        match(i,now);}}
      out.flags=(valid?P::Ready:0)|(b.active?P::Capturing:0)|(b.armed?P::Armed:0)|(b.drift?P::Drift:0)|(b.old?P::Old:0);
      if(capturing())record(i,out.rise,now);
    }
  }
 private:
  struct Tracker {
    float base=0;uint64_t sum=0;uint32_t count=0,quietAt=0,at=0,finishedAt=0,releaseAt=0,driftAt=0;
    uint16_t quietMin=0,quietMax=0,peak=0;
    bool calibrated=false,haveQuiet=false,active=false,armed=false,finished=false,releasing=false,drifting=false,drift=false,old=false;
  } bank[2];
  struct Capture {bool active=false,armed=true,releasing=false,finished=false;uint32_t at=0,releaseAt=0,finishedAt=0;uint16_t peak=0;} capture[2];
  uint32_t lastUs=0,trialAt=0,nextTrial=1;uint8_t trialKind=0;bool haveTime=false,candidateCurrent=false;
  static void increment(uint16_t& n){if(n!=65535)++n;}
  PiezoProtocol::Trial* trial(){return trialKind==1?&data.normal:&data.taps;}
  bool usable(unsigned i) const {return bank[i].calibrated&&!bank[i].drift&&uint32_t(std::lround(bank[i].base))+data.threshold[i]<4095;}
  void clearSupport(unsigned i){auto& b=data.bank[i];b.supportPeak=0;b.supportAt=b.supportHit=0;data.matchedBanks&=uint8_t(~(1u<<i));data.matchedHits[i]=0;}
  void match(unsigned i,uint32_t now){
    const auto& b=data.bank[i];
    if(!candidateCurrent||!data.candidate||!b.supportHit||b.supportPeak<PiezoProtocol::SUPPORT_THRESHOLD||!usable(i))return;
    const int32_t offset=int32_t(b.supportAt-data.candidateAt);
    if(offset>=-4500&&offset<=1500&&uint32_t(now-b.supportAt)<=4500){data.matchedBanks|=uint8_t(1u<<i);data.matchedHits[i]=b.supportHit;}
  }
  void record(unsigned i,uint16_t rise,uint32_t now){
    auto* t=trial();t->elapsedMs=uint16_t(std::min<uint32_t>(15000u,uint32_t(now-trialAt)));t->maxRise[i]=std::max(t->maxRise[i],rise);
    if(trialKind!=2)return;
    auto& c=capture[i];const auto floor=t->floor[i];
    if(rise<=floor/2){if(!c.releasing){c.releasing=true;c.releaseAt=now;}if(uint32_t(now-c.releaseAt)>=50)c.armed=true;}else c.releasing=false;
    if(!c.active&&c.armed&&(!c.finished||uint32_t(now-c.finishedAt)>=300)&&rise>floor){c.active=true;c.armed=false;c.at=now;c.peak=rise;}
    if(c.active){c.peak=std::max(c.peak,rise);if(uint32_t(now-c.at)>=25){c.active=false;c.finished=true;c.finishedAt=now;if(!t->hits[i]||c.peak<t->minPeak[i])t->minPeak[i]=c.peak;increment(t->hits[i]);}}
  }
};
