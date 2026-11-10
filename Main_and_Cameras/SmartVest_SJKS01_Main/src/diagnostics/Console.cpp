#include "Console.h"
#include "../../Config.h"
#include <freertos/FreeRTOS.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace {
HardwareSerial* output = nullptr;
portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;
constexpr size_t CAPACITY = 4096;
char queue[CAPACITY];
size_t head = 0, tail = 0, count = 0;
uint32_t dropped = 0, transmitted = 0;
}
void Console::begin(HardwareSerial& port) { output = &port; }
void Console::line(const char* text) {
  if (!Config::ENABLE_USB_DEBUG || !text) return;
  const size_t length = strnlen(text, 190);
  portENTER_CRITICAL(&queueMux);
  if (length + 2 > CAPACITY - count) { ++dropped; portEXIT_CRITICAL(&queueMux); return; }
  for (size_t i = 0; i < length; ++i) { queue[head] = text[i]; head = (head + 1) % CAPACITY; }
  queue[head] = '\r'; head = (head + 1) % CAPACITY;
  queue[head] = '\n'; head = (head + 1) % CAPACITY;
  count += length + 2;
  portEXIT_CRITICAL(&queueMux);
}
void Console::format(const char* format, ...) {
  if (!Config::ENABLE_USB_DEBUG) return;
  char lineBuffer[192];
  va_list args; va_start(args, format); vsnprintf(lineBuffer, sizeof(lineBuffer), format, args); va_end(args);
  line(lineBuffer);
}
void Console::pump() {
  if (!Config::ENABLE_USB_DEBUG || !output) return;
  const int available = output->availableForWrite();
  if (available <= 0) return;
  char bytes[64];
  size_t size = size_t(available) < sizeof(bytes) ? size_t(available) : sizeof(bytes);
  portENTER_CRITICAL(&queueMux);
  if (size > count) size = count;
  for (size_t i = 0; i < size; ++i) bytes[i] = queue[(tail + i) % CAPACITY];
  portEXIT_CRITICAL(&queueMux);
  if (!size) return;
  const size_t written = output->write(reinterpret_cast<const uint8_t*>(bytes), size);
  portENTER_CRITICAL(&queueMux);
  tail = (tail + written) % CAPACITY; count -= written; transmitted += written;
  portEXIT_CRITICAL(&queueMux);
}
uint32_t Console::droppedLines() { portENTER_CRITICAL(&queueMux); const auto value = dropped; portEXIT_CRITICAL(&queueMux); return value; }
uint32_t Console::transmittedBytes() { portENTER_CRITICAL(&queueMux); const auto value = transmitted; portEXIT_CRITICAL(&queueMux); return value; }
