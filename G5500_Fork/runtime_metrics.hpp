// G5500 Fork; GPL-3.0.
#pragma once
#include <atomic>
#include <cstdint>
#include <esp_heap_caps.h>
#include "allocation_record.hpp"
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_debug_helpers.h>
#if CONFIG_IDF_TARGET_ARCH_XTENSA
#include <esp_cpu_utils.h>
#endif
#endif
namespace forkrot {
// Freeze the first failure in each size class. A published sample never changes,
// so H4 can read it without a critical section or an allocation under low memory.
struct AllocationFailure {
  std::atomic<unsigned> state{0}; // empty, being captured, ready
  uint32_t size = 0, caps = 0, freeBytes = 0, largestBlock = 0;
  uint32_t pc[8]{};
  const char *function = "unknown";
  char task[16]{};
  bool beginCapture() {
    unsigned empty = 0;
    return state.compare_exchange_strong(empty, 1);
  }
  bool ready() const { return state.load(std::memory_order_acquire) == 2; }
  void publish() { state.store(2, std::memory_order_release); }
};
// Allocation callbacks may run on either core. No logging or allocations
// here: reporting is deferred to H4 and /api/status in the main loop.
struct RuntimeMetrics {
  uint32_t *recoveryWord = nullptr;
  uint32_t recoveryCookie = 0;
  std::atomic<uint32_t> allocationFailures{0}, largestFailedAllocation{0};
  bool hookReady = false;
  uint32_t loopMaxUs = 0, loopsOver20ms = 0; // main-loop access only
  AllocationFailure failures[2]; // first <1024B and first >=1024B failure
  AllocationRecord previousFailures[2]{};
  AllocationRecord *persistentFailures=nullptr;
  uint8_t elf[32]{};
  void recordLoop(uint32_t elapsedUs) {
    if (elapsedUs > loopMaxUs) loopMaxUs = elapsedUs;
    if (elapsedUs > 20000) ++loopsOver20ms;
  }
};
inline RuntimeMetrics runtimeMetrics;
inline void beginRuntimeMetrics(uint32_t *recoveryWord = nullptr, uint32_t recoveryCookie = 0,
    AllocationRecord *records=nullptr,const uint8_t *elf=nullptr,bool retain=false) {
  runtimeMetrics.recoveryWord = recoveryWord;
  runtimeMetrics.recoveryCookie = recoveryCookie;
  runtimeMetrics.persistentFailures=records;
  for(unsigned i=0;i<32;++i)runtimeMetrics.elf[i]=elf?elf[i]:0;
  for(unsigned i=0;records&&i<2;++i) {
    runtimeMetrics.previousFailures[i]={};
    if(retain&&allocationRecordValid(records[i]))runtimeMetrics.previousFailures[i]=records[i];
    __atomic_store_n(&records[i].magic,0,__ATOMIC_RELEASE);
  }
  runtimeMetrics.hookReady = heap_caps_register_failed_alloc_callback(
      [](size_t size, uint32_t caps, const char *function) {
        auto &m = runtimeMetrics;
        // Mark the next boot before an allocation failure can escalate to an
        // abort. No NVS, logging, heap allocation or main-loop progress needed.
        if (m.recoveryWord) __atomic_store_n(m.recoveryWord, m.recoveryCookie, __ATOMIC_RELAXED);
        m.allocationFailures.fetch_add(1, std::memory_order_relaxed);
        uint32_t previous = m.largestFailedAllocation.load(std::memory_order_relaxed);
        while (size > previous && !m.largestFailedAllocation.compare_exchange_weak(
            previous, uint32_t(size), std::memory_order_relaxed)) {}
        auto &failure = m.failures[size >= 1024 ? 1 : 0];
        if (!failure.beginCapture()) return;
        failure.size = size; failure.caps = caps; failure.function = function;
        failure.freeBytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        failure.largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#ifdef ARDUINO_ARCH_ESP32
        const char *task = xPortInIsrContext() ? "ISR" : pcTaskGetName(nullptr);
        for (unsigned i = 0; task && i + 1 < sizeof failure.task && task[i]; ++i) failure.task[i] = task[i];
#if CONFIG_IDF_TARGET_ARCH_XTENSA
        esp_backtrace_frame_t frame{};
        esp_backtrace_get_start(&frame.pc, &frame.sp, &frame.next_pc);
        for (unsigned i = 0; i < 8; ++i) {
          failure.pc[i] = esp_cpu_process_stack_pc(frame.pc);
          if (!frame.next_pc || !esp_backtrace_get_next_frame(&frame)) break;
        }
#endif
#endif
        if(m.persistentFailures)captureAllocationRecord(m.persistentFailures[size>=1024?1:0],failure,m.elf);
        failure.publish();
      }) == ESP_OK;
}
} // namespace forkrot
