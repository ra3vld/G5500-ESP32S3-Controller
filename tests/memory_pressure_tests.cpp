#include "memory_pressure.hpp"
#include "runtime_metrics.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace forkrot;
int main() {
  MemoryPressure p;p.sample(0,65536,16384);assert(!p.limited);
  p.sample(10,32767,16384);assert(p.limited);
  p.sample(20,65536,16384);p.sample(10019,65536,16384);assert(p.limited);
  p.sample(10020,65536,16384);assert(!p.limited);
  p.sample(UINT32_MAX-5000,65536,8191);p.sample(UINT32_MAX-4999,65536,16384);
  p.sample(5000,65536,16384);assert(!p.limited);
  AllocationRecord rtc[2]{};uint8_t elf[32];std::memset(elf,0x5a,sizeof elf);uint32_t latch=0;
  beginRuntimeMetrics(&latch,123,rtc,elf,false);
  fakeAllocFailureHook(768,3,"heap_caps_malloc");fakeAllocFailureHook(4096,3,"malloc");
  assert(latch==123&&allocationRecordValid(rtc[0])&&allocationRecordValid(rtc[1]));
  assert(rtc[0].size==768&&rtc[1].size==4096&&rtc[1].elf[31]==0x5a);
  fakeAllocFailureHook(8192,3,"later");assert(rtc[1].size==4096);
  auto corrupt=rtc[0];++corrupt.pc[2];assert(!allocationRecordValid(corrupt));
  corrupt=rtc[0];corrupt.magic=1;assert(!allocationRecordValid(corrupt));
  beginRuntimeMetrics(nullptr,0,rtc,elf,true);
  assert(allocationRecordValid(runtimeMetrics.previousFailures[0]));
  assert(allocationRecordValid(runtimeMetrics.previousFailures[1]));
  assert(!allocationRecordValid(rtc[0])&&!allocationRecordValid(rtc[1]));
  rtc[0]=runtimeMetrics.previousFailures[0];beginRuntimeMetrics(nullptr,0,rtc,elf,false);
  assert(!allocationRecordValid(runtimeMetrics.previousFailures[0]));
  std::cout<<"PASS: memory pressure hysteresis/wrap, allocation callback RTC capture, frozen first failures, reboot recovery and corrupt/power-on rejection\n";
}
