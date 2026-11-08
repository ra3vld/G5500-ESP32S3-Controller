#include "board.hpp"
#include "hud.hpp"
#include <cassert>
#include <cstdio>
using namespace forkrot;
int main() {
  static_assert(SELECTED_BOARD==10 && DISPLAY_KIND==5 && FORK_CLASSIC_ESP32);
  static_assert(!BLUETOOTH_SPP_SUPPORT && !WEB_LIVE_SUPPORT && !LOG_HISTORY_SUPPORT && !RSSI_HISTORY_SUPPORT);
  static_assert(!OTA_SUPPORT && WIFI_COMPACT_BUFFERS && !ESP32_SPP_PROFILE);
  static_assert(BLUETOOTH_SUPPORT && MQTT_SUPPORT && HUD_SUPPORT && COMMAND_API_SUPPORT);
  static_assert(board::PIN_PANEL_SCK==14 && board::PIN_PANEL_MOSI==13 && board::PIN_BACKLIGHT==27);
  static_assert(board::PIN_I2C_SDA==33 && board::PIN_I2C_SCL==32 && board::PIN_GT_RST==25);
  HardwareConfig c;assert(!board::validateHardware(c));
  assert(!c.localAxisConfigured(0) && !c.localAxisConfigured(1));
  assert(HudConfig{}.role==DeviceRole::Hud);
  for(int pin:{1,3,2,12,13,14,15,27,32,33,25,21,4,26,34,5,18,19,23}) {
    assert(board::fixedPinOwner(pin));auto bad=c;bad.pins.led=pin;assert(board::validateHardware(bad));
  }
  c.pins.led=22;assert(!board::validateHardware(c));
  c.pins.led=35;assert(board::validateHardware(c));
  Controller controller;assert(controller.model==Model::G5500);
  assert(controller.jog(0,1,false,100)==Result::Unavailable);
  c=HardwareConfig{};c.bluetooth=true;assert(c.backend()==BT_HC05 && board::validateHardware(c));
  c.pins.hc05Rx=35;c.pins.hc05Tx=22;c.pins.hc05State=39;
  assert(!board::validateHardware(c));
  c.bluetoothMode=BT_ESP32_SPP;assert(c.backend()==BT_NONE);
  std::puts("PASS: ST7796 board pins, built-in reservations, optional local drive, HUD defaults and full ESP32 resource budget with external HC-05");
}
