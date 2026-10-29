// Synchronous, bounded-stripe QSPI transport for ESP32 and ESP32-S3. GPL-3.0.
#pragma once
#include <driver/spi_master.h>
#include "pin_map.hpp"
namespace forkrot {
class QspiPanelBus {
  spi_device_handle_t device_=nullptr;
  bool owned_=false;
  int cs_=-1;
  esp_err_t error_=ESP_OK;
public:
  bool begin(const PinMap &pins,size_t maxBytes);
  bool speed(int hz);
  bool command(uint8_t reg,const uint8_t *data=nullptr,size_t bytes=0);
  bool readId(uint8_t (&id)[4]);
  bool pixels(const uint8_t *data,size_t bytes);
  const char *error() const {return esp_err_to_name(error_);}
  void end();
};
}
