#pragma once
#include <cstdint>

// Readback cannot prove clients can exchange packets. A previously working
// camera link can lose every association while the driver's flags stay up.
// Retry a silent outage at most three times, with increasing intervals; actual
// HTTP traffic rearms recovery. A fresh empty AP or an idle lone phone is healthy.
struct ApServiceWatchdog {
  static constexpr uint32_t SILENT_MS=45000, COOLDOWN_MS=120000;
  uint32_t lastTraffic=0,lastRestart=0,lastRecovery=0,requests=0,faultBaseline=0,recoveries=0;
  bool observed=false,peersExpected=false,hasRecovered=false;
  uint8_t silentAttempts=0;
  void observe(uint32_t now,uint32_t count,bool bothCameras,uint32_t faults){
    peersExpected=peersExpected||bothCameras;
    if(count==requests)return;
    requests=count;observed=true;lastTraffic=now;faultBaseline=faults;silentAttempts=0;
  }
  void restarted(uint32_t now){lastRestart=now;}
  bool recover(uint32_t now,uint32_t faults){
    if(!observed||silentAttempts>=3||uint32_t(now-lastTraffic)<SILENT_MS||
       uint32_t(now-lastRestart)<SILENT_MS||(!peersExpected&&uint32_t(faults-faultBaseline)<3)||
       (hasRecovered&&uint32_t(now-lastRecovery)<(COOLDOWN_MS<<(silentAttempts>1?1:0))))return false;
    ++silentAttempts;hasRecovered=true;lastRecovery=now;++recoveries;return true;
  }
};
