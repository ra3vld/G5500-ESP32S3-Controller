// Effective panel wiring: built-in boards retain their fixed connections. GPL-3.0.
#pragma once
#include "hardware_config.hpp"
namespace forkrot {
inline PinMap displayPins(const HardwareConfig &c) {
  auto p=c.pins;
#if SELECTED_BOARD == BOARD_ESP32_2424S012C
  p.tftMosi=board::PIN_PANEL_MOSI;p.tftSck=board::PIN_PANEL_SCK;
  p.tftCs=board::PIN_PANEL_CS;p.tftDc=board::PIN_PANEL_DC;p.tftRst=-1;
  p.tftBacklight=board::PIN_BACKLIGHT;
  p.touchSda=board::PIN_I2C_SDA;p.touchScl=board::PIN_I2C_SCL;
  p.touchRst=board::PIN_CST_RST;p.touchIrq=board::PIN_CST_IRQ;
#elif SELECTED_BOARD == BOARD_ESP32_3248S035C
  p.tftMosi=board::PIN_PANEL_MOSI;p.tftSck=board::PIN_PANEL_SCK;
  p.tftCs=board::PIN_PANEL_CS;p.tftDc=board::PIN_PANEL_DC;p.tftRst=-1;
  p.tftBacklight=board::PIN_BACKLIGHT;
  p.touchSda=board::PIN_I2C_SDA;p.touchScl=board::PIN_I2C_SCL;
  p.touchRst=board::PIN_GT_RST;p.touchIrq=board::PIN_GT_IRQ;
#endif
  return p;
}
}
