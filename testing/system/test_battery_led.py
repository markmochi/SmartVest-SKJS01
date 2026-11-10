"""Run the real LED driver, percentage policy, and timed manual override on the host."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as tmp:
    for name in ['test_battery_indicator','test_battery_led','test_led_override','test_crash_led']:
        out=Path(tmp)/name
        subprocess.run(['g++','-std=c++17','-UNDEBUG','-DARDUINO_ARCH_ESP32=1',
            '-DCONFIG_IDF_TARGET_ESP32=1','-I'+str(r/'led_host'),
            str(r/(name+'.cpp')),'-o',str(out)],check=True)
        subprocess.run([str(out)],check=True)
    driver=r.parents[1]/'Main_and_Cameras/SmartVest_SJKS01_Main/src/power'
    for variant in ['anode','disabled','boot']:
        config=(driver/'../../Config.h').resolve().read_text(encoding='utf8')
        if variant=='anode':config=config.replace('LED_COMMON_ANODE = false','LED_COMMON_ANODE = true')
        if variant=='disabled':config=config.replace('ENABLE_BATTERY_LED = true','ENABLE_BATTERY_LED = false')
        if variant=='boot':config=config.replace('BATTERY_LED_BOOT_TEST = false','BATTERY_LED_BOOT_TEST = true')
        p=Path(tmp);(p/'Config.h').write_text(config,encoding='utf8')
        (p/'BatteryLed.h').write_text((driver/'BatteryLed.h').read_text(encoding='utf8').replace('../../Config.h','Config.h'),encoding='utf8')
        (p/'variant.cpp').write_text(r'''
#include "BatteryLed.h"
#include <cassert>
int main(){
 BatteryLed led;led.begin();CrashLedSignal crash;crash.detected(1,1,0);
 if(!Config::ENABLE_BATTERY_LED){auto before=writes;led.tick(BatteryIndicator::State::Full,true,true,&crash);assert(writes==before);return 0;}
 const int on=Config::LED_COMMON_ANODE?LOW:HIGH,off=Config::LED_COMMON_ANODE?HIGH:LOW;
 assert(testMillis==3000&&delays.size()==12);
 for(unsigned i=0;i<delays.size();++i){
  const auto& sample=delays[i];const int expected=i%2?off:on;
  assert(sample.at==i*250&&sample.duration==250);
  assert(sample.red==expected&&sample.green==expected&&sample.blue==expected);
 }
 led.tick(BatteryIndicator::State::Full,false,false,&crash);
 assert(levels[23]==on&&pwm[18]==(Config::LED_COMMON_ANODE?191:64)&&levels[19]==off);
 testMillis=250;led.tick(BatteryIndicator::State::Full,false,false,&crash);
 assert(levels[23]==off&&pwm[18]==(Config::LED_COMMON_ANODE?255:0));
 testMillis=5000;led.tick(BatteryIndicator::State::Unknown,false,false,&crash);
 assert(levels[23]==off&&pwm[18]==(Config::LED_COMMON_ANODE?255:0)&&levels[19]==off);
}
''',encoding='utf8')
        out=p/variant
        subprocess.run(['g++','-std=c++17','-UNDEBUG','-DARDUINO_ARCH_ESP32=1','-DCONFIG_IDF_TARGET_ESP32=1',
            '-I'+str(r/'led_host'),'-I'+str(driver),'-I'+str(driver.parent.parent),str(p/'variant.cpp'),'-o',str(out)],check=True)
        subprocess.run([str(out)],check=True)
    print('PASS common-anode polarity, disabled master gate and crash preemption of boot test')
