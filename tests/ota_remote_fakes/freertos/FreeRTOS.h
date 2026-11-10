#pragma once
#include <chrono>
#include <thread>
#include <vector>
inline constexpr int pdPASS=1;
#define pdMS_TO_TICKS(n) (n)
inline std::vector<std::thread> otaWorkers;
inline bool otaTaskFailure=false;
