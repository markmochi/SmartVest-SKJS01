#include "../../Main_and_Cameras/SmartVest_SJKS01_Main/src/power/BatteryLed.h"
#include <cassert>
#include <iostream>
int main(){
 BatteryLed led; led.begin();
 assert(testMillis==3000&&delays.size()==12);
 for(unsigned i=0;i<delays.size();++i){
   const auto& sample=delays[i];const int expected=i%2?LOW:HIGH;
   assert(sample.at==i*250&&sample.duration==250);
   assert(sample.red==expected&&sample.green==expected&&sample.blue==expected);
 }
 assert(modes[23]==OUTPUT && modes[18]==OUTPUT && modes[19]==OUTPUT);
 assert(levels[23]==LOW && levels[18]==LOW && levels[19]==LOW);
 // Critical red alternates without touching green or blue, including wrap.
 for(uint32_t now:{0u,249u,250u,499u,500u,0xfffffff0u,16u}){
   testMillis=now;led.tick(BatteryIndicator::State::Critical);
   assert(levels[23]==((now/250)%2==0?HIGH:LOW));
   assert(levels[18]==LOW&&levels[19]==LOW);
 }
 testMillis=0;
 led.tick(BatteryIndicator::State::Normal);assert(levels[23]==LOW&&levels[18]==LOW&&levels[19]==LOW);
 led.tick(BatteryIndicator::State::Low); assert(levels[23]==HIGH && levels[18]==LOW);
 led.tick(BatteryIndicator::State::Charging); assert(levels[23]==LOW && levels[18]==HIGH);
 testMillis=500; led.tick(BatteryIndicator::State::Charging); assert(levels[18]==LOW);
 led.tick(BatteryIndicator::State::Full); assert(levels[18]==HIGH && levels[19]==LOW);
 led.tick(BatteryIndicator::State::Unknown); assert(levels[23]==LOW && levels[18]==LOW);
 led.tick(BatteryIndicator::State::Unknown,true,true); assert(levels[19]==HIGH && levels[23]==LOW && levels[18]==LOW);
 led.tick(BatteryIndicator::State::Unknown,false,true); assert(levels[19]==LOW);
 // Reboot indication also completes across the millisecond counter wrap.
 delays.clear();testMillis=0xffffff00u;BatteryLed reboot;reboot.begin();
 assert(testMillis==uint32_t(0xffffff00u+3000u)&&delays.size()==12);
 assert(levels[23]==LOW&&levels[18]==LOW&&levels[19]==LOW);
 reboot.tick(BatteryIndicator::State::Full);assert(levels[18]==HIGH&&levels[23]==LOW&&levels[19]==LOW);
 std::cout<<"PASS six white startup flashes over 3000 ms, all-off handover, timer wrap and normal indicator restoration\n";
 std::cout<<"PASS actual LED driver pin setup, red/green outputs, blinking, unknown off\n";
}
