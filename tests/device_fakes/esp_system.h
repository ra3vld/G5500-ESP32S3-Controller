#pragma once
#include <cstdint>
using esp_reset_reason_t=int;
inline constexpr int ESP_RST_UNKNOWN=0;
inline uint32_t esp_random() {static uint32_t n=0;return ++n;}
