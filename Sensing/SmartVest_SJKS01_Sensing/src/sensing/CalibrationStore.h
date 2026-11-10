#pragma once
#include <Preferences.h>
#include <cmath>
#include <cstring>
#include "../link/CrashProtocol.h"
// Reuse saved bias only after model, profile, CRC and finite/range validation.
class CalibrationStore {
 public:
  bool valid=false,stored=false;float bias[3]={};uint16_t deviceId=0;
  bool load(uint16_t id){
    valid=stored=false;Preferences p;if(!p.begin("sv_bmi_gyro",true))return false;
    uint8_t b[SIZE]={};bool ok=p.getBytesLength("bias")==SIZE&&p.getBytes("bias",b,SIZE)==SIZE;p.end();
    valid=stored=ok&&decode(b,id);return valid;
  }
  bool save(uint16_t id,const float* values){
    valid=false;if((id&255)!=0x43)return false;
    uint8_t b[SIZE]={},check[SIZE]={};b[0]='B';b[1]='G';b[2]=1;
    CrashProtocol::put16(b+4,id);CrashProtocol::put16(b+6,0x70B9);CrashProtocol::put16(b+8,0x70C9);
    for(unsigned i=0;i<3;++i){if(!std::isfinite(values[i])||std::fabs(values[i])>10)return false;std::memcpy(b+10+i*4,values+i,4);}
    CrashProtocol::put16(b+22,CrashProtocol::crc16(b,22));Preferences p;if(!p.begin("sv_bmi_gyro",false))return false;
    bool ok=p.putBytes("bias",b,SIZE)==SIZE&&p.getBytesLength("bias")==SIZE&&p.getBytes("bias",check,SIZE)==SIZE&&!std::memcmp(b,check,SIZE);p.end();
    valid=stored=ok&&decode(check,id);return valid;
  }
 private:
  static constexpr unsigned SIZE=24;
  bool decode(const uint8_t* b,uint16_t id){
    if((id&255)!=0x43||b[0]!='B'||b[1]!='G'||b[2]!=1||b[3]||CrashProtocol::get16(b+4)!=id||CrashProtocol::get16(b+6)!=0x70B9||CrashProtocol::get16(b+8)!=0x70C9||CrashProtocol::get16(b+22)!=CrashProtocol::crc16(b,22))return false;
    float values[3];for(unsigned i=0;i<3;++i){std::memcpy(values+i,b+10+i*4,4);if(!std::isfinite(values[i])||std::fabs(values[i])>10)return false;}
    std::memcpy(bias,values,sizeof(bias));deviceId=id;return true;
  }
};
