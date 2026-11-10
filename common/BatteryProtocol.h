#pragma once
#ifndef SMARTVEST_BATTERY_PROTOCOL_V1_H
#define SMARTVEST_BATTERY_PROTOCOL_V1_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Nano UART packet and the payload of the Sensing -> Main V2 Battery message.
// Fixed memory, explicit little endian, CRC16-CCITT. No String/JSON on the Nano.
namespace BatteryProtocol {
constexpr size_t SIZE=40;
constexpr uint32_t STALE_MS=6000;
enum Flag : uint8_t { Responding=1, Valid=2, Overflow=4 };
enum Profile : uint8_t { Unknown=0, LiIon3SApproximate=1 };
enum Charge : uint8_t { ChargeUnknown=0, Charging=1, Discharging=2, Idle=3, VoltageCharging=4, VoltageNotCharging=5 };
inline bool measuredCharge(uint8_t s){return s>=Charging&&s<=Idle;}
inline bool voltageCharge(uint8_t s){return s==VoltageCharging||s==VoltageNotCharging;}
inline bool isCharging(uint8_t s){return s==Charging||s==VoltageCharging;}
struct Reading {
  uint32_t sequence=0, uptime=0, age=UINT32_MAX, busMV=0, powerMW=0;
  int32_t currentMA=0, shuntUV=0;
  uint8_t flags=0, percent=255, profile=0, error=0;
  uint8_t charge=ChargeUnknown;
  int16_t netCurrentMA=0; // Positive into battery; meaningful only for measuredCharge(), never voltage estimates.
};
inline void put32(uint8_t* p,uint32_t n){for(uint8_t i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
inline uint32_t get32(const uint8_t* p){uint32_t n=0;for(uint8_t i=0;i<4;++i)n|=uint32_t(p[i])<<(8*i);return n;}
inline uint16_t crc16(const uint8_t* p,size_t n){uint16_t c=0xffff;while(n--){c^=uint16_t(*p++)<<8;for(uint8_t i=0;i<8;++i)c=(c&0x8000)?uint16_t((c<<1)^0x1021):uint16_t(c<<1);}return c;}
inline void encode(const Reading& r,uint8_t* p){
  memset(p,0,SIZE);p[0]=0xb6;p[1]=0x26;p[2]=3;p[3]=r.flags;
  put32(p+4,r.sequence);put32(p+8,r.uptime);put32(p+12,r.age);
  put32(p+16,r.busMV);put32(p+20,uint32_t(r.currentMA));put32(p+24,r.powerMW);put32(p+28,uint32_t(r.shuntUV));
  p[32]=r.percent;p[33]=r.profile;p[34]=r.error;p[35]=r.charge;
  p[36]=uint8_t(r.netCurrentMA);p[37]=uint8_t(uint16_t(r.netCurrentMA)>>8);
  const uint16_t c=crc16(p,SIZE-2);p[38]=uint8_t(c);p[39]=uint8_t(c>>8);
}
inline bool decode(const uint8_t* p,Reading& r){
  if(p[0]!=0xb6||p[1]!=0x26||(p[2]<1||p[2]>3)||p[3]>7||p[35]>VoltageNotCharging||
     (p[2]<3&&p[35]>Idle)||
     (p[2]==1&&(p[35]||p[36]||p[37]))||
     uint16_t(p[38]|uint16_t(p[39])<<8)!=crc16(p,SIZE-2))return false;
  Reading v;v.flags=p[3];v.sequence=get32(p+4);v.uptime=get32(p+8);v.age=get32(p+12);
  v.busMV=get32(p+16);v.currentMA=int32_t(get32(p+20));v.powerMW=get32(p+24);v.shuntUV=int32_t(get32(p+28));
  v.percent=p[32];v.profile=p[33];v.error=p[34];v.charge=p[35];
  v.netCurrentMA=int16_t(uint16_t(p[36])|(uint16_t(p[37])<<8));
  if(v.netCurrentMA < -10000||v.netCurrentMA>10000||(!measuredCharge(v.charge)&&v.netCurrentMA))return false;
  if(!v.sequence||v.profile>1||(v.percent>100&&v.percent!=255)||(!v.profile&&v.percent!=255))return false;
  if(v.flags&Valid){
    if(!(v.flags&Responding)||(v.flags&Overflow)||v.error||v.age>=STALE_MS||v.busMV<8000||v.busMV>15000||
       v.currentMA < -5000||v.currentMA>5000||v.powerMW>75000||v.shuntUV < -81920||v.shuntUV>81920)return false;
  }else if(v.percent!=255||v.charge||v.netCurrentMA)return false;
  r=v;return true;
}
// Owner-observed loaded operating range, not measured capacity or cell safety.
constexpr uint16_t DISPLAY_EMPTY_MV=10600, DISPLAY_FULL_MV=11650;
inline uint8_t estimate3S(uint32_t mv){
  if(mv<=DISPLAY_EMPTY_MV)return 0;if(mv>=DISPLAY_FULL_MV)return 100;
  return uint32_t(mv-DISPLAY_EMPTY_MV)*100/(DISPLAY_FULL_MV-DISPLAY_EMPTY_MV);
}
class Tracker {
 public:
  Reading data;
  bool seen=false;
  uint32_t receivedAt=0,accepted=0,invalid=0,duplicates=0;
  bool accept(const Reading& r,uint32_t now){
    // Repeated forwarding may only increase source age, never refresh a sample.
    if(seen&&r.sequence==data.sequence&&r.uptime==data.uptime){++duplicates;return false;}
    data=r;seen=true;receivedAt=now;++accepted;if(!(r.flags&Valid))++invalid;return true;
  }
  uint32_t age(uint32_t now)const{
    if(!seen||data.age==UINT32_MAX)return UINT32_MAX;
    const uint32_t elapsed=uint32_t(now-receivedAt);
    return elapsed>UINT32_MAX-data.age?UINT32_MAX:data.age+elapsed;
  }
  bool fresh(uint32_t now)const{return seen&&(data.flags&Valid)&&age(now)<STALE_MS;}
  Reading snapshot(uint32_t now)const{Reading r=data;r.age=age(now);if(!fresh(now)){r.flags&=uint8_t(~Valid);r.percent=255;r.charge=ChargeUnknown;r.netCurrentMA=0;}return r;}
  void reset(){seen=false;data=Reading();}
};
class Parser {
 public:
  uint32_t rejected=0;
  bool feed(uint8_t b,uint32_t now,Reading& r){
    if(used&&uint32_t(now-lastByte)>=100)used=0;lastByte=now;
    bytes[used++]=b;
    while(used&&(bytes[0]!=0xb6||(used>1&&bytes[1]!=0x26)))shift();
    if(used<SIZE)return false;
    if(decode(bytes,r)){used=0;return true;}
    ++rejected;shift();while(used&&(bytes[0]!=0xb6||(used>1&&bytes[1]!=0x26)))shift();return false;
  }
 private:
  uint8_t bytes[SIZE]={};size_t used=0;uint32_t lastByte=0;
  void shift(){--used;memmove(bytes,bytes+1,used);}
};
}

#endif
