#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <deque>
constexpr int SERIAL_8N1 = 0;
inline uint32_t clockMs = 0, testExtraUs=0;
inline uint32_t millis() { return clockMs; }
inline uint32_t micros() { return clockMs * 1000+testExtraUs; }
inline void delay(uint32_t ms) { clockMs += ms; }
struct UsbSerial {
  std::deque<char> rx;
  std::string output;
  int capacity = 8192;
  void setTxBufferSize(int) {}
  void begin(uint32_t) {}
  int availableForWrite() { return capacity; }
  int available() { return int(rx.size()); }
  int read() { const int c = rx.front(); rx.pop_front(); return c; }
  size_t write(const uint8_t* p, size_t n) { output.append((const char*)p, n); return n; }
};
inline UsbSerial Serial;

inline void delayMicroseconds(uint32_t us){testExtraUs+=us;clockMs+=testExtraUs/1000;testExtraUs%=1000;}
