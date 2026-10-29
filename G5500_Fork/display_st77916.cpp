// External Waveshare 1.85-inch ST77916 / CST816S, ESP32 and ESP32-S3. GPL-3.0.
#include "display.hpp"
#if GRAPHICAL_DISPLAY && DISPLAY_KIND == DISPLAY_ST77916
#include "st77916_panel.hpp"
#include "diagnostics.hpp"
#include "hardware.hpp"
#include "network.hpp"
namespace forkrot {
namespace {
void resetPin(int pin) {
  if(pin<0)return;
  digitalWrite(pin,LOW);pinMode(pin,OUTPUT);delay(10);digitalWrite(pin,HIGH);delay(120);
}
void backlight(int pin,bool on) {
  if(pin<0)return;
  digitalWrite(pin,on?HIGH:LOW);pinMode(pin,OUTPUT);
}
}
void Display::initLocalDisplay() {
  const auto &p=runtime_.controller.hardware.pins;
  backlight(p.tftBacklight,false);
  auto fail=[&]() {
    displayFailed_=true;
    diagnostics_.dbgPrintln(String("[Display] ST77916 initialization failed: ")+panelBus_.error());
    panelBus_.end();
  };
  if(!panelBus_.begin(p,sizeof pixels_)){fail();return;}
  uint8_t id[4]{};
  const bool identified=panelBus_.readId(id);
  const unsigned version=ST77916_INIT_VARIANT?ST77916_INIT_VARIANT:st77916::variant(id);
  if(!identified)diagnostics_.dbgPrintln("[Display] ST77916 ID unavailable; using configured/default init");
  if(!panelBus_.speed(ST77916_SPI_HZ)){fail();return;}
  if(p.tftRst>=0)resetPin(p.tftRst);
  else {
    if(!panelBus_.command(0x01)){fail();return;}
    delay(120);
  }
  if(!st77916::initialize(panelBus_,version,runtime_.displaySettings.madctl(0),
                         [](uint32_t ms){delay(ms);})){fail();return;}
  resetPin(p.touchRst);
  if(p.touchIrq>=0)pinMode(p.touchIrq,INPUT); // Poll coordinates; IRQ is optional.
  touchBus_.setTimeOut(3);
  touchBusReady_=touchBus_.begin(p.touchSda,p.touchScl,400000);
  if(touchBusReady_) {
    touchBus_.beginTransmission(0x15);touchBus_.write(uint8_t(0xfe));touchBus_.write(uint8_t(1));
    touchHealthy_=touchBus_.endTransmission()==0; // Disable automatic sleep.
  } else touchHealthy_=false;
  lv_init();lv_tick_set_cb([]()->uint32_t{return millis();});
  lv_delay_set_cb([](uint32_t ms){delay(ms);});
  display_=lv_display_create(PanelWidth,PanelHeight);
  if(!display_){fail();return;}
  lv_display_set_color_format(display_,LV_COLOR_FORMAT_RGB565);
  lv_display_set_user_data(display_,this);
  lv_display_set_buffers(display_,pixels_.data(),nullptr,sizeof pixels_,LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display_,flushFrame);
  ui_.create(true);ui_.update(PanelView{});
  rendering_=true;lv_refr_now(display_);rendering_=false;
  backlight(p.tftBacklight,!displayFailed_);
  diagnostics_.dbgPrintln(String("[Display] ST77916 360x360, init=")+version+", QSPI="+ST77916_SPI_HZ+
      ", CST816S="+(touchHealthy_?"ready":"unavailable"));
}
void Display::flushFrame(lv_display_t *display,const lv_area_t *area,uint8_t *pixels) {
  auto &self=*static_cast<Display *>(lv_display_get_user_data(display));
  const unsigned w=area->x2-area->x1+1,h=area->y2-area->y1+1;
  bool ok=!self.displayFailed_ && w*h<=self.pixels_.size() &&
      st77916::window(self.panelBus_,area->x1,area->y1,w,h);
  if(ok) {
    lv_draw_sw_rgb565_swap(pixels,w*h);
    ok=self.panelBus_.pixels(pixels,w*h*2);
    lv_draw_sw_rgb565_swap(pixels,w*h);
  }
  if(!ok && !self.displayFailed_) {
    self.displayFailed_=true;
    backlight(self.runtime_.controller.hardware.pins.tftBacklight,false);
    self.diagnostics_.dbgPrintln(String("[Display] ST77916 transfer failed: ")+self.panelBus_.error());
  }
  if(ok && lv_display_flush_is_last(display))self.publishFrame();
  lv_display_flush_ready(display);
  // Keep touch release, HUD leases and motor control responsive between stripes.
  if(self.inputReady_) {
    self.serviceTouch();self.network_.serviceHud(false);
    self.runtime_.controller.tick(millis());self.hardware_.controlRotator();
  }
}
bool Display::serviceFrame() {return !displayFailed_;}
void Display::serviceKnob() {
  const uint32_t now=millis();
  if(runtime_.hud.isHud())hudKnob_.sample(true,false,0,now,runtime_.loggerSettings.encoderInverted);
  else knob_.sample(true,false,0,now,runtime_.loggerSettings.encoderInverted);
}
TouchSample Display::readTouch() {
  TouchSample sample;sample.sampledAt=millis();
  if(!touchBusReady_ || displayFailed_)return sample;
  touchBus_.beginTransmission(0x15);touchBus_.write(uint8_t(0x02));
  if(touchBus_.endTransmission(false)!=0 || touchBus_.requestFrom(uint8_t(0x15),size_t(5))!=5)return sample;
  uint8_t data[5];for(auto &b:data)b=touchBus_.read();
  return orientTouch(decodeCstTouch(data,PanelWidth,PanelHeight,sample.sampledAt));
}
}
#endif
