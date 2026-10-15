// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "bluetooth.hpp"
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
#include <esp32-hal-bt.h>
#include <esp_bt_main.h>
// Tell Arduino's startup not to release memory needed by our Classic host.
#if __has_include(<esp32-hal-alloc-bt-classic-mem.h>)
#include <esp32-hal-alloc-bt-classic-mem.h>
#endif
#endif
namespace forkrot {
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
Bluetooth *Bluetooth::owner_ = nullptr;
void Bluetooth::onEvent(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (!owner_) return;
  auto &self = *owner_;
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
    self.onData(param->data_ind.data, param->data_ind.len);
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
  // Same no-input/no-output SSP pairing as before; no Bluetooth PIN in the UI.
  if (event == ESP_BT_GAP_CFM_REQ_EVT)
    esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
}
void Bluetooth::onData(const uint8_t *bytes, size_t size) {
  for (size_t i = 0; i < size; ++i) {
    std::string command;
    if (!receiver_.feed(static_cast<char>(bytes[i]), command)) continue;
    uint32_t head = rxHead_.load(std::memory_order_relaxed);
    if (uint32_t(head - rxTail_.load(std::memory_order_acquire)) == RX_SLOTS) {
      rxDropped_.fetch_add(1);
      continue;
    }
    commands_[head % RX_SLOTS] = {std::move(command), session_.load()};
    rxHead_.store(head + 1, std::memory_order_release);
  }
}
#endif
bool Bluetooth::begin() {
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
  owner_ = this;
  // Keep the IDF SPP transport, but avoid BluetoothSerial's unused 4096-byte
  // TX task, byte RX queue, TX queue, semaphore and two event groups.
  if (!btStarted() && !btStartMode(BT_MODE_CLASSIC_BT)) return false;
  auto state = esp_bluedroid_get_status();
  if (state == ESP_BLUEDROID_STATUS_UNINITIALIZED && esp_bluedroid_init() != ESP_OK) return false;
  if (state != ESP_BLUEDROID_STATUS_ENABLED && esp_bluedroid_enable() != ESP_OK) return false;
  if (esp_bt_gap_register_callback(onGapEvent) != ESP_OK ||
      esp_spp_register_callback(onEvent) != ESP_OK ||
      esp_bt_gap_set_device_name(board::BLUETOOTH_NAME) != ESP_OK) return false;
  esp_bt_io_cap_t capability = ESP_BT_IO_CAP_NONE;
  if (esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &capability, sizeof capability) != ESP_OK)
    return false;
  esp_bt_cod_t cod{};
  cod.major = 1; cod.minor = 4; cod.service = 0x16;
  if (esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD) != ESP_OK) return false;
  esp_spp_cfg_t cfg = BT_SPP_DEFAULT_CONFIG();
  cfg.mode = ESP_SPP_MODE_CB;
  if (esp_spp_enhanced_init(&cfg) != ESP_OK) return false;
  const uint32_t started = millis();
  while (!ready_.load() && uint32_t(millis() - started) < 1500) delay(1);
  return ready_.load(); // server readiness arrives in START_EVT
#elif BLUETOOTH_BACKEND == BT_HC05
  pinMode(board::PIN_HC05_STATE, INPUT);
  serial_.begin(board::HC05_BAUD, SERIAL_8N1, board::PIN_HC05_RX, board::PIN_HC05_TX);
  ready_ = true;
  return true;
#else
  return true;
#endif
}
bool Bluetooth::connected() {
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
  return ready_.load() && client_.load() != 0;
#elif BLUETOOTH_BACKEND == BT_HC05
  return ready_ && digitalRead(board::PIN_HC05_STATE) == HIGH;
#else
  return false;
#endif
}
void Bluetooth::service() {
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
  uint32_t session = session_.load();
  if (session != seenSession_) {
    for (auto &reply : replies_) reply = String();
    txHead_ = txCount_ = 0;
    seenSession_ = session;
  }
  uint32_t client = client_.load();
  if (!ready_ || !client || !txCount_ || writing_.load() || congested_.load()) return;
  const String &reply = replies_[txHead_];
  writing_.store(true);
  // esp_spp_write queues a copy and returns immediately. BluetoothSerial::print can
  // wait up to a second for its TX queue; keep that wait out of the control loop.
  esp_err_t result = esp_spp_write(client, reply.length(),
                                 reinterpret_cast<uint8_t *>(const_cast<char *>(reply.c_str())));
  if (result != ESP_OK) writing_.store(false);
  replies_[txHead_] = String();
  txHead_ = (txHead_ + 1) % TX_SLOTS;
  --txCount_;
#endif
}
bool Bluetooth::receive(std::string &command) {
  service();
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
  if (!ready_) return false;
  uint32_t tail = rxTail_.load(std::memory_order_relaxed);
  while (tail != rxHead_.load(std::memory_order_acquire)) {
    auto &line = commands_[tail % RX_SLOTS];
    uint32_t session = line.session;
    command = std::move(line.command);
    rxTail_.store(++tail, std::memory_order_release);
    if (client_.load() && session == session_.load()) return true;
  }
#elif BLUETOOTH_BACKEND == BT_HC05
  if (!ready_) return false;
  while (serial_.available()) {
    int byte = serial_.read();
    if (byte >= 0 && receiver_.feed(static_cast<char>(byte), command)) return true;
  }
#else
  (void)command;
#endif
  return false;
}
bool Bluetooth::send(const String &reply) {
#if BLUETOOTH_BACKEND == BT_ESP32_SPP
  service();
  if (!connected() || reply.length() > 512 || txCount_ == TX_SLOTS) return false;
  replies_[(txHead_ + txCount_) % TX_SLOTS] = reply;
  ++txCount_;
  service();
  return true;
#elif BLUETOOTH_BACKEND == BT_HC05
  return ready_ && serial_.print(reply) == reply.length();
#else
  (void)reply;
  return false;
#endif
}
}
