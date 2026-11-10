#pragma once
#include <stdint.h>
#include <map>
struct TestWire {
  std::map<uint8_t,uint16_t> regs={{0xfe,0x5449},{0xff,0x2260},{2,9600},{4,16384},{3,5243},{1,10000},{6,8}};
  bool connected=true,shortRead=false,repeatedStartShort=false,readTimeout=false;
  bool pointerStopped=false,timeoutFlag=false;uint8_t reg=0,index=0,received=0;
  unsigned requests=0,configureDelay=0;
  void begin(){} void setClock(unsigned long){} void setWireTimeout(unsigned long,bool){}
  void clearWireTimeoutFlag(){timeoutFlag=false;} bool getWireTimeoutFlag(){return timeoutFlag;}
  void beginTransmission(uint8_t){index=0;} void write(uint8_t r){reg=r;}
  uint8_t endTransmission(bool stop=true){pointerStopped=stop;return connected?0:2;}
  uint8_t requestFrom(uint8_t,uint8_t){++requests;index=0;timeoutFlag=readTimeout;received=readTimeout?0:(shortRead||(!pointerStopped&&repeatedStartShort)?1:2);return received;}
  int available(){return received-index;}
  int read(){return index++?regs[reg]&255:regs[reg]>>8;}
};
inline TestWire Wire;
