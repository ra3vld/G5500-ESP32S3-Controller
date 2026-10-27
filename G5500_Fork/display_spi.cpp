// SPI pixel transport for the common LVGL interface. GPL-3.0.
#include "display.hpp"
#if DISPLAY_SUPPORT && (DISPLAY_KIND == DISPLAY_ILI9341 || DISPLAY_KIND == DISPLAY_GC9A01)
#include "board.hpp"
#include "hardware.hpp"
#include "network.hpp"
#include "diagnostics.hpp"
#include <Wire.h>
namespace forkrot {
namespace {
#if DISPLAY_SUPPORT && DISPLAY_KIND == DISPLAY_GC9A01
bool touchWrite(uint8_t reg,uint8_t value) {
  Wire.beginTransmission(0x15);Wire.write(reg);Wire.write(value);
  return Wire.endTransmission()==0;
}
#endif
}
void Display::initLocalDisplay() {
#if DISPLAY_SUPPORT && DISPLAY_KIND == DISPLAY_GC9A01
  pinMode(board::PIN_BACKLIGHT,OUTPUT);digitalWrite(board::PIN_BACKLIGHT,LOW);
  SPI.begin(board::PIN_PANEL_SCK,-1,board::PIN_PANEL_MOSI,board::PIN_PANEL_CS);
  panel_.emplace(&SPI,board::PIN_PANEL_DC,board::PIN_PANEL_CS,-1);
  panel_->begin(40000000);panel_->setRotation(runtime_.displaySettings.screen&3);panel_->invertDisplay(true);
  pinMode(board::PIN_CST_IRQ,INPUT_PULLUP);pinMode(board::PIN_CST_RST,OUTPUT);
  digitalWrite(board::PIN_CST_RST,LOW);delay(10);digitalWrite(board::PIN_CST_RST,HIGH);delay(100);
  touchHealthy_=Wire.begin(board::PIN_I2C_SDA,board::PIN_I2C_SCL,400000);
  Wire.setTimeOut(3);touchHealthy_=touchHealthy_ && touchWrite(0xfe,1);
#else
  const auto &pins=runtime_.controller.hardware.pins;
  panel_.emplace(&SPI,pins.tftDc,pins.tftCs,pins.tftRst);
  touch.emplace(pins.touchCs,pins.touchIrq);
  pinMode(pins.tftCs,OUTPUT);digitalWrite(pins.tftCs,HIGH);
  pinMode(pins.touchCs,OUTPUT);digitalWrite(pins.touchCs,HIGH);
  SPI.begin(pins.tftSck,pins.tftMiso,pins.tftMosi,pins.tftCs);
  panel_->begin();panel_->setRotation((1+(runtime_.displaySettings.screen&3))%4);touch->begin(SPI);touch->setRotation(1);
#endif
  uint8_t address=runtime_.displaySettings.madctl(DISPLAY_KIND==DISPLAY_GC9A01?0x48:0x28);
  panel_->sendCommand(0x36,&address,1);
  panel_->fillScreen(0);
  lv_init();lv_tick_set_cb([]()->uint32_t{return millis();});
  lv_delay_set_cb([](uint32_t ms){delay(ms);});
  display_=lv_display_create(screenWidth(),screenHeight());
  if(!display_) {displayFailed_=true;diagnostics_.dbgPrintln("[Display] LVGL allocation failed");return;}
  lv_display_set_color_format(display_,LV_COLOR_FORMAT_RGB565);
  lv_display_set_user_data(display_,this);
  lv_display_set_buffers(display_,pixels_.data(),nullptr,sizeof pixels_,LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display_,flushFrame);
  ui_.create(DISPLAY_KIND==DISPLAY_GC9A01);ui_.update(PanelView{});
  rendering_=true;lv_refr_now(display_);rendering_=false;
#if DISPLAY_SUPPORT && DISPLAY_KIND == DISPLAY_GC9A01
  digitalWrite(board::PIN_BACKLIGHT,HIGH);
#endif
  diagnostics_.dbgPrintln("[Display] LVGL RGB565 partial buffer ready");
}
void Display::flushFrame(lv_display_t *display,const lv_area_t *area,uint8_t *pixels) {
  auto &self=*static_cast<Display *>(lv_display_get_user_data(display));
  const unsigned w=area->x2-area->x1+1,h=area->y2-area->y1+1;
  self.panel_->startWrite();self.panel_->setAddrWindow(area->x1,area->y1,w,h);
  self.panel_->writePixels(reinterpret_cast<uint16_t *>(pixels),w*h);self.panel_->endWrite();
  if(lv_display_flush_is_last(display)) self.publishFrame();
  lv_display_flush_ready(display);
  // Input and control are serviced between stripes, even in a full LVGL refresh.
  // serviceTouch only queues UI changes while rendering_ is set.
  if(self.inputReady_) {
    self.serviceTouch();
    self.network_.serviceHud(false);
    self.runtime_.controller.tick(millis());
    self.hardware_.controlRotator();
  }
}
bool Display::serviceFrame() {return !displayFailed_;}
void Display::serviceKnob() {
  const uint32_t now=millis();
  if(runtime_.hud.isHud()) hudKnob_.sample(true,false,0,now,runtime_.loggerSettings.encoderInverted);
  else knob_.sample(true,false,0,now,runtime_.loggerSettings.encoderInverted);
}
TouchSample Display::readTouch() {
  TouchSample sample;sample.sampledAt=millis();
#if DISPLAY_SUPPORT && DISPLAY_KIND == DISPLAY_GC9A01
  Wire.beginTransmission(0x15);Wire.write(uint8_t(0x02));
  if(Wire.endTransmission(false)!=0 || Wire.requestFrom(uint8_t(0x15),size_t(5))!=5) return sample;
  uint8_t data[5];for(auto &b:data)b=Wire.read();
  sample=decodeCstTouch(data,PanelWidth,PanelHeight,sample.sampledAt);
#else
  int x=0,y=0;sample.healthy=true;sample.down=readTouchXY(x,y);sample.x=x;sample.y=y;
#endif
  return orientTouch(sample);
}
}
#endif
