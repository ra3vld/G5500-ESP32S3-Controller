// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "bluetooth.hpp"
#if BLUETOOTH_SUPPORT
#include <cstring>
#include <new>
#if SELECTED_BOARD == BOARD_ESP32
#include <esp32-hal-bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#ifdef ARDUINO
#include <esp_bt.h>
#endif
// Tell Arduino's startup not to release memory needed by our Classic host.
#if __has_include(<esp32-hal-alloc-bt-classic-mem.h>)
#include <esp32-hal-alloc-bt-classic-mem.h>
#endif
#endif
namespace forkrot {
#if SELECTED_BOARD == BOARD_ESP32
Bluetooth *Bluetooth::owner_ = nullptr;
void Bluetooth::onEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (!owner_ || !owner_->spp_) return;
  auto &self = *owner_->spp_;
  if (event == ESP_SPP_INIT_EVT) {
    if (param->init.status == ESP_SPP_SUCCESS)
      esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "ESP32SPP");
  } else if (event == ESP_SPP_START_EVT) {
    self.ready_.store(param->start.status == ESP_SPP_SUCCESS);
    if (self.ready_.load())
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  } else if (event == ESP_SPP_SRV_OPEN_EVT && param->srv_open.status != ESP_SPP_SUCCESS) {
    return;
  } else if (event == ESP_SPP_SRV_OPEN_EVT && self.client_.load() == 0) {
    self.receiver_.clear();
    self.session_.fetch_add(1);
    self.writing_.store(false);
    self.congested_.store(false);
    self.client_.store(param->srv_open.handle);
  } else if (event == ESP_SPP_SRV_OPEN_EVT) {
    esp_spp_disconnect(param->srv_open.handle);
  } else if (event == ESP_SPP_DATA_IND_EVT && param->data_ind.handle == self.client_.load()) {
    owner_->onData(param->data_ind.data, param->data_ind.len);
  } else if (event == ESP_SPP_CLOSE_EVT && param->close.handle == self.client_.load() &&
             (param->close.async || param->close.status == ESP_SPP_SUCCESS)) {
    self.client_.store(0);
    self.receiver_.clear();
    self.session_.fetch_add(1);
  } else if (event == ESP_SPP_WRITE_EVT && param->write.handle == self.client_.load()) {
    self.congested_.store(param->write.cong);
    self.writing_.store(false);
  } else if (event == ESP_SPP_CONG_EVT && param->cong.handle == self.client_.load()) {
    self.congested_.store(param->cong.cong);
  }
}
void Bluetooth::onGapEvent(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  if (!owner_ || !owner_->spp_) return;
  auto &self = *owner_->spp_;
  if (event == ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT && param->acl_conn_cmpl_stat.stat == ESP_BT_STATUS_SUCCESS) {
    const uint32_t key = uint32_t(param->acl_conn_cmpl_stat.handle) + 1;
    for (auto handle : self.aclHandles_) if (handle == key) return;
    for (auto &handle : self.aclHandles_) if (!handle) {
      handle = key;
      self.aclCount_.fetch_add(1);
      break;
    }
  } else if (event == ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT) {
    const uint32_t key = uint32_t(param->acl_disconn_cmpl_stat.handle) + 1;
    for (auto &handle : self.aclHandles_) if (handle == key) {
      handle = 0;
      self.aclCount_.fetch_sub(1);
      break;
    }
  }
  // Same no-input/no-output SSP pairing as before; no Bluetooth PIN in the UI.
  if (event == ESP_BT_GAP_CFM_REQ_EVT)
    esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
}
void Bluetooth::onData(const uint8_t *bytes, size_t size) {
  const auto session = spp_->session_.load();
  if (spp_->receiverSession_ != session) {
    spp_->receiver_.cancelPending();
    spp_->receiverSession_ = session;
  }
  for (size_t i = 0; i < size; ++i) {
    std::string_view command;
    if (spp_->receiver_.feed(static_cast<char>(bytes[i]), command) && !spp_->commands_.push(command, session))
      spp_->rxDropped_.fetch_add(1);
  }
}
#endif
bool Bluetooth::begin(uint8_t backend, const PinMap &pins) {
  backend_ = BT_NONE;
#if SELECTED_BOARD == BOARD_ESP32
  if (backend != BT_ESP32_SPP) {
#ifdef ARDUINO
    // The core has already released unused BLE memory. Its wrapper tracks that
    // release; BTDM here returns the remaining Classic/host memory to the heap.
    if (esp_bt_mem_release(ESP_BT_MODE_BTDM) != ESP_OK) return false;
#endif
  }
  if (backend == BT_ESP32_SPP) {
    spp_.reset(new (std::nothrow) SppState);
    if (!spp_) return false;
    owner_ = this; backend_ = backend;
    bool ok = [&] {
      if (!btStarted() && !btStartMode(BT_MODE_CLASSIC_BT)) return false;
      auto state = esp_bluedroid_get_status();
      if (state == ESP_BLUEDROID_STATUS_UNINITIALIZED && esp_bluedroid_init() != ESP_OK) return false;
      if (state != ESP_BLUEDROID_STATUS_ENABLED && esp_bluedroid_enable() != ESP_OK) return false;
      if (esp_bt_gap_register_callback(onGapEvent) != ESP_OK || esp_spp_register_callback(onEvent) != ESP_OK ||
          esp_bt_gap_set_device_name(board::BLUETOOTH_NAME) != ESP_OK) return false;
      esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
      if (esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof capability) != ESP_OK) return false;
      esp_bt_cod_t cod{}; cod.major = 1; cod.minor = 4; cod.service = 0x16;
      if (esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD) != ESP_OK) return false;
      esp_spp_cfg_t cfg = BT_SPP_DEFAULT_CONFIG(); cfg.mode = ESP_SPP_MODE_CB;
      if (esp_spp_enhanced_init(&cfg) != ESP_OK) return false;
      const uint32_t started = millis();
      while (!spp_->ready_.load() && uint32_t(millis()-started) < 1500) delay(1);
      return spp_->ready_.load();
    }();
    if (!ok) end();
    return ok;
  }
