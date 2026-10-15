#pragma once
#include <cstddef>
#include <cstdint>
inline constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
inline size_t fakeHeapFree=32768, fakeHeapBlock=16384;
inline size_t heap_caps_get_free_size(int) { return fakeHeapFree; }
inline size_t heap_caps_get_minimum_free_size(int) { return fakeHeapFree; }
inline size_t heap_caps_get_largest_free_block(int) { return fakeHeapBlock; }
inline constexpr int ESP_OK = 0;
using esp_alloc_failed_hook_t = void (*)(size_t, uint32_t, const char *);
inline esp_alloc_failed_hook_t fakeAllocFailureHook = nullptr;
inline int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback) {
  fakeAllocFailureHook = callback; return ESP_OK;
}
