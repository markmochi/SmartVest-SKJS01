#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <math.h>

// Bosch BMI323 datasheet BST-BMI323-DS000-13, sections 5.3 and 5.6.
constexpr int BMI_PIN_SCK=18, BMI_PIN_MISO=19, BMI_PIN_MOSI=23, BMI_PIN_CS=21;
constexpr uint32_t BMI_SPI_HZ=100000; // 100 kHz: confirmed stable standalone link
constexpr uint16_t BMI_CHIP_ID=0x0043;
constexpr uint16_t BMI_ACC_CONFIG=0x70B9; // HP, no averaging, ODR/4, +/-16g, 200Hz
constexpr uint16_t BMI_GYR_CONFIG=0x70C9; // HP, no averaging, ODR/4, +/-2000dps, 200Hz
constexpr float ACCEL_RANGE_G=16.0f, GYRO_RANGE_DPS=2000.0f;
constexpr float ACCEL_LSB_PER_G=32768.0f/ACCEL_RANGE_G;
constexpr float GYRO_LSB_PER_DPS=32768.0f/GYRO_RANGE_DPS;
constexpr uint32_t TARGET_PERIOD_US=5000;
constexpr uint32_t BMI_TIME_MASK=0x3FFFFFFFU; // sensor time bits 31:30 are reserved
constexpr uint32_t BMI_PERIOD_TICKS=128; // 5000us / 39.0625us
constexpr uint16_t BMI_ACC_READY=0x0080, BMI_GYR_READY=0x0040, BMI_TEMP_READY=0x0020;
constexpr uint16_t BMI_ERROR_MASK=0x0975; // defined ERR_REG bits, including fatal/configuration/feature/interface faults
enum class BMIReadResult : uint8_t { Fresh, NotReady, Duplicate, Rejected, Fault };
enum BMIFrameFlags : uint32_t {
  BMI_TEMP_VALID=1U, BMI_TEMP_FRESH=2U, BMI_TIME_DELTA_VALID=4U
  // STATUS data-ready bits at bits 15:8; SAT_FLAGS axes at bits 21:16.
};
struct BMI323Frame {
  int16_t accel[3]{},gyro[3]{},rawTemperature=INT16_MIN;
  float temperatureC=NAN;
  uint32_t sensorTime=0,deltaUs=0,flags=0;
  uint16_t status=0,error=0,saturation=0;
};

// Only the acquisition task accesses this driver. SPI has no acknowledgement:
// successful bus clocks are NOT a successful measurement. Reads are validated
// against ID, configuration, error/reset flags, DRDY and advancing sensor time.
class BMI323SPI {
public:
  bool identified=false,configured=false;
  bool busStarted=false; // avoid tearing down a healthy ESP32 SPI bus on retries
  uint16_t chipId=0,accConfig=0,gyrConfig=0,lastStatus=0,lastError=0,lastSaturation=0;
  uint32_t idFailures=0,configFailures=0,invalidFrames=0,resetFaults=0; // includes transient ID/config read errors
  uint32_t missedPeriods=0,rejectedFrames=0,readAttempts=0;
  uint8_t badStreak=0;
  const char* detail="Starting BMI323 SPI";
  const char* lastIssue="None";

