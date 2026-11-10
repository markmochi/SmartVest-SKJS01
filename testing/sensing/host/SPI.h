#pragma once
#include "Arduino.h"
constexpr int HIGH=1,LOW=0,OUTPUT=1,INPUT=0,MSBFIRST=1,SPI_MODE0=0;
inline void(*testDigitalWrite)(int,int)=nullptr;
inline int testPinModes[64]{},testPinLevels[64]{};
inline void digitalWrite(int pin,int value){if(testPinModes[pin]!=OUTPUT)return;testPinLevels[pin]=value;if(testDigitalWrite)testDigitalWrite(pin,value);}
inline void pinMode(int pin,int mode){testPinModes[pin]=mode;}
struct SPISettings{uint32_t clock;int order,mode;SPISettings(uint32_t c,int o,int m):clock(c),order(o),mode(m){}};
struct SPIClass{int sck=0,miso=0,mosi=0,cs=0;uint32_t clock=0;int mode=-1,order=-1;
 uint8_t(*byteHook)(uint8_t)=nullptr;
 bool beginOK=true;unsigned beginCount=0,endCount=0;
 bool csHighAtBegin=false;
 bool begin(int a,int b,int c,int d){sck=a;miso=b;mosi=c;cs=d;beginCount++;csHighAtBegin=testPinModes[d]==OUTPUT&&testPinLevels[d]==HIGH;return beginOK;}
 void end(){endCount++;}
 void beginTransaction(const SPISettings& s){clock=s.clock;mode=s.mode;order=s.order;}
 uint8_t transfer(uint8_t value){return byteHook?byteHook(value):0;}
 void transferBytes(const uint8_t* tx,uint8_t* rx,uint32_t n){for(uint32_t i=0;i<n;i++)rx[i]=transfer(tx[i]);}
 void endTransaction(){}
};inline SPIClass SPI;
