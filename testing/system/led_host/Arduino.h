#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
constexpr int HIGH=1, LOW=0, OUTPUT=1;
inline uint32_t testMillis=0;
inline int levels[40]={}, modes[40]={};
inline int pwm[40]={};
inline unsigned writes=0;
inline bool attachOk=true;
inline uint32_t millis(){return testMillis;}
struct DelaySample { uint32_t at, duration; int red, green, blue; };
inline std::vector<DelaySample> delays;
inline void delay(uint32_t duration){
 delays.push_back({testMillis,duration,levels[23],levels[18],levels[19]});
 testMillis+=duration;
}
inline void digitalWrite(int p,int v){levels[p]=v;++writes;}
inline void pinMode(int p,int v){modes[p]=v;}
inline bool sigmaDeltaAttach(int p,uint32_t hz){return p==18&&hz==312500&&attachOk;}
inline bool sigmaDeltaWrite(int p,uint32_t v){pwm[p]=v;levels[p]=v?HIGH:LOW;++writes;return true;}
