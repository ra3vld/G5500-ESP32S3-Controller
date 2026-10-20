#pragma once
#include <cstddef>
inline constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
inline size_t fakeHeapFree=65536, fakeHeapBlock=32768;
inline size_t heap_caps_get_free_size(int) {return fakeHeapFree;}
inline size_t heap_caps_get_minimum_free_size(int) {return fakeHeapFree;}
inline size_t heap_caps_get_largest_free_block(int) {return fakeHeapBlock;}
