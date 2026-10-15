#include "wifi_fallback.hpp"
#include "runtime_metrics.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace forkrot;
int main() {
  WifiFallback policy;
  policy.reset(100, true);
  assert(policy.update(101, true, true, false) == WifiAction::None);
  // Loss immediately restores fallback, and never reconnects every loop.
  assert(policy.update(102, true, false, false) == WifiAction::StartAp);
  for (uint32_t now = 103; now < 30102; ++now)
    assert(policy.update(now, true, false, true) == WifiAction::None);
  assert(policy.update(30102, true, false, true) == WifiAction::StartSta);
  assert(policy.update(38101, true, false, true) == WifiAction::None);
  assert(policy.update(38102, true, false, true) == WifiAction::StartAp);
  assert(policy.update(68102, true, false, true) == WifiAction::StartSta);
  assert(policy.update(68103, true, true, true) == WifiAction::StopAp);
  // If disabling AP fails, retry at most once per second.
  assert(policy.update(68104, true, true, true) == WifiAction::None);
  assert(policy.update(69103, true, true, true) == WifiAction::StopAp);
  assert(policy.update(69104, true, true, false) == WifiAction::None);
  assert(policy.update(69105, true, false, false) == WifiAction::StartAp);
  policy.reset(0, false);
  assert(policy.update(30000, false, false, true) == WifiAction::None);
  assert(policy.update(300000, false, false, true) == WifiAction::None);
  policy.reset(UINT32_MAX - 10000, false);
  assert(policy.update(19998, true, false, true) == WifiAction::None);
  assert(policy.update(19999, true, false, true) == WifiAction::StartSta);
  assert(policy.update(27999, true, false, true) == WifiAction::StartAp);
  beginRuntimeMetrics(); assert(runtimeMetrics.hookReady && fakeAllocFailureHook);
  fakeAllocFailureHook(2308, 3, "heap_caps_malloc");
  assert(runtimeMetrics.failures[1].ready() && !runtimeMetrics.failures[0].ready());
  assert(runtimeMetrics.failures[1].size == 2308 && runtimeMetrics.failures[1].caps == 3);
  assert(runtimeMetrics.failures[1].freeBytes == fakeHeapFree);
  // Later failures must not overwrite a trace being read by H4 on another core.
  std::thread radio([] { for (unsigned i=0;i<10000;++i) fakeAllocFailureHook(4096, 7, "other"); });
  for (unsigned i=0;i<10000;++i) {
    fakeAllocFailureHook(64, 1, "small");
    assert(runtimeMetrics.failures[1].size == 2308 && runtimeMetrics.failures[1].caps == 3);
  }
  radio.join();
  assert(runtimeMetrics.allocationFailures == 20001 && runtimeMetrics.largestFailedAllocation == 4096);
  assert(runtimeMetrics.failures[0].ready() && runtimeMetrics.failures[0].size == 64);
  std::cout << "PASS: STA/AP fallback, retry/deadline/wrap, bounded AP stop retry, immutable OOM samples across threads\n";
}
