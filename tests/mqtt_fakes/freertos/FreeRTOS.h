#pragma once
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
inline constexpr int pdTRUE=1,pdPASS=1;
inline unsigned pdMS_TO_TICKS(unsigned ms){return ms;}
struct FakeMqttTask {
  std::thread thread;
  std::mutex mutex;
  std::condition_variable changed;
  bool notified=false;
  ~FakeMqttTask(){if(thread.joinable())thread.join();}
};
using TaskHandle_t=FakeMqttTask *;
inline thread_local TaskHandle_t fakeCurrentTask=nullptr;
inline std::vector<std::unique_ptr<FakeMqttTask>> fakeMqttTasks;
inline bool fakeMqttTaskFailure=false;
