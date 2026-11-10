#pragma once
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <atomic>
struct UartHealth {
  std::atomic<uint32_t> fifoOverflow{0}, bufferFull{0}, framing{0}, parity{0}, breaks{0};
  void observe(hardwareSerial_error_t error) {
    switch (error) {
      case UART_FIFO_OVF_ERROR: ++fifoOverflow; break;
      case UART_BUFFER_FULL_ERROR: ++bufferFull; break;
      case UART_FRAME_ERROR: ++framing; break;
      case UART_PARITY_ERROR: ++parity; break;
      case UART_BREAK_ERROR: ++breaks; break;
      default: break;
    }
  }
  uint32_t total() const { return fifoOverflow.load()+bufferFull.load()+framing.load()+parity.load()+breaks.load(); }
  void writeJson(JsonObject out) const {
    out["total"] = total(); out["fifoOverflow"] = fifoOverflow.load(); out["bufferFull"] = bufferFull.load();
    out["framing"] = framing.load(); out["parity"] = parity.load(); out["breaks"] = breaks.load();
  }
};
