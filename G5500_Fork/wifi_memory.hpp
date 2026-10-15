// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "board.hpp"
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
#include <Network.h>
#include <esp_wifi.h>
namespace forkrot {
inline esp_err_t initCompactWifi() {
  if (!::Network.begin()) return ESP_FAIL;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.nvs_enable = 0; // credentials are loaded by Storage; driver settings live in RAM
  cfg.static_rx_buf_num = 2;
  cfg.dynamic_rx_buf_num = 4;
  cfg.tx_buf_type = 1;
  cfg.static_tx_buf_num = 0;
  cfg.dynamic_tx_buf_num = 2;
  cfg.cache_tx_buf_num = 2;
  cfg.rx_mgmt_buf_num = 2;
  cfg.mgmt_sbuf_num = 8;
  cfg.ampdu_rx_enable = 0;
  cfg.ampdu_tx_enable = 0;
  cfg.amsdu_tx_enable = 0;
  // IDF init is idempotent. Arduino subsequently attaches its events/netifs
  // through WiFi.mode(), while this initial buffer configuration is retained.
  return esp_wifi_init(&cfg);
}
} // namespace forkrot
#endif
