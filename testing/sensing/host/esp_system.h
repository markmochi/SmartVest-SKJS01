#pragma once
#include <cstdint>
inline uint32_t randomId = 100;
inline uint32_t esp_random() { return ++randomId; }
