#pragma once
#include "Arduino.h"
#include <array>
#include <functional>
enum hardwareSerial_error_t { UART_FIFO_OVF_ERROR, UART_BUFFER_FULL_ERROR, UART_FRAME_ERROR,
                             UART_PARITY_ERROR, UART_BREAK_ERROR };
struct SerialPort {
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  int capacity = 512, begins = 0, rxPin = -1, txPin = -1;
  uint32_t baud = 0;
  std::function<void(hardwareSerial_error_t)> errorHandler;
};
inline std::array<SerialPort, 3> ports;
class HardwareSerial {
 public:
  explicit HardwareSerial(int port) : index(port) {}
  void setRxBufferSize(int) {} void setTxBufferSize(int) {}
  void begin(uint32_t baud, int, int rxPin, int txPin) {
    auto& p = ports[index]; ++p.begins; p.baud = baud; p.rxPin = rxPin; p.txPin = txPin;
  }
  void onReceiveError(std::function<void(hardwareSerial_error_t)> callback) { ports[index].errorHandler = callback; }
  int availableForWrite() { return ports[index].capacity; }
  int available() { return int(ports[index].rx.size()); }
  int read() { auto& rx = ports[index].rx; const int c = rx.front(); rx.pop_front(); return c; }
  size_t write(const uint8_t* p, size_t n) {
    auto& port = ports[index];
    if (n > size_t(port.capacity)) return 0;
    port.tx.insert(port.tx.end(), p, p + n); return n;
  }
 private: int index;
};
inline void transfer(int from, int to) {
  auto& source = ports[from].tx;
  for (uint8_t byte : source) ports[to].rx.push_back(byte);
  source.clear();
}
