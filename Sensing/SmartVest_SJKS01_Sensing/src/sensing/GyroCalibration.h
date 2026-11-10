#pragma once
#include <cmath>
#include "Imu.h"
// Incremental equivalent of the reference stationary-capture quality policy.
class GyroCalibration {
 public:
  enum class Result:uint8_t {Idle,Collecting,Complete,Motion,Gap,SensorLost,Timeout};
  Result result=Result::Idle;uint16_t count=0;float bias[3]={};
  float acceleration(size_t i)const{return mean[i];}
  void start(uint32_t now,const float* previous=nullptr){*this=GyroCalibration();result=Result::Collecting;startedAt=now;if(previous){havePrior=true;for(unsigned i=0;i<3;++i)prior[i]=previous[i];}}
  void cancel(){result=Result::Idle;}
  void tick(uint32_t now,bool healthy){
    if(result!=Result::Collecting)return;
    if(!healthy)result=Result::SensorLost;
    else if(count && uint32_t(now-lastAt)>SensorConfig::CAL_MAX_GAP_MS)result=Result::Gap;
    else if(uint32_t(now-startedAt)>=SensorConfig::CAL_TIMEOUT_MS){if(count>=220)complete();else result=Result::Timeout;}
  }
  void feed(const ImuSample& s){
    if(result!=Result::Collecting)return;
    if(count && (s.intervalUs>SensorConfig::SAMPLE_GAP_US || s.deltaUs>SensorConfig::SAMPLE_GAP_US)){result=Result::Gap;return;}
    if(count && s.sequence==lastSequence)return;
    float norm2=0;for(unsigned i=0;i<3;++i){if(!std::isfinite(s.accel[i])||!std::isfinite(s.gyro[i])){result=Result::Motion;return;}norm2+=s.accel[i]*s.accel[i];}
    if(s.saturated){result=Result::Motion;return;}
    const float norm=std::sqrt(norm2);lastAt=s.atMs;lastSequence=s.sequence;++count;
    for(unsigned i=0;i<6;++i){float v=i<3?s.accel[i]:s.gyro[i-3],d=v-mean[i];mean[i]+=d/count;m2[i]+=d*(v-mean[i]);}
    float d=norm-meanMagnitude;meanMagnitude+=d/count;m2Magnitude+=d*(norm-meanMagnitude);
    if(count>=SensorConfig::GYRO_CAL_SAMPLES)complete();
  }
 private:
  bool havePrior=false;float prior[3]={};
  float mean[6]={},m2[6]={},meanMagnitude=0,m2Magnitude=0;
  uint32_t startedAt=0,lastAt=0,lastSequence=0;
  void complete(){
    float a=0,g=0,rate2=0;for(unsigned i=0;i<3;++i){a+=m2[i];g+=m2[i+3];const float rate=mean[i+3]-(havePrior?prior[i]:0);rate2+=rate*rate;}
    if(meanMagnitude<=.75f||meanMagnitude>=1.25f||std::sqrt(m2Magnitude/count)>=.04f||std::sqrt(a/count)>=.04f||std::sqrt(g/count)>=2||std::sqrt(rate2)>=(havePrior?3:5)){result=Result::Motion;return;}
    for(unsigned i=0;i<3;++i)bias[i]=mean[i+3];
    result=Result::Complete;
  }
};
