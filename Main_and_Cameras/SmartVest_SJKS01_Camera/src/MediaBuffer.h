#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#endif
// Camera frame/index workspaces belong in the required PSRAM, leaving the
// internal heap available to Wi-Fi, task stacks and filesystem bookkeeping.
class MediaBuffer {
 public:
  ~MediaBuffer(){std::free(bytes);}
  MediaBuffer()=default;MediaBuffer(const MediaBuffer&)=delete;MediaBuffer& operator=(const MediaBuffer&)=delete;
  bool reserve(size_t n){
    if(bytes)return n<=capacity;
#ifdef ARDUINO_ARCH_ESP32
    bytes=static_cast<uint8_t*>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#else
    bytes=static_cast<uint8_t*>(std::malloc(n));
#endif
    if(bytes)capacity=n;return bytes!=nullptr;
  }
  uint8_t* data(){return bytes;}
 private:
  uint8_t* bytes=nullptr;size_t capacity=0;
};
