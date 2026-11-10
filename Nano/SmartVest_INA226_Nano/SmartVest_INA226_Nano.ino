#include <Wire.h>
#include <INA226_WE.h>
#include "src/BatteryProtocol.h"
#include "src/BatteryDisplay.h"

// Only enable for a direct Nano USB investigation: omits the telemetry frame
// and prints at 115200 baud. Leave 0 for readable status + normal vest relay.
#ifndef SMARTVEST_NANO_USB_DIAGNOSTICS
#define SMARTVEST_NANO_USB_DIAGNOSTICS 0
#endif

// Classic 5 V ATmega328P Nano. UART TX D1 -> divider -> Sensing RX32.
// D0 is intentionally unconnected. USB and D1 share readable status + CRC frame.
constexpr uint8_t INA_ADDRESS=0x40;
constexpr float SHUNT_OHMS=0.010f, MAX_AMPS=5.0f;
constexpr uint32_t UART_BAUD=19200, SAMPLE_MS=1000, RETRY_MS=5000;
constexpr bool ESTIMATE_3S_LIION=true; // Approximate loaded operating range: 10.6 to 11.65 V.
INA226_WE ina226(INA_ADDRESS);
BatteryProtocol::Reading reading;
BatteryDisplay batteryDisplay;
bool configured=false;
uint32_t sampledAt=0,retriedAt=0;
uint16_t expectedCalibration=0,expectedConfig=0;
uint8_t lastRegister=0,lastReadCount=0,lastPointerError=0;
bool lastReadTimeout=false;
uint32_t recoveredReads=0;

