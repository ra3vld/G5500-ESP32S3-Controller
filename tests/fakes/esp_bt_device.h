#pragma once
#include "esp_spp_api.h"
inline const uint8_t *esp_bt_dev_get_address() {
  return fakeSpp.addressAvailable ? fakeSpp.address : nullptr;
}
