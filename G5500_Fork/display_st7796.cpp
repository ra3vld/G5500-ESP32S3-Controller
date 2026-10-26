// ESP32-3248S035C: LVGL's ST7796 driver over SPI, GT911 on I2C1. GPL-3.0.
#include "display.hpp"
#if DISPLAY_KIND == DISPLAY_ST7796
#include "board.hpp"
#include "network.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include <SPI.h>
namespace forkrot {
namespace {
void transfer(const uint8_t *command,size_t commands,const uint8_t *data,size_t bytes,bool pixels) {
  SPI.beginTransaction(SPISettings(40000000,MSBFIRST,SPI_MODE0));
  digitalWrite(board::PIN_PANEL_CS,LOW);digitalWrite(board::PIN_PANEL_DC,LOW);
  SPI.writeBytes(command,commands);digitalWrite(board::PIN_PANEL_DC,HIGH);
  if(bytes) {
    if(pixels) SPI.writePixels(data,bytes); // ESP32 swaps little-endian RGB565 for the SPI wire.
    else SPI.writeBytes(data,bytes);
  }
  digitalWrite(board::PIN_PANEL_CS,HIGH);SPI.endTransaction();
}
}
void Display::sendPanelCommand(lv_display_t *,const uint8_t *cmd,size_t commands,const uint8_t *data,size_t bytes) {
  transfer(cmd,commands,data,bytes,false);
}
void Display::sendPanelPixels(lv_display_t *display,const uint8_t *cmd,size_t commands,uint8_t *data,size_t bytes) {
  transfer(cmd,commands,data,bytes,true);
  auto &self=*static_cast<Display *>(lv_display_get_user_data(display));
  if(lv_display_flush_is_last(display)) self.publishFrame();
  lv_display_flush_ready(display);
  if(self.inputReady_) {
    self.serviceTouch();self.network_.serviceHud(false);self.runtime_.controller.tick(millis());self.hardware_.controlRotator();
  }
}
void Display::initLocalDisplay() {
  pinMode(board::PIN_BACKLIGHT,OUTPUT);digitalWrite(board::PIN_BACKLIGHT,LOW);
  pinMode(board::PIN_PANEL_CS,OUTPUT);digitalWrite(board::PIN_PANEL_CS,HIGH);
  pinMode(board::PIN_PANEL_DC,OUTPUT);
  pinMode(5,OUTPUT);digitalWrite(5,HIGH); // Unselected onboard SD card.
  for(int pin:{4,16,17}) {pinMode(pin,OUTPUT);digitalWrite(pin,HIGH);} // Active-low RGB LED off.
  SPI.begin(board::PIN_PANEL_SCK,board::PIN_PANEL_MISO,board::PIN_PANEL_MOSI,board::PIN_PANEL_CS);
  pinMode(board::PIN_GT_RST,OUTPUT);digitalWrite(board::PIN_GT_RST,LOW);delay(10);
  digitalWrite(board::PIN_GT_RST,HIGH);delay(100);
  // Do not drive INT: stock revisions may connect it to ground.
  touchBus_.setTimeOut(3);touchHealthy_=touchBus_.begin(board::PIN_I2C_SDA,board::PIN_I2C_SCL,100000) && touch_.begin();
  lv_init();lv_tick_set_cb([]()->uint32_t{return millis();});lv_delay_set_cb([](uint32_t ms){delay(ms);});
  display_=lv_st7796_create(screenWidth(),screenHeight(),LV_LCD_FLAG_NONE,sendPanelCommand,sendPanelPixels);
  if(!display_) {displayFailed_=true;diagnostics_.dbgPrintln("[Display] ST7796 allocation failed");return;}
  lv_display_set_user_data(display_,this);lv_display_set_color_format(display_,LV_COLOR_FORMAT_RGB565);
  const auto address=runtime_.displaySettings.madctl(0xa0);
  lv_lcd_generic_mipi_set_address_mode(display_,address&0x40,address&0x80,address&0x20,false);
  lv_st7796_set_invert(display_,false);
  lv_display_set_buffers(display_,pixels_.data(),nullptr,sizeof pixels_,LV_DISPLAY_RENDER_MODE_PARTIAL);
  ui_.create(false);ui_.update(PanelView{});
  rendering_=true;lv_refr_now(display_);rendering_=false;
  digitalWrite(board::PIN_BACKLIGHT,HIGH);
  diagnostics_.dbgPrintln(touchHealthy_?"[Display] ST7796 480x320 / GT911 ready":"[Display] ST7796 ready; GT911 unavailable");
}
bool Display::serviceFrame() {return !displayFailed_;}
void Display::serviceKnob() {
  const uint32_t now=millis();
  if(runtime_.hud.isHud()) hudKnob_.sample(true,false,0,now,runtime_.loggerSettings.encoderInverted);
  else knob_.sample(true,false,0,now,runtime_.loggerSettings.encoderInverted);
}
TouchSample Display::readTouch() {return orientTouch(touch_.poll(millis()));}
}
#endif
