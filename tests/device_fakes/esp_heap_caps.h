#pragma once
#include "../http_fakes/esp_heap_caps.h"
inline constexpr int MALLOC_CAP_SPIRAM=4;
inline size_t heap_caps_get_total_size(int) {return 0;}
