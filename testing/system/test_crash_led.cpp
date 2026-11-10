#include "../../Main_and_Cameras/SmartVest_SJKS01_Main/src/power/BatteryLed.h"
#include <cassert>
#include <iostream>
using P=CrashLedSignal::Pattern;
int main(){
 CrashLedSignal s;assert(s.tick(0)==P::Off);
 s.sent(1,1,0);assert(s.tick(0)==P::Off);
 s.detected(0,1,0);assert(s.tick(0)==P::Off);
 s.detected(1,1,100);assert(s.tick(100)==P::Blink&&s.lit(100));
 assert(s.tick(349)==P::Blink&&s.lit(349));assert(!s.lit(350));
 s.detected(1,1,4000);assert(s.tick(5099)==P::Blink);assert(s.tick(5100)==P::Off);
 s.sent(1,1,6000);assert(s.tick(6000)==P::Steady&&s.lit(6000));
 s.sent(1,1,10000);assert(s.tick(10999)==P::Steady);assert(s.tick(11000)==P::Off);
 s.detected(1,2,12000);s.sent(1,1,12001);assert(s.tick(12001)==P::Blink);
 s.sent(1,2,12040);assert(s.tick(12040)==P::Steady);assert(s.tick(17040)==P::Off);
 s.detected(2,1,0xfffffff0u);assert(s.tick(233)==P::Blink&&s.lit(233));
 assert(!s.lit(234));assert(s.tick(4983)==P::Blink);assert(s.tick(4984)==P::Off);
 assert(s.tick(0xfffffff1u)==P::Off);
 s.sent(2,1,0xfffffff0u);assert(s.tick(4983)==P::Steady);assert(s.tick(4984)==P::Off);
 assert(s.tick(0xfffffff1u)==P::Off);
 CrashLedSignal reboot;reboot.sent(2,1,100);assert(reboot.tick(100)==P::Off);
 CrashLedSignal overlap;overlap.detected(9,1,0);overlap.sent(9,1,10);
 overlap.detected(9,2,11);assert(overlap.tick(11)==P::Blink);
 overlap.sent(9,1,12);assert(overlap.tick(12)==P::Blink);

 BatteryLed led;testMillis=100;led.begin();assert(led.colorReady());
 CrashLedSignal alert;alert.detected(3,1,100);
 auto update=[&](uint32_t now){testMillis=now;led.tick(BatteryIndicator::State::Critical,true,true,&alert);};
 update(100);assert(levels[23]==HIGH&&pwm[18]==64&&levels[19]==LOW);
 const auto unchanged=writes;update(101);assert(writes==unchanged);
 update(350);assert(levels[23]==LOW&&pwm[18]==0&&levels[19]==LOW);
 alert.sent(3,1,351);update(351);assert(levels[23]==HIGH&&pwm[18]==64&&levels[19]==LOW);
 update(5350);assert(pwm[18]==64);update(5351);assert(pwm[18]==0&&levels[19]==HIGH);
 led.tick(BatteryIndicator::State::Full,false,false,&alert);assert(pwm[18]==255&&levels[19]==LOW);
 led.tick(BatteryIndicator::State::Unknown,false,false,&alert);assert(pwm[18]==0&&levels[23]==LOW);
 attachOk=false;BatteryLed failed;failed.begin();assert(!failed.colorReady());
 failed.tick(BatteryIndicator::State::Full);assert(levels[18]==HIGH);
 std::cout<<"PASS crash/sent 5s timers, 250ms blink, early/late success, identity/dedup, rollover, reboot, orange hardware brightness, priority, restore, cached writes and brightness failure fallback\n";
}
