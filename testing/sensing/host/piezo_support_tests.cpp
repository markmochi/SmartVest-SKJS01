#include "src/sensing/PiezoImpact.h"
#include <cassert>
#include <iostream>
namespace P=PiezoProtocol;
struct SupportRig {
 PiezoImpact m;uint32_t now=0,us=0;uint16_t frontRest=0,rearRest=0;
 SupportRig(uint16_t f=0,uint16_t r=0):frontRest(f),rearRest(r){m.resetBaseline();step(2100,f,r);}
 void step(unsigned n,uint16_t f=0,uint16_t r=0){for(unsigned j=0;j<n;++j){++now;us+=1000;m.feed(now,us,f,r);}}
 void rest(unsigned n=400){step(n,frontRest,rearRest);}
 void pulse(uint16_t front,uint16_t rear){step(2,front,rear);rest();}
};
int main(){
 assert(P::DEFAULT_THRESHOLD==100&&P::SUPPORT_THRESHOLD==800);
 {SupportRig r;assert(r.m.ready());for(uint16_t v:{50,90,99})r.pulse(v,v);
  assert(r.m.data.bank[0].hits==0&&r.m.data.bank[1].hits==0);
  r.pulse(800,0);assert(r.m.data.bank[0].hits==1&&r.m.data.bank[1].hits==0);
  r.m.candidate(1,r.now);assert(r.m.data.matchedBanks==1);
  r.pulse(0,800);assert(r.m.data.bank[1].hits==1&&r.m.data.matchedBanks==3);
  r.rest(5000);r.m.candidate(2,r.now);assert(!r.m.data.matchedBanks);
  r.pulse(0,800);assert(r.m.data.matchedBanks==2); // Rear alone supports the current IMU candidate.
  r.m.configure(100,100);assert(!r.m.data.matchedBanks&&!r.m.data.bank[1].lastPeak);
  r.rest();r.m.candidate(3,r.now);assert(!r.m.data.matchedBanks);
 }
 {SupportRig r;r.pulse(4095,4095);assert(!r.m.data.bank[0].hits&&!r.m.data.bank[1].hits);
  r.pulse(800,0);assert(r.m.data.bank[0].hits==1);r.m.configure(800,800);r.rest();
  r.pulse(799,0);assert(r.m.data.bank[0].hits==1);r.pulse(800,0);assert(r.m.data.bank[0].hits==2);
 }
 {SupportRig r(3294,0);assert(r.m.ready());r.pulse(4094,0);assert(r.m.data.bank[0].lastPeak==800);}
 {SupportRig r(3995,0);assert(!r.m.ready());r.pulse(4094,0);assert(!r.m.data.bank[0].hits);}
 {SupportRig r;r.pulse(800,0);r.now+=20;r.us+=20000;r.m.feed(r.now,r.us,0,0);
  r.m.candidate(1,r.now);assert(!r.m.data.matchedBanks); // No support across lost acquisition continuity.
 }
 {SupportRig r;r.m.candidate(1,r.now);r.rest(1600);r.pulse(800,0);assert(!r.m.data.matchedBanks);}
 {SupportRig r;r.pulse(800,0);r.rest(4500);r.m.candidate(1,r.now);assert(!r.m.data.matchedBanks);}
 {SupportRig r;r.step(2000,3000,0);assert(r.m.data.bank[0].hits==1);r.rest();r.pulse(800,0);assert(r.m.data.bank[0].hits==2);}
 {P::Status s;s.sequence=1;ImuLabProtocol::Frame f;P::encode(s,f);P::Status decoded;assert(P::decode(f,decoded)&&decoded.threshold[0]==100);
  s.threshold[0]=800;P::encode(s,f);assert(P::decode(f,decoded));s.threshold[0]=801;P::encode(s,f);assert(!P::decode(f,decoded));}
 {SupportRig r;r.pulse(100,0);assert(r.m.data.bank[0].hits==1);r.m.candidate(1,r.now);assert(!r.m.data.matchedBanks);
  r.pulse(799,0);assert(r.m.data.bank[0].hits==2&&!r.m.data.matchedBanks);r.pulse(800,0);assert(r.m.data.matchedBanks==1);
  const auto support=r.m.data.bank[0].supportHit;r.pulse(100,0);assert(r.m.data.bank[0].hits==4&&r.m.data.bank[0].supportHit==support&&r.m.data.matchedHits[0]==support);}
 {SupportRig r(3295,0);assert(r.m.ready());r.pulse(4094,0);assert(r.m.data.bank[0].lastPeak==799&&!r.m.data.bank[0].supportHit);}
 {SupportRig r;r.pulse(800,0);r.m.candidate(1,r.now);assert(r.m.data.matchedBanks==1);r.step(1,4095,0);assert(!r.m.data.matchedBanks&&!r.m.data.bank[0].supportHit);}
 {SupportRig r;r.m.candidate(1,r.now);r.rest(1499);r.pulse(800,0);assert(r.m.data.matchedBanks==1);}
 {SupportRig r;r.m.candidate(1,r.now);r.rest(1500);r.pulse(800,0);assert(!r.m.data.matchedBanks);}
 {SupportRig r;r.pulse(800,0);r.rest(4099);r.m.candidate(1,r.now);assert(r.m.data.matchedBanks==1);}
 {SupportRig r;r.pulse(800,0);r.rest(4100);r.m.candidate(1,r.now);assert(!r.m.data.matchedBanks);}
 std::cout<<"PASS 100 point hits and optional 800 point support: 99 rejected, exact 100 hit and 800 support accepted, 799 unsupported, weak later hits retain strong evidence, either bank sufficient, 800 override, clipping/headroom, held-input rearm, configuration/continuity isolation, correlation timing and status bounds\n";
}
