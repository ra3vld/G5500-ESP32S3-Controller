#pragma once
#include "Arduino.h"
#include <deque>
#include <functional>
using esp_err_t = int;
inline constexpr int ESP_OK = 0, ESP_SPP_SUCCESS = 0;
enum esp_spp_cb_event_t { ESP_SPP_SRV_OPEN_EVT, ESP_SPP_CLOSE_EVT, ESP_SPP_WRITE_EVT, ESP_SPP_CONG_EVT };
struct esp_spp_cb_param_t {
  struct { uint32_t handle = 0; } srv_open;
  struct { uint32_t handle = 0; bool async = true; int status = ESP_SPP_SUCCESS; } close;
  struct { uint32_t handle = 0; bool cong = false; } write, cong;
};
using esp_spp_cb_t = void (*)(esp_spp_cb_event_t, esp_spp_cb_param_t *);
struct FakeSpp {
  bool beginOk = true, connected = false, input = true, output = true, master = true, bleDisabled = false;
  int writeResult = ESP_OK;
  std::string name;
  std::deque<unsigned char> rx;
  std::vector<std::pair<uint32_t, std::string>> writes;
  esp_spp_cb_t callback = nullptr;
  std::function<void(const uint8_t *, size_t)> dataCallback;
};
inline FakeSpp fakeSpp;
class BluetoothSerial {
public:
  void register_callback(esp_spp_cb_t callback) { fakeSpp.callback = callback; }
  void enableSSP(bool input, bool output) { fakeSpp.input = input; fakeSpp.output = output; }
  void onData(std::function<void(const uint8_t *, size_t)> callback) { fakeSpp.dataCallback = callback; }
  bool begin(const String &name, bool master, bool disableBLE) {
    fakeSpp.name = name.c_str(); fakeSpp.master = master; fakeSpp.bleDisabled = disableBLE;
    return fakeSpp.beginOk;
  }
  bool hasClient() { return fakeSpp.connected; }
  int available() { return fakeSpp.rx.size(); }
  int read() { if (fakeSpp.rx.empty()) return -1; int byte = fakeSpp.rx.front(); fakeSpp.rx.pop_front(); return byte; }
};
inline esp_err_t esp_spp_write(uint32_t client, int size, uint8_t *bytes) {
  if (fakeSpp.writeResult == ESP_OK) fakeSpp.writes.emplace_back(client, std::string(reinterpret_cast<char *>(bytes), size));
  return fakeSpp.writeResult;
}
inline void sppConnect(uint32_t client) {
  fakeSpp.connected = true;
  esp_spp_cb_param_t param; param.srv_open.handle = client;
  fakeSpp.callback(ESP_SPP_SRV_OPEN_EVT, &param);
}
inline void sppClose(uint32_t client, bool active = true) {
  if (active) fakeSpp.connected = false;
  esp_spp_cb_param_t param; param.close.handle = client;
  fakeSpp.callback(ESP_SPP_CLOSE_EVT, &param);
}
inline void sppComplete(uint32_t client, bool congested = false) {
  esp_spp_cb_param_t param; param.write.handle = client; param.write.cong = congested;
  fakeSpp.callback(ESP_SPP_WRITE_EVT, &param);
}
inline void sppCongestion(uint32_t client, bool congested) {
  esp_spp_cb_param_t param; param.cong.handle = client; param.cong.cong = congested;
  fakeSpp.callback(ESP_SPP_CONG_EVT, &param);
}
inline void sppFeed(const std::string &bytes) { fakeSpp.dataCallback(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()); }
