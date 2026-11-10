#include "../../Main_and_Cameras/SmartVest_SJKS01_Main/src/power/BatteryLedTest.h"
#include <cassert>
#include <iostream>
int main(){
 BatteryLedTest t;
 assert(!t.active(0));
 for(const char* s:{"full","low","critical","charging","blue","off"}){
  assert(t.start(s,100)); assert(t.active(100)); assert(!std::strcmp(t.name(100),s));
  assert(t.remaining(100)==15000); assert(t.active(15099));
  t.tick(15100);assert(!t.active(15100));assert(t.remaining(15100)==0);
 }
 assert(t.start("full",0));assert(!t.start("bad",1));assert(t.state()==BatteryIndicator::State::Full);
 assert(!t.start(nullptr,1));assert(t.start("stop",2));assert(!t.active(2));
 assert(t.start("blue",0xfffffff0u));assert(t.active(100));t.tick(14984);assert(!t.active(14984));
 t.tick(0xfffffff1u);assert(!t.active(0xfffffff1u));
 std::cout<<"PASS LED override modes, validation, stop, expiry, timer wrap, no resurrection\n";
}
