#pragma once
#include <cstddef>
#include <cstdint>
constexpr uint32_t MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
inline size_t heap_caps_get_free_size(uint32_t) { return 128000; }
inline size_t heap_caps_get_minimum_free_size(uint32_t) { return 120000; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 96000; }
