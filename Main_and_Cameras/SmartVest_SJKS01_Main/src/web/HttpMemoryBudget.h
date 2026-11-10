#pragma once
#include <cstddef>
#include <cstdint>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

namespace HttpMemoryBudget {
// JSON/response caps alone leave no reserve for TCP and Wi-Fi allocations.
constexpr size_t RESERVE=12*1024;
inline bool fits(size_t bytes,size_t free,size_t largest){return free>=RESERVE&&bytes<=free-RESERVE&&bytes<=largest;}
// One shared check also keeps the repeated allocator guards small in flash.
__attribute__((noinline)) inline bool available(size_t bytes){
#if defined(ARDUINO_ARCH_ESP32)
  constexpr uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  return fits(bytes,heap_caps_get_free_size(caps),heap_caps_get_largest_free_block(caps));
#else
  (void)bytes;return true;
#endif
}
}
