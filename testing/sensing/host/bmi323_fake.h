#pragma once
#include "SPI.h"
#include <array>
#include <cassert>

// Byte-level SPI/NVS test doubles. This does not model electrical behavior,
// sensor noise, latency guarantees, RF traffic or real ESP32 task scheduling.
struct FakeBMI323 {
  inline static FakeBMI323* active=nullptr;
  std::array<uint16_t,128> regs{};
  std::vector<std::vector<uint8_t>> transactions;
  std::vector<uint8_t> bytes;
  std::array<int16_t,3> accel{0,0,2048},gyro{16,-32,8};
  int16_t temp=4096;
  uint32_t epoch=0,origin=0,forcedTime=128;
  uint16_t forcedStatus=0,forcedError=0,saturation=0;
  uint16_t burst[10]{};
  bool wireTiming=false;
  bool selected=false,spiActive=false,automatic=false,allHigh=false,ignoreAccWrite=false;
  unsigned transientIdReads=0,transientAccConfigReads=0;
  bool read=false,pendingReset=false;
  uint8_t address=0;unsigned index=0,resets=0;
  int mode=0;uint32_t stopAfter=0;
  uint8_t low=0;
  FakeBMI323(){defaults();active=this;testDigitalWrite=selectHook;SPI.byteHook=byteHook;}
  void defaults(){regs.fill(0);regs[0]=0x0043;regs[0x20]=0x0028;regs[0x21]=0x0048;forcedStatus=1;epoch=0;origin=micros();}
  static void selectHook(int pin,int value){if(active&&pin==21)active->select(value);}
  static uint8_t byteHook(uint8_t value){assert(active);return active->transfer(value);}
  void select(int value){
    if(value==LOW){assert(!selected);selected=true;bytes.clear();index=0;pendingReset=false;}
    else if(selected){
      if(!bytes.empty())transactions.push_back(bytes);
      selected=false;
      if(pendingReset){defaults();spiActive=false;resets++;}
      else spiActive=true;
    }
  }
  void ready(uint32_t time,uint16_t status=0x00C0){automatic=false;forcedTime=time;forcedStatus=status;}
  void update(){
    if(!automatic||regs[0x20]!=0x70B9||regs[0x21]!=0x70C9)return;
    uint32_t next=(micros()-origin)/5000;
    if(stopAfter&&next>stopAfter)next=stopAfter;
    if(next>epoch){epoch=next;forcedTime=(epoch*128)&0x3FFFFFFF;forcedStatus|=0x00C0;if(epoch%4==0)forcedStatus|=0x0020;}
  }
  uint16_t word(uint8_t reg){
    if(reg==0&&transientIdReads){transientIdReads--;return 0x1141;}
    if(reg==0x20&&transientAccConfigReads){transientAccConfigReads--;return 0x7001;}
    if(reg==1)return forcedError;
    if(reg==2){update();uint16_t result=forcedStatus;forcedStatus=0;return result;}
    if(reg==3)latch();
    if(reg>=3&&reg<=12)return burst[reg-3];
    return regs[reg];
  }
  void latch(){
    auto a=accel,g=gyro;
    if(mode==1)g={160,0,0}; // persistent >5 dps rotation must not become zero-rate bias
    if(mode==2)g={int16_t(epoch%2?160:-160),0,0};
    if(mode==3){a={int16_t(epoch%2?1024:-1024),0,1774};} // nearly constant magnitude, changing axis direction
    for(int i=0;i<3;i++){burst[i]=uint16_t(a[i]);burst[i+3]=uint16_t(g[i]);}
    burst[6]=uint16_t(temp);burst[7]=uint16_t(forcedTime);burst[8]=uint16_t(forcedTime>>16);burst[9]=saturation;
  }
  void write(uint8_t reg,uint16_t value){
    if(reg==0x7E&&value==0xDEAF){pendingReset=true;return;}
    if(reg==0x20&&ignoreAccWrite)return;
    regs[reg]=value;
    if(reg==0x21){origin=micros();epoch=0;}
  }
  uint8_t transfer(uint8_t value){
    assert(selected);if(wireTiming)delayMicroseconds(80);bytes.push_back(value);unsigned pos=index++;
    if(pos==0){address=value&0x7F;read=value&0x80;if(read&&address==3)latch();return 0;}
    if(allHigh)return 0xFF;
    if(!spiActive)return 0;
    if(read){
      if(pos==1){assert(value==0);return 0xA5;} // distinguish dummy from payload
      assert(value==0);unsigned offset=pos-2;
      if(!(offset&1)){cached=word(address+offset/2);return uint8_t(cached);}
      return uint8_t(cached>>8);
    }
    unsigned offset=pos-1;if(!(offset&1))low=value;else write(address+offset/2,uint16_t(low)|(uint16_t(value)<<8));return 0;
  }
  uint16_t cached=0;
  void checkProtocol(){
    assert(!selected&&SPI.clock==100000&&SPI.mode==0&&SPI.order==MSBFIRST);
    for(const auto& t:transactions){
      if(t[0]&0x80){assert(t.size()>=4&&t.size()%2==0);assert(t[1]==0);}
      else assert(t.size()>=3&&t.size()%2==1);
    }
  }
};
