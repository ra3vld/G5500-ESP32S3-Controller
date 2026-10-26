// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "board.hpp"
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
inline esp_err_t initBufferedWifi() {
  if (!::Network.begin()) return ESP_FAIL;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.nvs_enable = 0;
  // Reserve more burst capacity for HUD telemetry and simultaneous web traffic.
  // Dynamic buffers consume memory only while packets are pending.
  cfg.static_rx_buf_num = 8;
  cfg.dynamic_rx_buf_num = 64;
  cfg.tx_buf_type = 1;
  cfg.static_tx_buf_num = 0;
  cfg.dynamic_tx_buf_num = 64;
  cfg.cache_tx_buf_num = 16;
  return esp_wifi_init(&cfg);
}
inline esp_err_t initControllerWifi() {
  if (!::Network.begin()) return ESP_FAIL;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  cfg.nvs_enable = 0;
  // Keep bursts bounded on classic ESP32 without PSRAM. Both directions count:
  // 64 RX + 64 TX buffers can otherwise consume nearly 200 KiB at packet MTU.
  cfg.static_rx_buf_num = 4;
  cfg.dynamic_rx_buf_num = 8;
  cfg.tx_buf_type = 1;
  cfg.static_tx_buf_num = 0;
  cfg.dynamic_tx_buf_num = 8;
  cfg.cache_tx_buf_num = 4;
  // Tiny pools prioritize command/ACK latency over aggregated bursts. Avoid
  // A-MPDU reorder/window state with the minimum 4 RX / 8 dynamic buffers.
  cfg.ampdu_rx_enable = 0;
  cfg.ampdu_tx_enable = 0;
  cfg.rx_ba_win = 0;
  cfg.amsdu_tx_enable = 0;
  return esp_wifi_init(&cfg);
}
} // namespace forkrot
