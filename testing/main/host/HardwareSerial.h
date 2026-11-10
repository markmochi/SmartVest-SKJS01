#pragma once
#include "Arduino.h"
#include <deque>
#include <functional>
enum hardwareSerial_error_t {UART_FIFO_OVF_ERROR,UART_BUFFER_FULL_ERROR,UART_FRAME_ERROR,UART_PARITY_ERROR,UART_BREAK_ERROR};
struct FakeModem {
 std::deque<char> rx; std::string tx; std::vector<std::string> commands;
 std::function<void(std::string)> respond; bool body=false; int submissions=0;
 void inject(const std::string& s){for(char c:s)rx.push_back(c);}
 void put(char c){
  if(c==27){tx.clear();body=false;return;}
  if(c==26){++submissions;tx.clear();body=false;if(respond)respond("BODY");return;}
  if(c=='\r'&&!body){auto cmd=tx;tx.clear();commands.push_back(cmd);if(respond)respond(cmd);return;}
  tx+=c;
 }
};
inline FakeModem modem;
class HardwareSerial {
 public:
 explicit HardwareSerial(int){}
 void setRxBufferSize(int){} void setTxBufferSize(int){}
 void begin(uint32_t,int,int,int){} void end(){}
 void onReceiveError(std::function<void(hardwareSerial_error_t)>){}
 int available(){return int(modem.rx.size());} int availableForWrite(){return 512;}
 int read(){char c=modem.rx.front();modem.rx.pop_front();return c;}
 size_t print(const String&s){for(char c:s.v)modem.put(c);return s.length();}
 size_t print(char c){modem.put(c);return 1;}
 size_t write(int c){modem.put(char(c));return 1;}
 size_t write(const uint8_t* bytes,size_t count){for(size_t i=0;i<count;++i)modem.put(char(bytes[i]));return count;}
};
