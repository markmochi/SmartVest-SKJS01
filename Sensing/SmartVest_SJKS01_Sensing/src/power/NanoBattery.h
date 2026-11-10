#pragma once
#include <HardwareSerial.h>
#include <atomic>
#include "BatteryProtocol.h"
#include "BatteryTextFrame.h"
#include "../../Config.h"
class NanoBattery {
 public:
  BatteryProtocol::Tracker samples;
  std::atomic<uint32_t> uartErrors{0};
  uint32_t bytes=0;
  void begin(){
    uart.setRxBufferSize(256);uart.setTxBufferSize(128);
    uart.begin(SensorConfig::BATTERY_BAUD,SERIAL_8N1,SensorConfig::BATTERY_RX_PIN,SensorConfig::BATTERY_TX_PIN);
    uart.onReceiveError([this](hardwareSerial_error_t){++uartErrors;});
  }
  void tick(){
    BatteryProtocol::Reading r;
    for(unsigned budget=0;budget<96&&uart.available();++budget){
      ++bytes;const uint8_t b=uint8_t(uart.read());const uint32_t now=millis();
      if(parser.feed(b,now,r))samples.accept(r,now);
      if(textParser.feed(b,now,r))samples.accept(r,now);
    }
  }
  uint32_t rejected()const{return parser.rejected+textParser.rejected;}
 private:
  HardwareSerial uart{1};
  BatteryProtocol::Parser parser;
  BatteryTextFrame textParser;
};
