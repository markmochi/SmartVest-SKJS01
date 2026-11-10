#pragma once
#include "BatteryProtocol.h"

// Presentation filter, not a fuel gauge or charging detector. Only valid
// samples enter this class; raw voltage/current remain unchanged in telemetry.
class BatteryDisplay {
 public:
  static constexpr uint32_t RISE_HOLD_MS=60000;
  static constexpr uint32_t VOLTAGE_TAU_MS=30000;
  // Match Main's source voltage (VBUS plus the signed shunt drop), rounded
  // to millivolts. Inputs have already passed Nano's measurement checks.
  static uint32_t sourceMillivolts(uint32_t busMV,int32_t shuntUV){
    return uint32_t((int32_t(busMV)*1000+shuntUV+500)/1000);
  }
  uint8_t update(uint32_t mv,uint32_t now){
    const uint8_t raw=BatteryProtocol::estimate3S(mv);
    if(!seen){
      seen=true;seedWindow(mv);voltageQ=int32_t(mv)*MV_SCALE;level=int32_t(raw)*UNITS;
      shown=raw==0?0:raw==100?100:uint8_t(((raw+5)/10)*10);
      if(raw&&shown==0)shown=10;if(raw<100&&shown==100)shown=90;
      previousAt=now;previousMedian=uint16_t(mv);resume=false;return shown;
    }
    uint32_t elapsed=uint32_t(now-previousAt);previousAt=now;
    // Neither missing samples nor an outage earn percentage movement credit.
    if(resume||elapsed>=BatteryProtocol::STALE_MS){
      seedWindow(mv);elapsed=0;resume=false;hold=true;holdSince=now;
      riseLimited=true;riseReference=uint16_t((voltageQ+MV_SCALE/2)/MV_SCALE);
      previousMedian=uint16_t(mv);
      warningRun=criticalRun=emptyRun=emptyRecoveryRun=0;
    }else {window[index]=uint16_t(mv);index=(index+1)%5;}
    const uint16_t medianMV=median();
    // Detect a persistent step in the median, not isolated raw spikes. Repeated
    // noise cannot keep restarting the same hold on an elevated plateau.
    if(!riseLimited&&uint32_t(medianMV)>=uint32_t(previousMedian)+250&&
       int32_t(medianMV)*MV_SCALE-voltageQ>=250*MV_SCALE){
      hold=riseLimited=true;holdSince=now;
      riseReference=uint16_t((voltageQ+MV_SCALE/2)/MV_SCALE);
    }
    previousMedian=medianMV;
    if(hold&&uint32_t(now-holdSince)>=RISE_HOLD_MS)hold=false;
    if(elapsed>2000)elapsed=2000;
    // Symmetric filtering in voltage space avoids the old 12x downward
    // percentage bias and avoids averaging across different curve slopes.
    // Maximum product at the validated 8..15 V / <=2 s input bounds is <2^31.
    const int32_t voltageTarget=int32_t(medianMV)*MV_SCALE;
    voltageQ+=(voltageTarget-voltageQ)*int32_t(elapsed)/int32_t(VOLTAGE_TAU_MS+elapsed);
    if(riseLimited&&elapsed&&uint32_t(medianMV)<=uint32_t(riseReference)+100&&
       voltageQ<=(int32_t(riseReference)+100)*MV_SCALE){
      // End the step guard only once raw and filtered voltage have recovered.
      // Never snap the symmetric filter down to a noise trough.
      riseLimited=hold=false;
    }
    const int32_t target=int32_t(BatteryProtocol::estimate3S(uint32_t((voltageQ+MV_SCALE/2)/MV_SCALE)))*UNITS;
    if(target>level&&!hold){
      // Normal recovery has the same rate as decline. A large elevated step
      // alone cannot earn a rapid increase: one point/minute after its hold.
      const int32_t step=int32_t(elapsed)*(riseLimited?1:12);
      level+=step<target-level?step:target-level;
    }else if(target<level){
      // One point per five seconds, with sustained low-voltage bypass.
      const int32_t step=int32_t(elapsed)*12;level-=step<level-target?step:level-target;
    }
    count(mv<=10810,warningRun);count(mv<=10705,criticalRun);
    count(mv<=BatteryProtocol::DISPLAY_EMPTY_MV,emptyRun);
    count(mv>=10750,emptyRecoveryRun);
    if(warningRun>=3&&level>20*UNITS)level=20*UNITS;
    if(criticalRun>=3&&level>10*UNITS)level=10*UNITS;
    if(emptyRun>=3){
      level=0;shown=0;
      const int32_t emptyQ=int32_t(mv)*MV_SCALE;
      if(voltageQ>emptyQ)voltageQ=emptyQ;
      return shown;
    }
    // Recover the minimum positive display after sustained recovery above the
    // reserve boundary; do not leave 0% stuck until the 7-point boundary.
    if(shown==0&&level>0&&emptyRecoveryRun>=3)shown=10;
    if(riseLimited&&!hold&&level>=target)riseLimited=false;
    // +/-2 points around nearest-ten boundaries prevents 80/90 flicker.
    while(shown<90&&level>=int32_t(shown+7)*UNITS)shown+=10;
    if(shown==90&&level==100*UNITS&&target==100*UNITS)shown=100;
    while(shown>10&&level<=int32_t(shown-7)*UNITS)shown-=10;
    return shown;
  }
  void invalidate(){resume=true;warningRun=criticalRun=emptyRun=emptyRecoveryRun=0;}
  uint8_t smoothedPercent()const{return uint8_t(level/UNITS);}
  bool riseHeld()const{return hold;}
 private:
  static constexpr int32_t UNITS=60000;
  static constexpr int32_t MV_SCALE=128;
  uint16_t window[5]={};uint8_t index=0,shown=0;
  uint8_t warningRun=0,criticalRun=0,emptyRun=0,emptyRecoveryRun=0;
  bool seen=false,resume=false,hold=false,riseLimited=false;
  uint16_t previousMedian=0,riseReference=0;
  int32_t level=0,voltageQ=0;uint32_t previousAt=0,holdSince=0;
  static void count(bool low,uint8_t& run){if(!low)run=0;else if(run<3)++run;}
  void seedWindow(uint32_t mv){for(auto& v:window)v=uint16_t(mv);index=0;}
  uint16_t median()const{
    uint16_t sorted[5];for(uint8_t i=0;i<5;++i)sorted[i]=window[i];
    for(uint8_t i=1;i<5;++i){uint16_t v=sorted[i];uint8_t j=i;
      while(j&&sorted[j-1]>v){sorted[j]=sorted[j-1];--j;}sorted[j]=v;}
    return sorted[2];
  }
};
