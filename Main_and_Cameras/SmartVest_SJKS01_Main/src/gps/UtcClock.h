#pragma once
#include <stdint.h>
namespace UtcClock {
constexpr uint64_t MIN_MS=1577836800000ULL,MAX_MS=4102444800000ULL,HOLD_MS=86400000ULL;
inline bool valid(uint64_t ms){return ms>=MIN_MS && ms<MAX_MS;}
inline uint64_t fromDate(unsigned y,unsigned m,unsigned d,unsigned h,unsigned min,unsigned sec,unsigned centis){
  if(y<2020||y>2099||m<1||m>12||h>23||min>59||sec>59||centis>99)return 0;
  const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap=(y%4==0 && (y%100!=0 || y%400==0));
  if(d<1||d>days[m-1]+(m==2&&leap?1U:0U))return 0;
  int year=int(y)-(m<=2);const int era=year/400;
  const unsigned yoe=unsigned(year-era*400),mp=m>2?m-3:m+9;
  const unsigned doy=(153*mp+2)/5+d-1,doe=yoe*365+yoe/4-yoe/100+doy;
  const int64_t civil=int64_t(era)*146097+doe-719468;
  return (uint64_t(civil)*86400+h*3600+min*60+sec)*1000+centis*10;
}
struct Anchor {
  uint64_t epoch=0,at=0;uint8_t source=0; // 1 GPS, 2 developer device
  void set(uint64_t utc,uint64_t now,uint8_t kind){epoch=utc;at=now;source=kind;}
  uint64_t get(uint64_t now)const{return valid(epoch)&&now>=at&&now-at<HOLD_MS?epoch+now-at:0;}
};
}
