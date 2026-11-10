#include <cassert>
#include <cstdint>
#include <iostream>
#include <random>
#include "../../Nano/SmartVest_INA226_Nano/src/BatteryDisplay.h"
#include "../sensing/host/battery/not_charging_capture.h"
struct Run {
  BatteryDisplay filter;uint32_t now=0;uint8_t shown=0;
  uint8_t sample(uint32_t mv,uint32_t elapsed=1000){
    shown=filter.update(mv,now);now+=elapsed;
    assert(shown<=100&&shown%10==0&&filter.smoothedPercent()<=100);return shown;
  }
};
int main(){
  static_assert(sizeof(BatteryDisplay)<=96,"Fixed Nano filter memory");
  assert(BatteryDisplay::sourceMillivolts(11600,50000)==11650);
  assert(BatteryDisplay::sourceMillivolts(11800,-25000)==11775);
  assert(BatteryDisplay::sourceMillivolts(10600,0)==10600);
  assert(BatteryDisplay::sourceMillivolts(11650,500)==11651);
  Run correctedFull;assert(correctedFull.sample(BatteryDisplay::sourceMillivolts(11600,50000))==100);
  assert(BatteryProtocol::estimate3S(10600)==0&&BatteryProtocol::estimate3S(11125)==50);
  assert(BatteryProtocol::estimate3S(11650)==100&&BatteryProtocol::estimate3S(11800)==100);
  Run full;full.sample(11650);
  for(unsigned n=0;n<3600;++n)assert(full.sample(n%2?11650:11800)==100);

  for(auto mv:{10600u,10705u,10810u,11125u,11440u,11650u}){
    Run r;const auto first=r.sample(mv);
    for(unsigned i=0;i<3600;++i)assert(r.sample(mv)==first);
  }
  for(unsigned amplitude:{100u,200u}){
    Run r;r.sample(11125);
    for(unsigned i=1;i<=3600;++i){
      assert(r.sample(i/10%2?11125+amplitude:11125-amplitude)==50);
      if(i>=300)assert(r.filter.smoothedPercent()>=47&&r.filter.smoothedPercent()<=53);
    }
  }
  Run random;random.sample(11125);std::mt19937 rng(921);
  for(unsigned i=0;i<7200;++i){
    const auto mv=11125+int(rng()%301)-150;
    assert(random.sample(mv)==50);
  }
  Run step;step.sample(11125);
  for(unsigned i=1;i<=3600;++i){
    auto shown=step.sample(11650);
    if(i<=300)assert(shown==50);
    if(i==3)assert(step.filter.riseHeld());
    if(i>=64)assert(!step.filter.riseHeld());
  }
  assert(step.shown==100);
  for(unsigned length:{1u,2u,15u,30u,60u}){
    Run pulse;pulse.sample(11125);
    for(unsigned i=0;i<length;++i)assert(pulse.sample(11650)==50);
    for(unsigned i=0;i<300;++i)assert(pulse.sample(11125)==50);
  }
  // Ordinary gradual recovery is no longer restricted by a permanent slow-up
  // rate. The voltage estimate follows it with a bounded filter lag.
  Run gradual;gradual.sample(11000);
  for(unsigned i=1;i<=600;++i){
    const auto raw=BatteryProtocol::estimate3S(11000+i/2);
    gradual.sample(11000+i/2);
    if(i>120)assert(int(gradual.filter.smoothedPercent())>=int(raw)-3);
  }
  assert(gradual.shown>=60);
  Run discharge;discharge.sample(11650);
  for(unsigned i=1;i<=14400;++i){
    unsigned mv=11650-i*1050/14400;discharge.sample(mv);
    const auto raw=BatteryProtocol::estimate3S(mv);
    assert(discharge.filter.smoothedPercent()<=raw+3);
  }
  Run reserve;reserve.sample(12000);
  reserve.sample(10810);reserve.sample(10810);assert(reserve.sample(10810)==20);
  reserve.sample(10705);reserve.sample(10705);assert(reserve.sample(10705)==10);
  reserve.sample(10600);reserve.sample(10600);assert(reserve.sample(10600)==0);
  // A positive reserve estimate cannot stay stuck at 0% after stable recovery.
  for(unsigned i=0;i<120;++i)reserve.sample(10750);
  assert(reserve.shown==10);
  Run gap;gap.sample(11125);gap.filter.invalidate();gap.now+=3600000;
  assert(gap.sample(11650)==50&&gap.filter.riseHeld());
  gap.filter.invalidate();gap.now+=100000;
  assert(gap.sample(10600)!=0); // Outage did not count as low-voltage duration.
  Run capture;
  for(auto mv:NOT_CHARGING_CAPTURE_MV)capture.sample(mv);
  assert(capture.shown==40&&capture.filter.smoothedPercent()>=35&&capture.filter.smoothedPercent()<=50);
  Run normal,wrap;wrap.now=0xffff0000;
  for(unsigned i=0;i<48*3600;++i){
    const auto mv=i%3600==600?11650u:11125u+int(i%21)-10;
    assert(normal.sample(mv)==wrap.sample(mv));
    assert(normal.shown==50);
  }
  // Exercise maximum validated voltage span at the capped 2s arithmetic step.
  Run extremes;for(unsigned i=0;i<10000;++i)extremes.sample(i%2?8000:15000,2000);
  std::cout<<"PASS symmetric voltage smoothing: no downward ratchet under +/-100/200mV cycles or seeded noise; recorded 423-point trace; slow recovery/discharge, transient/persistent rises, reserve/recovery, outages, extremes, rollover and 48-hour host run\n";
}
