// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "ota.hpp"
#if OTA_SUPPORT
#if ARDUINO_OTA_SUPPORT
#include <ArduinoOTA.h>
#else
#include <Arduino.h>
#include <Update.h>
#endif
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_app_format.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace forkrot {
void Ota::begin(Stop stop, void *context) {
  if (initialized_) return;
  initialized_ = true; stop_ = stop; context_ = context;
  capacity_.flash = ESP.getFlashChipSize();
  capacity_.image = ESP.getSketchSize();
  const auto *running = esp_ota_get_running_partition();
  const auto *next = esp_ota_get_next_update_partition(nullptr);
  capacity_.separate = next && running && next->address != running->address;
  const auto *metadata = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  capacity_.metadata = metadata && metadata->size >= 0x2000;
  auto it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  while (it) {
    const auto *p = esp_partition_get(it);
    if (p->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN && p->subtype < ESP_PARTITION_SUBTYPE_APP_OTA_MAX &&
        p->address <= capacity_.flash && p->size <= capacity_.flash - p->address) {
      ++capacity_.slots;
      capacity_.slot = capacity_.slot ? std::min(capacity_.slot, p->size) : p->size;
    }
    it = esp_partition_next(it);
  }
  for (unsigned i = 0; i < 4; ++i) std::snprintf(token_ + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
  std::snprintf(hostname_, sizeof hostname_, "g5500-%06lx", static_cast<unsigned long>(ESP.getEfuseMac() & 0xffffff));
}
void Ota::lock() {
  if (!locked_) { locked_ = true; if (stop_) stop_(context_); }
}
void Ota::fail(const char *error) {
  Update.abort(); writing_ = false; error_ = error;
}
const char *Ota::start(uint32_t size) {
  if (!capacity_.available()) return capacity_.reason();
  if (locked_ || Update.isRunning()) return "update_locked";
  if (size < sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) || size > capacity_.slot) return "image_size";
#if ARDUINO_OTA_SUPPORT
  if (arduino_) { ArduinoOTA.end(); arduino_ = false; }
#endif
  lock(); total_ = size; received_ = 0; error_ = ""; touched_ = millis();
  if (!Update.begin(size, U_FLASH)) { fail("begin_failed"); return error_; }
  writing_ = true;
  return nullptr;
}
const char *Ota::write(uint32_t offset, std::string_view data) {
  if (!writing_) return "no_update";
  if (offset != received_) return "wrong_offset";
  if (data.empty() || data.size() > OTA_CHUNK_SIZE || data.size() > total_ - received_) return "chunk_size";
  if (!received_) {
    if (data.size() < sizeof(esp_image_header_t)) { fail("invalid_image"); return error_; }
    esp_image_header_t header;
    std::memcpy(&header, data.data(), sizeof header);
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
      fail("wrong_chip"); return error_;
    }
  }
  if (Update.write(reinterpret_cast<uint8_t *>(const_cast<char *>(data.data())), data.size()) != data.size()) {
    fail("write_failed"); return error_;
  }
  received_ += data.size(); touched_ = millis();
  return nullptr;
}
const char *Ota::finish() {
  if (!writing_) return "no_update";
  if (received_ != total_) return "incomplete_image";
  // end() validates the complete ESP image before changing the boot slot.
  if (!Update.end()) { fail("invalid_image"); return error_; }
  writing_ = false; complete_ = true; reboot();
  return nullptr;
}
void Ota::cancel() { if (writing_) fail("cancelled"); }
void Ota::reboot() { restartAt_ = millis(); restarting_ = true; }
bool Ota::readyToRestart() const { return restarting_ && uint32_t(millis() - restartAt_) >= 1500; }
const char *Ota::armArduino() {
#if ARDUINO_OTA_SUPPORT

  if (!capacity_.available()) return capacity_.reason();
  if (locked_ || Update.isRunning()) return "update_locked";
  if (arduino_) ArduinoOTA.end();
  for (unsigned i = 0; i < 2; ++i) std::snprintf(password_ + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
  ArduinoOTA.setHostname(hostname_).setPassword(password_).setRebootOnSuccess(false).setTimeout(10000);
  ArduinoOTA.onStart([this] {
    lock(); error_ = "";
    if (ArduinoOTA.getCommand() != U_FLASH) fail("firmware_only");
  });
  ArduinoOTA.onProgress([this](unsigned received, unsigned total) { received_ = received; total_ = total; });
  ArduinoOTA.onEnd([this] { complete_ = true; reboot(); });
  ArduinoOTA.onError([this](ota_error_t error) {
    // A failed authentication never interrupts the running controller.
    if (locked_) fail(error == OTA_CONNECT_ERROR ? "connect_failed" : "arduino_failed");
  });
  ArduinoOTA.begin(); arduino_ = true; armedAt_ = millis();
  return nullptr;

#else
return "feature_disabled";
#endif
}
void Ota::service() {
  if (writing_ && uint32_t(millis() - touched_) >= 30000) fail("upload_timeout");
#if ARDUINO_OTA_SUPPORT
  if (arduino_ && !locked_) {
    if (uint32_t(millis() - armedAt_) >= 600000) { ArduinoOTA.end(); arduino_ = false; }
    else ArduinoOTA.handle();
  }
#endif
}
size_t Ota::status(char *data, size_t capacity) const {
  const int n = std::snprintf(data, capacity,
      "{\"available\":%s,\"reason\":\"%s\",\"flashBytes\":%lu,\"imageBytes\":%lu,\"slotBytes\":%lu,"
      "\"minimumFlashBytes\":%lu,\"recommendedFlashBytes\":%lu,\"chunkBytes\":%lu,\"chipId\":%u,"
      "\"token\":\"%s\",\"locked\":%s,\"writing\":%s,\"complete\":%s,\"receivedBytes\":%lu,"
      "\"totalBytes\":%lu,\"error\":\"%s\",\"arduinoEnabled\":%s,\"hostname\":\"%s\",\"password\":\"%s\"}",
      capacity_.available() ? "true" : "false", capacity_.reason(),
      static_cast<unsigned long>(capacity_.flash), static_cast<unsigned long>(capacity_.image), static_cast<unsigned long>(capacity_.slot),
      static_cast<unsigned long>(capacity_.minimumFlash()), static_cast<unsigned long>(capacity_.recommendedFlash()),
      static_cast<unsigned long>(OTA_CHUNK_SIZE), unsigned(CONFIG_IDF_FIRMWARE_CHIP_ID), token_,
      locked_ ? "true" : "false", writing_ ? "true" : "false", complete_ ? "true" : "false",
      static_cast<unsigned long>(received_), static_cast<unsigned long>(total_), error_,
      arduino_ && !locked_ ? "true" : "false", hostname_, arduino_ && !locked_ ? password_ : "");
  return n > 0 && size_t(n) < capacity ? size_t(n) : 0;
}
} // namespace forkrot

#else
#include <Arduino.h>
#include <cstdio>
namespace forkrot {
void Ota::reboot(){restartAt_=millis();restarting_=true;}
bool Ota::readyToRestart() const{return restarting_ && uint32_t(millis()-restartAt_)>=1500;}
size_t Ota::status(char *data,size_t capacity)const {
 const int n=std::snprintf(data,capacity,"{\"available\":false,\"reason\":\"feature_disabled\"}");
 return n>0 && size_t(n)<capacity?size_t(n):0;
}
}
#endif
