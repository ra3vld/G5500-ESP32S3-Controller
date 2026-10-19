#include "wifi_fallback.hpp"
#include "network_addresses.hpp"
#include "runtime_metrics.hpp"
#include <cassert>
#include <iostream>
#include <thread>
using namespace forkrot;
int main() {
  char addresses[64];
  formatNetworkAddresses(addresses, sizeof addresses, "10.20.30.40", "192.168.4.1");
  assert(std::strcmp(addresses, "STA: 10.20.30.40\nAP: 192.168.4.1") == 0);
  formatNetworkAddresses(addresses, sizeof addresses, "255.255.255.255", "255.255.255.255", "  ");
  assert(std::strcmp(addresses, "STA: 255.255.255.255  AP: 255.255.255.255") == 0);
  formatNetworkAddresses(addresses, sizeof addresses, "10.0.0.2", "-");
  assert(std::strcmp(addresses, "STA: 10.0.0.2") == 0);
  formatNetworkAddresses(addresses, sizeof addresses, "0.0.0.0", "192.168.4.1");
  assert(std::strcmp(addresses, "AP: 192.168.4.1") == 0);
  formatNetworkAddresses(addresses, sizeof addresses, nullptr, "");
  assert(std::strcmp(addresses, "IP: -") == 0);
  char bounded[8] = "1234567";
  formatNetworkAddresses(bounded + 1, 6, "10.0.0.2", "192.168.4.1");
  assert(bounded[0] == '1' && bounded[6] == 0 && bounded[7] == 0);
  assert(serviceApAddress(0, 0) == 0x0104a8c0u);
  assert(serviceApAddress(0x6304a8c0u, 0x00ffffffu) == 0x01ff1facu);
  assert(serviceApAddress(0x6303a8c0u, 0x0000ffffu) == 0x01ff1facu);
  assert(serviceApAddress(0x6303a8c0u, 0x00ffffffu) == 0x0104a8c0u);
  WifiFallback policy;
  policy.reset(100, true);
  assert(policy.update(101, true, true, false) == WifiAction::None);
  // A short loss retries STA immediately without taking the radio down for 30s.
  assert(policy.update(102, true, false, false) == WifiAction::ReconnectSta);
  for (uint32_t now = 103; now < 8102; ++now)
    assert(policy.update(now, true, false, false) == WifiAction::None);
  assert(policy.update(8102, true, false, false) == WifiAction::StartAp);
  assert(policy.update(38101, true, false, true) == WifiAction::None);
  assert(policy.update(38102, true, false, true) == WifiAction::StartSta);
  assert(policy.update(38103, true, true, true) == WifiAction::StopAp);
  // If disabling AP fails, retry at most once per second.
  assert(policy.update(38104, true, true, true) == WifiAction::None);
  assert(policy.update(39103, true, true, true) == WifiAction::StopAp);
  assert(policy.update(39104, true, true, false) == WifiAction::None);
  assert(policy.update(39105, true, false, false) == WifiAction::ReconnectSta);
  // Recovery inside the attempt window needs no AP or settings change.
  assert(policy.update(39200, true, true, false) == WifiAction::None);
  assert(policy.update(50000, true, true, false) == WifiAction::None);
  assert(policy.update(50001, false, false, false) == WifiAction::StartAp);
  policy.reset(0, false);
  assert(policy.update(30000, false, false, true) == WifiAction::None);
  assert(policy.update(300000, false, false, true) == WifiAction::None);
  policy.reset(UINT32_MAX - 10000, false);
  assert(policy.update(19998, true, false, true) == WifiAction::None);
  assert(policy.update(19999, true, false, true) == WifiAction::StartSta);
  assert(policy.update(27999, true, false, true) == WifiAction::StartAp);
  // S2/S3 and explicitly forced ESP32 keep AP after a successful STA attempt.
  policy.reset(0, false);
  assert(policy.update(30000, false, false, true, true) == WifiAction::None);
  assert(policy.update(30000, true, false, true, true) == WifiAction::StartSta);
  assert(policy.update(30001, true, true, true, true) == WifiAction::None);
  assert(policy.update(90000, true, true, true, true) == WifiAction::None);
  assert(policy.update(90001, true, false, true, true) == WifiAction::ReconnectSta);
  assert(policy.update(98000, true, false, true, true) == WifiAction::None);
  assert(policy.update(98001, true, false, true, true) == WifiAction::StartAp);
  assert(policy.update(128001, true, false, true, true) == WifiAction::StartSta);
  assert(policy.update(136001, true, false, true, true) == WifiAction::StartAp);
  assert(policy.update(136002, true, true, true, true) == WifiAction::None);
  // Saving credentials starts immediately and retains AP through success,
  // a failed attempt, and subsequent automatic retries.
  policy.beginAttempt(200000);
  assert(policy.trying());
  assert(policy.update(207999, true, false, true, true) == WifiAction::None);
  assert(policy.update(208000, true, false, true, true) == WifiAction::StartAp);
  assert(!policy.trying());
  assert(policy.update(238000, true, false, true, true) == WifiAction::StartSta);
  assert(policy.trying());
  assert(policy.update(238001, true, true, true, true) == WifiAction::None);
  assert(!policy.trying());
  uint32_t recoveryWord = 0;
  beginRuntimeMetrics(&recoveryWord, 0x52335648u); assert(runtimeMetrics.hookReady && fakeAllocFailureHook);
  fakeAllocFailureHook(2308, 3, "heap_caps_malloc");
  assert(__atomic_load_n(&recoveryWord, __ATOMIC_RELAXED) == 0x52335648u);
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
  assert(__atomic_load_n(&recoveryWord, __ATOMIC_RELAXED) == 0x52335648u);
  assert(runtimeMetrics.allocationFailures == 20001 && runtimeMetrics.largestFailedAllocation == 4096);
  assert(runtimeMetrics.failures[0].ready() && runtimeMetrics.failures[0].size == 64);
  std::cout << "PASS: immediate STA recovery, fallback/always-on AP, retry/deadline/wrap, bounded AP stop retry, immutable OOM samples across threads\n";
}
