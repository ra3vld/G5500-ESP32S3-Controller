#pragma once
#include "../device_fakes/esp_system.h"
inline constexpr int ESP_RST_POWERON=1, ESP_RST_EXT=2, ESP_RST_SW=3, ESP_RST_PANIC=4;
inline constexpr int ESP_RST_INT_WDT=5, ESP_RST_TASK_WDT=6, ESP_RST_WDT=7;
inline constexpr int ESP_RST_DEEPSLEEP=8, ESP_RST_BROWNOUT=9, ESP_RST_SDIO=10, ESP_RST_USB=11;
inline esp_reset_reason_t fakeResetReason=ESP_RST_USB;
inline esp_reset_reason_t esp_reset_reason() {return fakeResetReason;}
