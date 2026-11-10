#include "Imu.h"
#include <cmath>
static_assert(SensorConfig::SPI_SCK_PIN==BMI_PIN_SCK && SensorConfig::SPI_MISO_PIN==BMI_PIN_MISO && SensorConfig::SPI_MOSI_PIN==BMI_PIN_MOSI && SensorConfig::SPI_CS_PIN==BMI_PIN_CS && SensorConfig::SPI_HZ==BMI_SPI_HZ, "Driver/profile mismatch");
void Imu::begin() {
  fresh=false;sampled=false;continuityLost=true;
  const bool ok=driver.begin();whoAmI=driver.chipId;phaseAt=millis();
  phase=ok?Phase::Live:Phase::Retry;
  error=ok?Error::None:!driver.busStarted?Error::Start:!driver.identified?Error::Identity:Error::Configuration;
  faultRegister=0;observed=whoAmI;expected=BMI_CHIP_ID;
  if(driver.identified){faultRegister=driver.accConfig!=BMI_ACC_CONFIG?0x20:0x21;observed=faultRegister==0x20?driver.accConfig:driver.gyrConfig;expected=faultRegister==0x20?BMI_ACC_CONFIG:BMI_GYR_CONFIG;}
  if(ok){faultRegister=0;observed=expected=0;}
  if(!ok)++failures;
}
void Imu::retry(){begin();}
bool Imu::healthy()const {return phase==Phase::Live && sampled && uint32_t(millis()-sample.atMs)<SensorConfig::IMU_STALE_MS;}
void Imu::tick(){
  fresh=false;continuityLost=false;
  if(phase!=Phase::Live){continuityLost=true;if(uint32_t(millis()-phaseAt)>=SensorConfig::IMU_RETRY_MS)begin();return;}
  const uint32_t start=micros();BMI323Frame frame;const auto result=driver.readFrame(frame);
  const uint32_t cycle=uint32_t(micros()-start);if(cycle>maxCycleUs)maxCycleUs=cycle;
  whoAmI=driver.chipId;
  if(result!=BMIReadResult::Fresh){
    if(result==BMIReadResult::NotReady)++notReady;
    else if(result==BMIReadResult::Duplicate)++duplicates;
    else {++failures;continuityLost=true;error=Error::Rejected;}
    if(sampled && uint32_t(micros()-sample.atUs)>SensorConfig::SAMPLE_GAP_US)continuityLost=true;
    if(result==BMIReadResult::Fault || uint32_t(millis()-(sampled?sample.atMs:phaseAt))>=SensorConfig::IMU_STALE_MS){
      phase=Phase::Retry;phaseAt=millis();sampled=false;continuityLost=true;error=Error::Stale;
    }
    return;
  }
  const uint32_t now=micros();sample.intervalUs=sampled?uint32_t(now-sample.atUs):0;
  sample.deltaUs=frame.deltaUs;sample.deltaValid=bool(frame.flags&BMI_TIME_DELTA_VALID);sample.sensorTime=frame.sensorTime;
  continuityLost=!sampled || !sample.deltaValid || sample.intervalUs>SensorConfig::SAMPLE_GAP_US || frame.deltaUs>SensorConfig::SAMPLE_GAP_US;
  if(sampled && continuityLost)++overruns;
  sample.accelClipped=bool(frame.saturation&7);sample.gyroClipped=bool(frame.saturation&56);
  for(unsigned i=0;i<3;++i){sample.rawAccel[i]=frame.accel[i];sample.rawGyro[i]=frame.gyro[i];sample.accel[i]=frame.accel[i]/ACCEL_LSB_PER_G;sample.gyro[i]=frame.gyro[i]/GYRO_LSB_PER_DPS;}
  // Match reference near-range quality guards as well as hardware SAT_FLAGS.
  for(unsigned i=0;i<3;++i){sample.accelClipped|=std::fabs(sample.accel[i])>=15.5f;sample.gyroClipped|=std::fabs(sample.gyro[i])>=1950.0f;}
  sample.saturated=sample.accelClipped||sample.gyroClipped;
  sample.rawTemperature=frame.rawTemperature;sample.temperatureC=frame.temperatureC;
  sample.temperatureValid=bool(frame.flags&BMI_TEMP_VALID);sample.temperatureFresh=bool(frame.flags&BMI_TEMP_FRESH);
  sample.atUs=now;sample.atMs=millis();sample.sequence=++samples;sampled=true;fresh=true;error=Error::None;
}
