#pragma once
#include "Arduino.h"
using esp_err_t = int;
inline constexpr int ESP_OK = 0, ESP_SPP_SUCCESS = 0;
enum esp_spp_cb_event_t { ESP_SPP_INIT_EVT, ESP_SPP_START_EVT, ESP_SPP_DATA_IND_EVT, ESP_SPP_SRV_OPEN_EVT, ESP_SPP_CLOSE_EVT, ESP_SPP_WRITE_EVT, ESP_SPP_CONG_EVT };
struct esp_spp_cb_param_t {
  struct { int status = ESP_SPP_SUCCESS; } init, start;
  struct { uint32_t handle = 0; int len = 0; uint8_t *data = nullptr; } data_ind;
  struct { uint32_t handle = 0; int status = ESP_SPP_SUCCESS; } srv_open;
  struct { uint32_t handle = 0; bool async = true; int status = ESP_SPP_SUCCESS; } close;
  struct { uint32_t handle = 0; bool cong = false; } write, cong;
};
using esp_spp_cb_t = void (*)(esp_spp_cb_event_t, esp_spp_cb_param_t *);
struct FakeSpp {
  bool beginOk = true, startOk = true, connected = false, input = true, output = true, master = true, bleDisabled = false;
  int writeResult = ESP_OK;
  uint32_t client = 0;
  unsigned starts = 0;
  bool callbackMode = false, pairingConfirmed = false;
  bool addressAvailable = true;
  uint8_t address[6] = {0x02, 0x0a, 0xb0, 0xc1, 0x00, 0xff};
  std::vector<uint32_t> disconnected;
  std::string name;
  std::vector<std::pair<uint32_t, std::string>> writes;
  esp_spp_cb_t callback = nullptr;
};
inline FakeSpp fakeSpp;
inline constexpr int ESP_SPP_SEC_NONE = 0, ESP_SPP_ROLE_SLAVE = 1, ESP_SPP_MODE_CB = 0;
struct esp_spp_cfg_t { int mode = 1; bool enable_l2cap_ertm = true; int tx_buffer_size = 990; };
#define BT_SPP_DEFAULT_CONFIG() esp_spp_cfg_t{}
inline esp_err_t esp_spp_register_callback(esp_spp_cb_t cb) { fakeSpp.callback = cb; return ESP_OK; }
inline esp_err_t esp_spp_start_srv(int, int role, int, const char *) {
  ++fakeSpp.starts; fakeSpp.master = role != ESP_SPP_ROLE_SLAVE;
  esp_spp_cb_param_t param; param.start.status = fakeSpp.startOk ? ESP_SPP_SUCCESS : -1;
  fakeSpp.callback(ESP_SPP_START_EVT, &param); return ESP_OK;
}
inline esp_err_t esp_spp_enhanced_init(const esp_spp_cfg_t *cfg) {
  fakeSpp.callbackMode = cfg->mode == ESP_SPP_MODE_CB;
  if (!fakeSpp.beginOk) return -1;
  esp_spp_cb_param_t param; fakeSpp.callback(ESP_SPP_INIT_EVT, &param); return ESP_OK;
}
inline esp_err_t esp_spp_disconnect(uint32_t handle) { fakeSpp.disconnected.push_back(handle); return ESP_OK; }
inline bool btStarted() { return false; }
inline constexpr int BT_MODE_CLASSIC_BT = 2;
inline bool btStartMode(int mode) { fakeSpp.bleDisabled = mode == BT_MODE_CLASSIC_BT; return true; }
inline constexpr int ESP_BLUEDROID_STATUS_UNINITIALIZED = 0, ESP_BLUEDROID_STATUS_ENABLED = 2;
inline int esp_bluedroid_get_status() { return ESP_BLUEDROID_STATUS_UNINITIALIZED; }
inline esp_err_t esp_bluedroid_init() { return ESP_OK; }
inline esp_err_t esp_bluedroid_enable() { return ESP_OK; }
inline constexpr int ESP_BT_STATUS_SUCCESS = 0;
enum esp_bt_gap_cb_event_t { ESP_BT_GAP_CFM_REQ_EVT, ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT, ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT };
struct esp_bt_gap_cb_param_t {
  struct { uint8_t bda[6]{}; } cfm_req;
  struct { int stat = ESP_BT_STATUS_SUCCESS; uint16_t handle = 0; } acl_conn_cmpl_stat;
  struct { uint16_t handle = 0; } acl_disconn_cmpl_stat;
};
using esp_bt_gap_cb_t = void (*)(esp_bt_gap_cb_event_t, esp_bt_gap_cb_param_t *);
inline esp_bt_gap_cb_t fakeGapCallback = nullptr;
inline void aclConnect(uint16_t handle, bool success = true) {
  esp_bt_gap_cb_param_t param; param.acl_conn_cmpl_stat.handle = handle;
  param.acl_conn_cmpl_stat.stat = success ? ESP_BT_STATUS_SUCCESS : -1;
  fakeGapCallback(ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT, &param);
}
inline void aclClose(uint16_t handle) {
  esp_bt_gap_cb_param_t param; param.acl_disconn_cmpl_stat.handle = handle;
  fakeGapCallback(ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT, &param);
}
inline esp_err_t esp_bt_gap_register_callback(esp_bt_gap_cb_t cb) { fakeGapCallback = cb; return ESP_OK; }
inline esp_err_t esp_bt_gap_set_device_name(const char *name) { fakeSpp.name = name; return ESP_OK; }
using esp_bt_io_cap_t = uint8_t;
inline constexpr esp_bt_io_cap_t ESP_BT_IO_CAP_NONE = 3;
inline constexpr int ESP_BT_SP_IOCAP_MODE = 0, ESP_BT_INIT_COD = 0, ESP_BT_CONNECTABLE = 0, ESP_BT_GENERAL_DISCOVERABLE = 0;
struct esp_bt_cod_t { int major = 0, minor = 0, service = 0; };
inline esp_err_t esp_bt_gap_set_security_param(int, void *value, unsigned) {
  fakeSpp.input = fakeSpp.output = *static_cast<uint8_t *>(value) != ESP_BT_IO_CAP_NONE; return ESP_OK;
}
inline esp_err_t esp_bt_gap_set_cod(esp_bt_cod_t, int) { return ESP_OK; }
inline esp_err_t esp_bt_gap_set_scan_mode(int, int) { return ESP_OK; }
inline esp_err_t esp_bt_gap_ssp_confirm_reply(uint8_t *, bool accept) { fakeSpp.pairingConfirmed = accept; return ESP_OK; }
inline void delay(unsigned ms) { fakeHardware.now += ms; }
inline esp_err_t esp_spp_write(uint32_t client, int size, uint8_t *bytes) {
  if (fakeSpp.writeResult == ESP_OK) fakeSpp.writes.emplace_back(client, std::string(reinterpret_cast<char *>(bytes), size));
  return fakeSpp.writeResult;
}
inline void sppConnect(uint32_t client) {
  fakeSpp.connected = true; fakeSpp.client = client;
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
inline void sppFeed(const std::string &bytes, uint32_t handle = 0) {
  esp_spp_cb_param_t param; param.data_ind.handle = handle ? handle : fakeSpp.client;
  param.data_ind.len = bytes.size(); param.data_ind.data = reinterpret_cast<uint8_t *>(const_cast<char *>(bytes.data()));
  fakeSpp.callback(ESP_SPP_DATA_IND_EVT, &param);
}
