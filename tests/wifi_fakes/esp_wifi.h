#pragma once
using esp_err_t=int;
inline constexpr int ESP_OK=0,ESP_FAIL=-1;
struct wifi_init_config_t {
  int nvs_enable=1,static_rx_buf_num=10,dynamic_rx_buf_num=32,tx_buf_type=1;
  int static_tx_buf_num=0,dynamic_tx_buf_num=32,cache_tx_buf_num=32;
  int rx_mgmt_buf_num=5,mgmt_sbuf_num=32,ampdu_rx_enable=1,ampdu_tx_enable=1;
  int amsdu_tx_enable=1,rx_ba_win=6;
};
#define WIFI_INIT_CONFIG_DEFAULT() wifi_init_config_t{}
inline wifi_init_config_t fakeWifiConfig;
inline unsigned fakeWifiCalls=0;
inline esp_err_t fakeWifiResult=ESP_OK;
inline esp_err_t esp_wifi_init(const wifi_init_config_t *cfg) {
  ++fakeWifiCalls;fakeWifiConfig=*cfg;return fakeWifiResult;
}
