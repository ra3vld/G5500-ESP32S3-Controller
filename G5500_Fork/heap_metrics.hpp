// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <esp_heap_caps.h>
#include <cstdint>
namespace forkrot {
// HTTP and radio buffers need byte-addressable internal RAM. Arduino's
// ESP.getFreeHeap/getMaxAllocHeap also count internal 32-bit-only regions.
inline uint32_t heapFreeBytes() {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
inline uint32_t heapMinBytes() {
  return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
inline uint32_t heapMaxBlockBytes() {
  return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
} // namespace forkrot
