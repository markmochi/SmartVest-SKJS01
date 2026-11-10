#pragma once
#include <HardwareSerial.h>
namespace Console {
void begin(HardwareSerial& port);
void line(const char* text);
void format(const char* format, ...);
void pump();
uint32_t droppedLines();
uint32_t transmittedBytes();
} // namespace Console