bool readRegisterChecked(uint8_t reg,uint16_t& value){
  // One normal repeated-START read, then one STOP-separated retry. INA226
  // retains its pointer across STOP (TI datasheet, register-read sequence).
  // Never accept a partial response or retry indefinitely on a faulty bus.
  for(uint8_t attempt=0;attempt<2;++attempt){
    lastRegister=reg;lastReadCount=0;lastReadTimeout=false;
    Wire.clearWireTimeoutFlag();Wire.beginTransmission(INA_ADDRESS);Wire.write(reg);
    lastPointerError=Wire.endTransmission(attempt!=0);
    if(lastPointerError){reading.error=lastPointerError;continue;}
    lastReadCount=Wire.requestFrom(INA_ADDRESS,uint8_t(2));
    lastReadTimeout=Wire.getWireTimeoutFlag();
    if(lastReadTimeout||lastReadCount!=2||Wire.available()!=2){
      while(Wire.available())Wire.read();
      reading.error=lastReadTimeout?12:11;continue;
    }
    value=uint16_t(Wire.read())<<8;value|=uint8_t(Wire.read());
    reading.error=0;if(attempt)++recoveredReads;return true;
  }
  return false;
}
bool configureMonitor(){
  reading.error=0;
  uint16_t manufacturer=0,device=0;
  if(!readRegisterChecked(0xfe,manufacturer)||!readRegisterChecked(0xff,device))return false;
  if(manufacturer!=0x5449||(device&0xfff0)!=0x2260){reading.error=6;return false;}
  if(!ina226.init()){reading.error=7;return false;}
  ina226.setResistorRange(SHUNT_OHMS,MAX_AMPS);
  ina226.setAverage(INA226_AVERAGE_16);
  ina226.setMeasureMode(INA226_CONTINUOUS);
  expectedCalibration=uint16_t(0.00512f/((MAX_AMPS/32768.0f)*SHUNT_OHMS));
  expectedConfig=0x4527; // AVG16, bus/shunt 1100 us, continuous both.
  uint16_t cal=0,conf=0;
  if(!readRegisterChecked(5,cal)||!readRegisterChecked(0,conf))return false;
  if(cal!=expectedCalibration||conf!=expectedConfig){reading.error=8;return false;}
  return true;
}
void measureBattery(){
  reading.flags=0;reading.percent=255;reading.age=UINT32_MAX;
  // Keep legacy charging fields empty for the existing receiver wire format.
  reading.charge=BatteryProtocol::ChargeUnknown;reading.netCurrentMA=0;
  if(!configured)return;
  reading.error=0;
  uint16_t conf=0,cal=0,bus=0,current=0,power=0,shunt=0,mask=0;
  // Explicitly check short reads; INA226_WE 1.3.0 does not reject every short
  // requestFrom result. Also detect a sensor reset/calibration-register loss.
  if(!readRegisterChecked(0,conf)||!readRegisterChecked(5,cal)||
     !readRegisterChecked(2,bus)||!readRegisterChecked(4,current)||
     !readRegisterChecked(3,power)||!readRegisterChecked(1,shunt)||!readRegisterChecked(6,mask)){
    configured=false;retriedAt=millis();return;
  }
  reading.flags=BatteryProtocol::Responding;
  if(conf!=expectedConfig||cal!=expectedCalibration){reading.error=8;configured=false;retriedAt=millis();return;}
  if(mask&4){reading.flags|=BatteryProtocol::Overflow;reading.error=9;return;}
  const float currentLSB=MAX_AMPS/32768.0f;
  reading.busMV=(uint32_t(bus)*5+2)/4;
  reading.currentMA=int32_t(roundf(int16_t(current)*currentLSB*1000));
  reading.powerMW=uint32_t(roundf(power*25*currentLSB*1000));
  // Report a magnitude for reverse flow; never interpret unsigned power as
  // a signed value or let its encoding reject valid reverse current.
  if(reading.currentMA<0)reading.powerMW=uint32_t(-reading.currentMA)*reading.busMV/1000;
  reading.shuntUV=int32_t(roundf(int16_t(shunt)*2.5f));
  if(reading.busMV<8000||reading.busMV>15000||reading.currentMA < -5000||reading.currentMA>5000||reading.powerMW>75000){reading.error=10;return;}
  reading.flags|=BatteryProtocol::Valid;reading.age=0;
  if(ESTIMATE_3S_LIION)reading.percent=batteryDisplay.update(BatteryDisplay::sourceMillivolts(reading.busMV,reading.shuntUV),millis());
}
void printBatteryStatus(){
  const bool valid=(reading.flags&BatteryProtocol::Valid)!=0;
  Serial.print(F("[BATTERY] valid="));Serial.print(valid?1:0);
  Serial.print(F(" voltage="));
  if(valid){
    Serial.print(reading.busMV/1000);Serial.print('.');
    const uint16_t fraction=reading.busMV%1000;
    if(fraction<100)Serial.print('0');if(fraction<10)Serial.print('0');
    Serial.print(fraction);Serial.print('V');
  }else Serial.print(F("unavailable"));
  Serial.print(F(" percent="));
  if(valid&&reading.percent<=100){Serial.print(reading.percent);Serial.print('%');}
  else Serial.print(F("unavailable"));
  if(valid){
    Serial.print(F(" raw="));Serial.print(BatteryProtocol::estimate3S(BatteryDisplay::sourceMillivolts(reading.busMV,reading.shuntUV)));
    Serial.print(F(" smooth="));Serial.print(batteryDisplay.smoothedPercent());
    Serial.print(F(" hold="));Serial.print(batteryDisplay.riseHeld()?1:0);
    Serial.print(F(" current_mA="));Serial.print(reading.currentMA);
  }
  Serial.print(F(" error="));Serial.print(reading.error);
  if(!valid){
    if(reading.error==10){Serial.print(F(" raw_mV="));Serial.print(reading.busMV);}
    Serial.print(F(" register=0x"));Serial.print(lastRegister,HEX);
    Serial.print(F(" pointer="));Serial.print(lastPointerError);
    Serial.print(F(" bytes="));Serial.print(lastReadCount);
    Serial.print(F(" timeout="));Serial.print(lastReadTimeout?1:0);
  }
}
void setup(){
  Serial.begin(SMARTVEST_NANO_USB_DIAGNOSTICS?115200:UART_BAUD);
  Serial.println(F("[NANO] SmartVest firmware 1.0"));
  Wire.begin();Wire.setClock(100000);
  Wire.setWireTimeout(25000,true);pinMode(LED_BUILTIN,OUTPUT);
  reading.profile=ESTIMATE_3S_LIION?BatteryProtocol::LiIon3SApproximate:BatteryProtocol::Unknown;
  configured=configureMonitor();retriedAt=millis();sampledAt=millis();
}
void loop(){
  uint32_t now=millis();
  if(!configured&&uint32_t(now-retriedAt)>=RETRY_MS){
    configured=configureMonitor();now=millis();retriedAt=now;sampledAt=now;
  }
  if(uint32_t(now-sampledAt)>=SAMPLE_MS){
    sampledAt=now;measureBattery();
    if(!(reading.flags&BatteryProtocol::Valid)){batteryDisplay.invalidate();}
    if(!++reading.sequence)++reading.sequence;
    reading.uptime=millis();
    printBatteryStatus();
#if SMARTVEST_NANO_USB_DIAGNOSTICS
    Serial.print(F(" recovered="));Serial.println(recoveredReads);
#else
    uint8_t bytes[BatteryProtocol::SIZE];BatteryProtocol::encode(reading,bytes);
    // ASCII hex preserves the existing CRC-protected packet and makes USB
    // output readable. Sensing accepts this wrapper and the older raw packets.
    Serial.print(F(" |B1:"));
    for(uint8_t b:bytes){
      const uint8_t hi=b>>4,lo=b&15;
      Serial.write(uint8_t(hi<10?'0'+hi:'A'+hi-10));
      Serial.write(uint8_t(lo<10?'0'+lo:'A'+lo-10));
    }
    Serial.println();
#endif
  }
  digitalWrite(LED_BUILTIN,(reading.flags&BatteryProtocol::Valid)?now%1000<50:now%500<250);
}
