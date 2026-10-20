#include "wifi_memory.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main() {
  assert(initControllerWifi()==ESP_OK);
  const auto controller=fakeWifiConfig;
  assert(controller.nvs_enable==0 && controller.tx_buf_type==1 && controller.static_tx_buf_num==0);
  assert(controller.static_rx_buf_num*1600<=6400);
  assert((controller.dynamic_rx_buf_num+controller.dynamic_tx_buf_num)*1600<=25600);
  assert(controller.rx_ba_win<=controller.static_rx_buf_num && controller.rx_ba_win<=controller.dynamic_rx_buf_num);
  assert(initCompactWifi()==ESP_OK);
  assert(fakeWifiConfig.static_rx_buf_num<=controller.static_rx_buf_num);
  assert(fakeWifiConfig.dynamic_rx_buf_num+fakeWifiConfig.dynamic_tx_buf_num<=
         controller.dynamic_rx_buf_num+controller.dynamic_tx_buf_num);
  assert(!fakeWifiConfig.ampdu_rx_enable && !fakeWifiConfig.ampdu_tx_enable);
  assert(initBufferedWifi()==ESP_OK);
  assert(fakeWifiConfig.dynamic_rx_buf_num==64 && fakeWifiConfig.dynamic_tx_buf_num==64);
  const auto calls=fakeWifiCalls;Network.ready=false;
  assert(initControllerWifi()==ESP_FAIL && initCompactWifi()==ESP_FAIL && initBufferedWifi()==ESP_FAIL);
  assert(fakeWifiCalls==calls);
  Network.ready=true;fakeWifiResult=ESP_FAIL;
  assert(initControllerWifi()==ESP_FAIL && fakeWifiCalls==calls+1); // no larger fallback
  std::cout<<"PASS: bounded classic Wi-Fi burst memory, BA window, compact/rich profiles, init failure\n";
}