#endif
  if (backend == BT_HC05) {
    hc_.reset(new (std::nothrow) HcState);
    if (!hc_) return false;
    if(!board::gpio(pins.hc05State) || !board::gpio(pins.hc05Rx) || !board::output(pins.hc05Tx)) { end(); return false; }
    hc_->statePin=pins.hc05State;
    pinMode(hc_->statePin, INPUT);
    hc_->serial.begin(board::HC05_BAUD, SERIAL_8N1, pins.hc05Rx, pins.hc05Tx);
    backend_ = backend; return true;
  }
  return backend == BT_NONE;
}
void Bluetooth::end() {
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) {
#ifdef ARDUINO
    // Deinit joins stack tasks/callbacks before the private queues are released.
    esp_spp_deinit();
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) esp_bluedroid_disable();
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) esp_bluedroid_deinit();
    btStop();
#endif
    if (owner_ == this) owner_ = nullptr;
    spp_.reset();
  }
#endif
  hc_.reset(); backend_ = BT_NONE;
}
bool Bluetooth::connected() {
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) return spp_->ready_.load() && spp_->client_.load() != 0;
#endif
  return hc_ && digitalRead(hc_->statePin) == HIGH;
}
bool Bluetooth::linkConnected() {
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) return spp_->aclCount_.load() != 0 || connected();
#endif
  return connected();
}
bool Bluetooth::macAddress(char (&address)[18]) const {
  address[0] = '\0';
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) {
    const uint8_t *mac = esp_bt_dev_get_address();
    if (!mac) return false;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned i=0; i<6; ++i) {
      address[i*3] = hex[mac[i]>>4]; address[i*3+1] = hex[mac[i]&15]; address[i*3+2] = i==5 ? '\0' : ':';
    }
    return true;
  }
#endif
  return false;
}
void Bluetooth::service() {
#if SELECTED_BOARD == BOARD_ESP32
  if (!spp_) return;
  auto &s = *spp_;
  uint32_t session = s.session_.load();
  if (session != s.seenSession_) { s.replies_.clear(); s.seenSession_ = session; }
  uint32_t client = s.client_.load();
  if (!s.ready_ || !client || s.replies_.empty() || s.writing_.load() || s.congested_.load()) return;
  char reply[512]; size_t size; uint32_t tag;
  if (!s.replies_.pop(reply, sizeof reply, size, tag)) return;
  s.writing_.store(true);
  if (esp_spp_write(client, size, reinterpret_cast<uint8_t *>(reply)) != ESP_OK) s.writing_.store(false);
#endif
}
bool Bluetooth::receive(char *out, size_t capacity, size_t &size) {
  service();
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) {
    auto &s = *spp_; if (!s.ready_) return false;
    uint32_t session;
    for (unsigned count=0; count<8 && s.commands_.pop(out,capacity,size,session); ++count)
      if (s.client_.load() && session == s.session_.load()) return true;
    return false;
  }
#endif
  if (hc_) {
    std::string_view line;
    for (unsigned count=0; count<128 && hc_->serial.available(); ++count) {
      int byte = hc_->serial.read();
      if (byte >= 0 && hc_->receiver.feed(static_cast<char>(byte), line)) {
        if (line.size() > capacity) return false;
        size=line.size(); std::memcpy(out,line.data(),size); return true;
      }
    }
  }
  return false;
}
bool Bluetooth::send(std::string_view reply) {
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) {
    service();
    if (!connected() || reply.size()>512 || !spp_->replies_.push(reply)) return false;
    service(); return true;
  }
#endif
  return hc_ && hc_->serial.write(reinterpret_cast<const uint8_t *>(reply.data()),reply.size()) == reply.size();
}
void Bluetooth::discardInput() {
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) spp_->session_.fetch_add(1);
#endif
  if (hc_) {
    ++inputSession_; hc_->receiver.cancelPending();
    for (unsigned i=0; i<512 && hc_->serial.available(); ++i) hc_->receiver.discard(char(hc_->serial.read()));
    hc_->receiver.cancelPending();
  }
}
uint32_t Bluetooth::sessionId() const {
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) return spp_->session_.load();
#endif
  return inputSession_;
}
uint32_t Bluetooth::droppedCommands() const {
#if SELECTED_BOARD == BOARD_ESP32
  if (spp_) return spp_->rxDropped_.load();
#endif
  return 0;
}
} // namespace forkrot

#endif
