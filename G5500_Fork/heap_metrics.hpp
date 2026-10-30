// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include "memory_pressure.hpp"
namespace forkrot {
inline MemoryPressure networkPressure;
inline std::atomic<bool> networkMemoryLimited{false};
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
struct HeapSnapshot { uint32_t free, largest; };
inline HeapSnapshot captureHeapSnapshot() { return {heapFreeBytes(), heapMaxBlockBytes()}; }
inline void sampleNetworkMemory(uint32_t now, HeapSnapshot heap = captureHeapSnapshot()) {
  networkPressure.sample(now, heap.free, heap.largest);
  networkMemoryLimited.store(networkPressure.limited, std::memory_order_relaxed);
}
inline bool networkLowMemory() { return networkMemoryLimited.load(std::memory_order_relaxed); }
// A successful socket write still queues later lwIP/Wi-Fi allocations on
// another task. Stop feeding bulk traffic before those bursts consume the
// control/recovery reserve, including on a fragmented heap.
inline bool networkBulkTxReady() {
  return heapFreeBytes() >= 32768 && heapMaxBlockBytes() >= 12288;
}
// The normal reserve limits bursts, not whether an established connection can
// make progress at all. Otherwise a page/status response below 32 KiB waits
// until its deadline and live telemetry repeatedly disconnects. A single small
// write can drain between turns; admit it only above 24 KiB/8 KiB of headroom.
inline size_t networkBulkTxBudget(size_t requested) {
  const auto free=heapFreeBytes(),block=heapMaxBlockBytes();
  if(free>=32768 && block>=12288)return requested;
  if(free>=24576 && block>=8192)return std::min(requested,size_t(512));
  return 0;
}
} // namespace forkrot