  bool startBus(){
    // The sensor task alone owns SPI. Reuse the initialized bus when recovering:
    // repeated SPI.end()/begin() during sensor brownouts can create extra CS edges.
    digitalWrite(BMI_PIN_CS,HIGH);
    pinMode(BMI_PIN_CS,OUTPUT);
    digitalWrite(BMI_PIN_CS,HIGH);
    if(busStarted)return true;
    const bool ready=SPI.begin(BMI_PIN_SCK,BMI_PIN_MISO,BMI_PIN_MOSI,BMI_PIN_CS);
    busStarted=ready;
    digitalWrite(BMI_PIN_CS,HIGH);
    if(ready)delay(200);
    return ready;
  }
  bool begin(){
    const bool wasConfigured=configured;
    identified=configured=false;timestampKnown=false;previousHostUs=0;pendingReady=0;badStreak=0;
    chipId=accConfig=gyrConfig=lastStatus=lastError=lastSaturation=0;
    if(!startBus()){detail="ESP32 SPI bus initialization failed";return false;}
    detail="Selecting BMI323 4-wire SPI";
    for(unsigned attempt=0;attempt<3;attempt++){
      activate();
      chipId=readRegister(0x00);
      if(validId())break;
      delay(10);
    }
    if(!validId()){
      idFailures++;
      detail=chipId==0?"BMI323 MISO returned all zeros; inspect power, CS, SDO/MISO and wiring":"BMI323 chip ID invalid after retries; inspect SPI wiring and power";
      return false;
    }
    identified=true;
    // Preserve an already running and correctly configured sensor after WiFi
    // starts. Do not reset or reconfigure it just because the sensor task began.
    if(wasConfigured){
      accConfig=readRegister(0x20);gyrConfig=readRegister(0x21);
      if(accConfig==BMI_ACC_CONFIG&&gyrConfig==BMI_GYR_CONFIG){
        lastError=readRegister(0x01);
        if(!(lastError&BMI_ERROR_MASK)){
          (void)readRegister(0x02);
          configured=true;lastHealthMs=millis();
          detail="BMI323 already configured; SPI bus reused";
          return true;
        }
      }
    }
    // Avoid a software reset in normal recovery: it reselects I2C mode
    // and unnecessarily interrupts a marginal, intermittently working link.
    writeRegister(0x20,BMI_ACC_CONFIG);delay(20);
    writeRegister(0x21,BMI_GYR_CONFIG);delay(200);
    accConfig=readRegister(0x20);gyrConfig=readRegister(0x21);
    lastError=readRegister(0x01);
    if(accConfig!=BMI_ACC_CONFIG||gyrConfig!=BMI_GYR_CONFIG){
      configFailures++;detail="BMI323 configuration readback mismatch";return false;
    }
    if(lastError&BMI_ERROR_MASK){detail="BMI323 reports a sensor/configuration error";return false;}
    (void)readRegister(0x02);
    configured=true;lastHealthMs=millis();detail="BMI323 configured without a destructive reset";
    return true;
  }
  BMIReadResult readFrame(BMI323Frame& frame){
    if(!configured)return BMIReadResult::Fault;
    readAttempts++;
    // Same single-burst framing as the stable diagnostic, plus SAT_FLAGS.
    // Every sample now carries its own chip-ID check. Never decode a separate
    // data transaction whose identity was last checked a second ago.
    uint16_t packet[13];readWords(0x00,packet,13);
    chipId=packet[0];lastError=packet[1];lastStatus=packet[2];
    if(!validId()){idFailures++;return reject("BMI323 sample chip ID invalid");}
    identified=true;
    if(lastError&BMI_ERROR_MASK)return reject("BMI323 error register reports a fault");
    if(lastStatus&0x0001){resetFaults++;return fault("BMI323 reset during sampling; reinitialization required");}
    // Configuration is slow-changing. Keep readback off the common path and
    // require three failed reads before declaring a configuration loss.
    if(uint32_t(millis()-lastHealthMs)>=5000U){
      lastHealthMs=millis();
      accConfig=readRegister(0x20);gyrConfig=readRegister(0x21);
      if(accConfig!=BMI_ACC_CONFIG||gyrConfig!=BMI_GYR_CONFIG){
        configFailures++;
        for(unsigned i=0;i<2&&(accConfig!=BMI_ACC_CONFIG||gyrConfig!=BMI_GYR_CONFIG);++i){
          delayMicroseconds(10);
          if(accConfig!=BMI_ACC_CONFIG)accConfig=readRegister(0x20);
          if(gyrConfig!=BMI_GYR_CONFIG)gyrConfig=readRegister(0x21);
        }
        if(accConfig!=BMI_ACC_CONFIG||gyrConfig!=BMI_GYR_CONFIG)
          return fault("BMI323 configuration readback failed on three reads");
      }
    }
    // Preserve separately arriving clear-on-read DRDY flags until both axes
    // groups are ready. Temperature DRDY is diagnostic, never a 200Hz gate.
    pendingReady|=lastStatus&(BMI_ACC_READY|BMI_GYR_READY|BMI_TEMP_READY);
    if((pendingReady&(BMI_ACC_READY|BMI_GYR_READY))!=(BMI_ACC_READY|BMI_GYR_READY))return BMIReadResult::NotReady;
    const uint16_t ready=pendingReady;pendingReady=0;
    const uint16_t* raw=packet+3; // axes, temperature, time, saturation from this burst
    lastSaturation=raw[9]&0x003F;
    for(unsigned i=0;i<6;i++){
      // Reset/invalid axes must not enter calibration or detection. A real
      // negative full-scale sample may be 0x8000; retain it if saturated.
      if(raw[i]==0x8000&&!(lastSaturation&(1U<<i))){invalidFrames++;return reject("BMI323 returned an uninitialized motion axis");}
    }
    const uint32_t sensorTime=(uint32_t(raw[7])|(uint32_t(raw[8])<<16))&BMI_TIME_MASK;
    const uint32_t delta=(sensorTime-previousTime)&BMI_TIME_MASK;
    if(timestampKnown&&delta==0)return BMIReadResult::Duplicate;
    if(timestampKnown&&delta>BMI_TIME_MASK/2U){resetFaults++;return reject("BMI323 sensor time moved backwards");}
    // Guard against forward single-bit flips in the sensor time word too.
    // Allow 40 ms of timing margin, but only compare closely spaced host reads.
    // Real acquisition pauses remain represented as gaps, not fake samples.
    if(timestampKnown){
      const uint32_t hostElapsedUs=uint32_t(micros()-previousHostUs);
      if(hostElapsedUs<100000U){
        const uint32_t allowedTicks=uint32_t((uint64_t(hostElapsedUs)*16U)/625U)+1024U;
        if(delta>allowedTicks){invalidFrames++;return reject("BMI323 sensor time jumped beyond host elapsed time");}
      }
    }
    frame={};frame.rawTemperature=int16_t(raw[6]);frame.status=ready;frame.error=lastError;frame.saturation=lastSaturation;
    frame.sensorTime=sensorTime;
    if(timestampKnown){
      // Round ticks*39.0625 to whole microseconds with 64-bit intermediates.
      frame.deltaUs=uint32_t((uint64_t(delta)*625U+8U)/16U);
      frame.flags|=BMI_TIME_DELTA_VALID;
      const uint32_t periods=delta/BMI_PERIOD_TICKS;
      if(periods>1)missedPeriods+=periods-1U;
    }
    frame.flags|=uint32_t(ready)<<8|uint32_t(lastSaturation)<<16;
    if(raw[6]!=0x8000){frame.temperatureC=float(int16_t(raw[6]))/512.0f+23.0f;frame.flags|=BMI_TEMP_VALID;if(ready&BMI_TEMP_READY)frame.flags|=BMI_TEMP_FRESH;}
    for(unsigned i=0;i<3;i++){frame.accel[i]=int16_t(raw[i]);frame.gyro[i]=int16_t(raw[i+3]);}
    badStreak=0;previousTime=sensorTime;previousHostUs=micros();timestampKnown=true;detail="BMI323 SPI measurements available";
    return BMIReadResult::Fresh;
  }
private:
  SPISettings settings{BMI_SPI_HZ,MSBFIRST,SPI_MODE0};
  uint32_t previousTime=0,previousHostUs=0,lastHealthMs=0;
  uint16_t pendingReady=0;
  bool timestampKnown=false;
  bool validId()const{return (chipId&0x00FFU)==BMI_CHIP_ID;}
  BMIReadResult fault(const char* why){configured=false;pendingReady=0;detail=lastIssue=why;return BMIReadResult::Fault;}
  BMIReadResult reject(const char* why){
    rejectedFrames++;pendingReady=0;detail=lastIssue=why;
    if(++badStreak>=3)return fault(why);
    return BMIReadResult::Rejected; // never delivered as a measurement
  }
  void activate(){digitalWrite(BMI_PIN_CS,LOW);delay(2);digitalWrite(BMI_PIN_CS,HIGH);delay(5);(void)readRegister(0x00);delay(5);} // discarded read creates CS rising edge; allow >200us
  void readWords(uint8_t reg,uint16_t* words,uint8_t count){
    SPI.beginTransaction(settings);digitalWrite(BMI_PIN_CS,LOW);delayMicroseconds(5);
    uint8_t tx[28]={},rx[28]={};tx[0]=reg|0x80;
    // Address + one dummy byte + at most 13 little-endian words. One ESP32
    // hardware transfer avoids 28 separate driver calls per acquisition.
    SPI.transferBytes(tx,rx,2U+2U*count);
    for(uint8_t i=0;i<count;i++)words[i]=uint16_t(rx[2+2*i])|(uint16_t(rx[3+2*i])<<8);
    digitalWrite(BMI_PIN_CS,HIGH);SPI.endTransaction();delayMicroseconds(3); // >=2us bus idle
  }
  uint16_t readRegister(uint8_t reg){uint16_t value=0;readWords(reg,&value,1);return value;}
  void writeRegister(uint8_t reg,uint16_t value){
    SPI.beginTransaction(settings);digitalWrite(BMI_PIN_CS,LOW);delayMicroseconds(5);
    SPI.transfer(reg&0x7F);SPI.transfer(uint8_t(value));SPI.transfer(uint8_t(value>>8));
    digitalWrite(BMI_PIN_CS,HIGH);SPI.endTransaction();delayMicroseconds(10);
  }
};
