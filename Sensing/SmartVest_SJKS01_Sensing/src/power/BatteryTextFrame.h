#pragma once
#include "BatteryProtocol.h"

// Streaming decoder for Nano's readable USB/UART line. Only the exact |B1:
// marker, 80 hex digits, CR/LF and a valid inner packet can publish a sample.
// No String, heap allocation, or unbounded line buffer; old raw packets remain
// supported independently by NanoBattery's existing binary parser.
class BatteryTextFrame {
 public:
  uint32_t rejected=0;
  bool feed(uint8_t b,uint32_t now,BatteryProtocol::Reading& out){
    if((prefix||active)&&uint32_t(now-lastByte)>=100)reset();
    lastByte=now;
    if(!active){
      const char* marker="|B1:";
      if(b==uint8_t(marker[prefix])){if(++prefix==4){prefix=0;active=true;}}
      else prefix=b=='|'?1:0;
      return false;
    }
    if(digits==BatteryProtocol::SIZE*2){
      if(b=='\r'&&!carriage){carriage=true;return false;}
      const bool complete=b=='\n';
      reset();
      if(complete&&BatteryProtocol::decode(bytes,out))return true;
      ++rejected;prefix=b=='|'?1:0;return false;
    }
    const int8_t n=hex(b);
    if(n<0){++rejected;reset();prefix=b=='|'?1:0;return false;}
    if(!(digits&1))bytes[digits/2]=uint8_t(n)<<4;
    else bytes[digits/2]|=uint8_t(n);
    ++digits;return false;
  }
 private:
  uint8_t bytes[BatteryProtocol::SIZE]={},prefix=0,digits=0;
  bool active=false,carriage=false;
  uint32_t lastByte=0;
  void reset(){prefix=0;digits=0;active=false;carriage=false;}
  static int8_t hex(uint8_t b){
    if(b>='0'&&b<='9')return b-'0';
    if(b>='A'&&b<='F')return b-'A'+10;
    if(b>='a'&&b<='f')return b-'a'+10;
    return -1;
  }
};
