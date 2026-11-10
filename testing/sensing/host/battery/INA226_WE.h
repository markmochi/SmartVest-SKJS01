#pragma once
#include "Wire.h"
constexpr uint16_t INA226_AVERAGE_16=0x400,INA226_CONTINUOUS=7;
struct INA226_WE {
  explicit INA226_WE(uint8_t){}
  bool init(){clockMs+=Wire.configureDelay;Wire.regs[0]=0x4127;return Wire.connected;}
  void setResistorRange(float ohms,float amps){Wire.regs[5]=uint16_t(0.00512f/((amps/32768)*ohms));}
  void setAverage(uint16_t n){Wire.regs[0]=(Wire.regs[0]&~0xe00)|n;}
  void setMeasureMode(uint16_t n){Wire.regs[0]=(Wire.regs[0]&~7)|n;}
};
