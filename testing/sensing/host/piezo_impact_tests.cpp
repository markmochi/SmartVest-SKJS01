#include "src/sensing/PiezoImpact.h"
#include <cassert>
#include <iostream>
namespace P=PiezoProtocol;
struct Rig {
 PiezoImpact m;uint32_t now=0,us=0;
 Rig(){m.configure(500,500);m.resetBaseline();}
 void step(unsigned n,uint16_t f=1000,uint16_t r=2000){for(unsigned j=0;j<n;++j){++now;us+=1000;m.feed(now,us,f,r);}}
 void settle(){step(2100);assert(m.ready());}
 void hit(uint16_t f,uint16_t r){step(2,uint16_t(1000+f),uint16_t(2000+r));step(400);}
};
int main(){
 {Rig r;r.settle();r.step(30,1499,2000);r.step(400);assert(r.m.data.bank[0].hits==0);r.hit(500,0);assert(r.m.data.bank[0].hits==1&&r.m.data.bank[0].lastPeak==500);assert(r.m.data.bank[1].hits==0);
  r.step(2000,1700,2000);assert(r.m.data.bank[0].hits==2);r.step(400);r.hit(900,800);assert(r.m.data.bank[0].hits==3&&r.m.data.bank[1].hits==1);
  r.m.candidate(1,r.now);assert(r.m.data.matchedBanks==3);r.step(5000);r.m.candidate(2,r.now);assert(r.m.data.matchedBanks==0&&r.m.data.matchedHits[0]==0&&r.m.data.matchedHits[1]==0);
  r.step(61000);assert(r.m.data.bank[0].flags&P::Old);r.hit(800,0);assert(!(r.m.data.bank[0].flags&P::Old)&&r.m.data.matchedHits[0]!=r.m.data.bank[0].hits);
 }
 {Rig r;for(unsigned i=0;i<30;++i)r.step(100,i%2?1400:1000,2000);assert(!r.m.calibrated());r.step(2100);assert(r.m.ready());
  r.step(2100,1250,2000);assert(r.m.data.bank[0].flags&P::Drift);assert(!r.m.ready());r.m.resetBaseline();r.step(2100);assert(r.m.ready());
  r.step(1,4095,2000);assert(r.m.data.bank[0].flags&P::Clipped);assert(!r.m.ready());r.step(400);assert(r.m.ready());
 }
 {Rig r;r.settle();assert(!r.m.start(2,r.now));assert(r.m.start(1,r.now));
  for(unsigned j=0;j<15000;++j)r.step(1,j%100==0?1100:1000,j%100==50?2120:2000);
  assert(r.m.data.normal.state==P::Complete&&r.m.data.normal.maxRise[0]==100&&r.m.data.normal.maxRise[1]==120);
  assert(r.m.start(2,r.now));const auto start=r.now;
  for(unsigned j=0;j<15000;++j){const bool f=j==1000||j==5000||j==9000;const bool b=j==2000||j==6000||j==10000;r.step(1,f?1400:1000,b?2600:2000);}
  assert(r.now-start==15000);assert(r.m.data.taps.state==P::Complete&&r.m.data.taps.hits[0]==3&&r.m.data.taps.hits[1]==3);
  assert(r.m.data.taps.minPeak[0]==400&&r.m.data.taps.minPeak[1]==600);
  assert(r.m.data.bank[0].hits==0&&r.m.data.bank[1].hits==3);
  r.m.data.sequence=1;r.m.data.flags=15;ImuLabProtocol::Frame frame;frame.boot=1;frame.session=2;P::encode(r.m.data,frame);assert(frame.length==180);
  P::Status value;assert(P::decode(frame,value)&&value.taps.hits[0]==3&&value.normal.maxRise[1]==120&&value.bank[1].hits==3);
  uint8_t wire[ImuLabProtocol::MAX_FRAME];const auto size=ImuLabProtocol::encode(frame,wire);assert(size==202);ImuLabProtocol::Frame decoded;assert(ImuLabProtocol::decode(wire,size,decoded));
  for(unsigned i=0;i<size;++i){wire[i]^=1;assert(!ImuLabProtocol::decode(wire,size,decoded));wire[i]^=1;}
  frame.data[159]=1;assert(!P::decode(frame,value));frame.data[159]=0;frame.data[0]=1;assert(!P::decode(frame,value));
 }
 {Rig r;r.settle();assert(r.m.start(1,r.now));r.now+=20;r.us+=20000;r.m.feed(r.now,r.us,1000,2000);assert(r.m.data.normal.state==P::Interrupted&&r.m.data.normal.gaps==1);
  r.m.resetBaseline();r.step(2100);assert(r.m.start(1,r.now));r.step(1,4095,2000);r.step(15000);assert(r.m.data.normal.clipped>0&&!r.m.start(2,r.now));
 }
 {Rig r;r.step(2100,3800,2000);assert(r.m.calibrated()&&!r.m.ready());r.m.configure(200,500);r.step(1,3800,2000);assert(r.m.ready());}
 {Rig r;r.now=UINT32_MAX-2200;r.us=UINT32_MAX-2200000;r.step(2100);assert(r.m.ready());r.hit(700,700);assert(r.m.data.bank[0].hits==1&&r.m.data.bank[1].hits==1);r.m.tick(r.now+61000);r.m.tick(r.now+62000);}
 std::cout<<"PASS piezo baseline quality, inclusive threshold, peak window, no held-input repeats, rearm, independent banks, drift/headroom/clipping, two trials, below-threshold trial taps, corroboration expiry, timer wrap and framed status integrity\n";
}
