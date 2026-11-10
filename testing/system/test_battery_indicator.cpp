#include "../../Main_and_Cameras/SmartVest_SJKS01_Main/src/power/BatteryIndicator.h"
#include <cassert>
#include <iostream>
using namespace BatteryIndicator;
int main() {
  Policy p;
  for (int value=0;value<=100;++value) {
    p.updatePercent(true,value,10,30);
    const State expected=value<=10?State::Critical:value<=30?State::Low:State::Normal;
    assert(p.state==expected);
    if(value>30){const auto light=output(p.state,0,250,500);assert(!light.red&&!light.green);}
    p.updatePercent(false,value,10,30);assert(p.state==State::Unknown);
  }
  for(int invalid:{-1,101,255}){p.updatePercent(true,invalid,10,30);assert(p.state==State::Unknown);}
  for(auto thresholds:{std::pair<int,int>{-1,30},{10,10},{30,10},{10,100}}){p.updatePercent(true,20,thresholds.first,thresholds.second);assert(p.state==State::Unknown);}
  assert(output(State::Critical,0,250,500).red);
  assert(!output(State::Critical,250,250,500).red);
  assert(output(State::Charging,0,250,500).green);
  assert(!output(State::Charging,500,250,500).green);
  assert(output(State::Full,500,250,500).green);
  assert(!output(State::Unknown,0,250,500).green);
  std::cout<<"PASS percentage states 0..100, invalid readings/thresholds and diagnostic blink outputs\n";
}
