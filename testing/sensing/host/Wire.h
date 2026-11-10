#pragma once
#include "Arduino.h"
#include <array>
class FakeWire {
 public:
  std::array<uint8_t, 256> registers = {};
  std::vector<uint8_t> tx;
  std::deque<uint8_t> rx;
  uint8_t cursor = 0, address = 0, connectedAddress = 0x68;
  bool secondDevice = false;
  bool clockedDataReady = true;
  uint32_t readyPeriod = UINT32_MAX;
  bool beginOk = true, fail = false, dataReady = true, shortRead = false;
  int stuckRegister = -1, begins = 0;
  bool begin(int, int, uint32_t) { ++begins; return beginOk; }
  void setTimeOut(int) {}
  void beginTransmission(uint8_t a) { address=a;tx.clear(); }
  size_t write(uint8_t byte) { tx.push_back(byte); return 1; }
  int endTransmission(bool = true) {
    if (fail || (address != connectedAddress && !secondDevice)) return 2;
    if (tx.size()) cursor = tx[0];
    if (tx.size() == 2 && cursor != stuckRegister) {
      if (cursor == 0x6B && tx[1] == 0x80) {
        const auto id = registers[0x75]; registers.fill(0); registers[0x75] = id;
      } else registers[cursor] = tx[1];
    }
    return 0;
  }
  size_t requestFrom(uint8_t, uint8_t length) {
    rx.clear();
    if (fail || (address != connectedAddress && !secondDevice)) return 0;
    const size_t n = shortRead ? length - 1 : length;
    const bool ready = dataReady && (!clockedDataReady || clockMs/5 != readyPeriod);
    if (cursor == 0x3A && ready) readyPeriod=clockMs/5;
    for (size_t i = 0; i < n; ++i) rx.push_back(cursor == 0x3A && i == 0 ? uint8_t(ready) : registers[cursor + i]);
    return n;
  }
  int available() { return int(rx.size()); }
  int read() { const int b = rx.front(); rx.pop_front(); return b; }
  void word(uint8_t reg, int16_t value) { registers[reg] = uint16_t(value) >> 8; registers[reg + 1] = uint8_t(value); }
};
inline FakeWire Wire;
