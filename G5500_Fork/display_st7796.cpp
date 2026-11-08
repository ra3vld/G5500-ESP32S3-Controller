// ST7796 over SPI with GT911 touch; fixed or runtime wiring. GPL-3.0.
#include "display.hpp"
#if DISPLAY_SUPPORT && DISPLAY_KIND == DISPLAY_ST7796
#include "board.hpp"
#include "network.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include <SPI.h>
namespace forkrot {
namespace {
// LVGL sends initialization commands before create() returns and user data exists.
// There is one physical SPI panel; capture its validated wiring before create().
int panelCs=-1,panelDc=-1;
void transfer(const uint8_t *command,size_t commands,const uint8_t *data,size_t bytes,bool pixels) {
  SPI.beginTransaction(SPISettings(40000000,MSBFIRST,SPI_MODE0));
  digitalWrite(panelCs,LOW);digitalWrite(panelDc,LOW);
  SPI.writeBytes(command,commands);digitalWrite(panelDc,HIGH);
  if(bytes) {
    if(pixels) SPI.writePixels(data,bytes); // ESP32 swaps little-endian RGB565 for the SPI wire.
    else SPI.writeBytes(data,bytes);
  }
  digitalWrite(panelCs,HIGH);SPI.endTransaction();
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
  const auto p=displayPins(runtime_.controller.hardware);
  panelCs=p.tftCs;panelDc=p.tftDc;
  if(p.tftBacklight>=0) {pinMode(p.tftBacklight,OUTPUT);digitalWrite(p.tftBacklight,LOW);}
  pinMode(panelCs,OUTPUT);digitalWrite(panelCs,HIGH);pinMode(panelDc,OUTPUT);
#if SELECTED_BOARD == BOARD_ESP32_3248S035C
  pinMode(5,OUTPUT);digitalWrite(5,HIGH); // Unselected onboard SD card.
  for(int pin:{4,16,17}) {pinMode(pin,OUTPUT);digitalWrite(pin,HIGH);} // Active-low RGB LED off.
#endif
  SPI.begin(p.tftSck,-1,p.tftMosi,p.tftCs);
  if(p.tftRst>=0) {
    pinMode(p.tftRst,OUTPUT);digitalWrite(p.tftRst,LOW);delay(10);digitalWrite(p.tftRst,HIGH);delay(100);
  }
  if(p.touchRst>=0) {
    pinMode(p.touchRst,OUTPUT);digitalWrite(p.touchRst,LOW);delay(10);digitalWrite(p.touchRst,HIGH);delay(100);
  }
  // Do not drive INT: stock revisions may connect it to ground. Probe both addresses.
  if(p.touchIrq>=0) pinMode(p.touchIrq,INPUT);
  touchBus_.setTimeOut(3);touchHealthy_=touchBus_.begin(p.touchSda,p.touchScl,100000) && touch_.begin();
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
  if(p.tftBacklight>=0) digitalWrite(p.tftBacklight,HIGH);
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
