#pragma once
#include <chrono>
inline int64_t fakeEspTimeUs=-1;
inline int64_t esp_timer_get_time() {
  if(fakeEspTimeUs>=0)return fakeEspTimeUs;
  return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}
