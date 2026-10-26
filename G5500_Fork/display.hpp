// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include <optional>
#if GRAPHICAL_DISPLAY
#include "runtime.hpp"
#include "crowpanel_knob.hpp"
#include "hud_input.hpp"
#include "panel_ui.hpp"
#include "panel_command.hpp"
#include "touch_sample.hpp"
#include <array>
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include "crowpanel_board.hpp"
#if CROWPANEL_USB_TRACE
#include "crowpanel_trace.hpp"
#endif
#elif DISPLAY_KIND == DISPLAY_GC9A01
#include <Adafruit_GC9A01A.h>
#elif DISPLAY_KIND == DISPLAY_ST7796
#include <Wire.h>
#include "gt911_touch.hpp"
#else
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include "display_state.hpp"
#endif
namespace forkrot {
class DeviceActions; class Calibration; class Hardware; class Diagnostics; class Network; class Storage;
class Display {
  Runtime &runtime_;
  DeviceActions &actions_;
  Calibration &calibration_;
  Hardware &hardware_;
  Diagnostics &diagnostics_;
  Network &network_;
  CrowPanelKnob knob_;
  HudKnob hudKnob_;
  Storage &storage_;
  bool hudTouchDown_=false,speedScreen_=false,speedKnobDown_=false;
  int speedRequested_[2]{-1,-1};
  panel::TouchGesture touchButtons_;
  uint32_t touchSerial_=0,touchSelection_=0,touchNoticeAt_=0;
  uint32_t inputRangeRevision_=0;
  Model inputModel_=Model::G5500;
  bool touchNotice_=false;
  PanelUi ui_;
  panel::TouchLayout presentedLayout_{};
  PanelCommandContext requestedCommand_{}, presentedCommand_{};
  PanelCommandContext commandContext() const;
  bool framePresented_=false;
  void publishFrame() {
    presentedLayout_=ui_.touchLayout();presentedCommand_=requestedCommand_;framePresented_=true;
  }
  lv_display_t *display_=nullptr;
  uint32_t lastInput_=0,lastTouch_=0,lastUi_=0;
  bool rendering_=false,settingsRequested_=false;
  bool redraw_=true,inputHealthy_=true,touchHealthy_=true,inputReady_=false,displayFailed_=false;
  bool flushPending_=false;
  uint32_t flushGeneration_=0,flushStarted_=0;
  bool serviceFrame();
  static constexpr unsigned PanelWidth=DISPLAY_KIND==DISPLAY_ST7796?480:DISPLAY_KIND==DISPLAY_CROWPANEL_21?480:DISPLAY_KIND==DISPLAY_GC9A01?240:320;
  static constexpr unsigned PanelHeight=DISPLAY_KIND==DISPLAY_CROWPANEL_21?480:DISPLAY_KIND==DISPLAY_ST7796?320:240;
  unsigned screenWidth() const {return runtime_.displaySettings.width(PanelWidth,PanelHeight);}
  unsigned screenHeight() const {return runtime_.displaySettings.height(PanelWidth,PanelHeight);}
  TouchSample orientTouch(TouchSample sample) const {
    if(!sample.healthy)return sample;
    if(sample.x>=PanelWidth||sample.y>=PanelHeight){sample.healthy=sample.down=false;return sample;}
    const auto p=runtime_.displaySettings.correctedTouch(sample.x,sample.y,PanelWidth,PanelHeight);
    sample.x=uint16_t(p.x);sample.y=uint16_t(p.y);return sample;
  }
  void updatePanelView(uint32_t now);
  void serviceKnob();
  TouchSample readTouch();
  static void flushFrame(lv_display_t *display,const lv_area_t *area,uint8_t *pixels);
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
  esp_lcd_panel_handle_t panel_=nullptr;
#if CROWPANEL_USB_TRACE
  CrowPanelTrace<> trace_;
  void serviceTrace(uint32_t now);
#endif
#else
  static constexpr unsigned StripeRows=16;
  alignas(4) std::array<uint16_t,PanelWidth*StripeRows> pixels_{};
#if DISPLAY_KIND == DISPLAY_GC9A01
  std::optional<Adafruit_GC9A01A> panel_;
#elif DISPLAY_KIND == DISPLAY_ST7796
  TwoWire touchBus_{1};
  Gt911Touch<TwoWire> touch_{touchBus_};
  static void sendPanelCommand(lv_display_t *,const uint8_t *,size_t,const uint8_t *,size_t);
  static void sendPanelPixels(lv_display_t *,const uint8_t *,size_t,uint8_t *,size_t);
#else
  std::optional<Adafruit_ILI9341> panel_;
  std::optional<XPT2046_Touchscreen> touch;
  TouchCalibration touchCalibration_;
  lv_obj_t *calScreen_=nullptr;
  enum TftPage : uint8_t { TFT_PAGE_DASH,TFT_PAGE_CAL_HOME,TFT_PAGE_CAL_AZ,TFT_PAGE_CAL_EL,TFT_PAGE_TOUCH_CAL };
  TftPage tftPage=TFT_PAGE_DASH;
  bool tftNeedsRedraw=true;
  uint8_t azCalPage_=0,elCalPage_=0,touchCalStep=0;
  int touchCalRawX[4]{},touchCalRawY[4]{};
  bool touchWaitRelease=false,touchHoldArmed=false;
  uint32_t touchHoldStartMs=0,calTouchAt_=0;
  void tftText(int16_t x,int16_t y,uint8_t size,uint16_t fg,uint16_t bg,const String &text);
  void tftButton(int x,int y,int w,int h,const String &text,bool active=false,bool saved=false);
  void drawCalHeader(const String &title);
  void drawCalHome();void drawCalAxis(bool az);void drawTouchCalibration();void drawTftPage();
  void startTouchCalibration();bool readTouchRawAverage(int &x,int &y);void finishTouchCalibration();
  void serviceTouchCalibration();void serviceCalibrationTouch();void serviceTouchRecoveryHold();
  bool readTouchXY(int &x,int &y);
  bool readCalibrationXY(int &x,int &y);
  static bool hit(int x,int y,int bx,int by,int bw,int bh) {return x>=bx && x<bx+bw && y>=by && y<by+bh;}
  void resetTouchCalAxis(bool az);void captureTouchMultiAz(uint8_t idx);void captureTouchMultiEl(uint8_t idx);
  void saveTouchCalibration();
  static constexpr uint16_t TFT_BG=ILI9341_BLACK,TFT_PANEL=0x18e3,TFT_BORDER=0x4208,TFT_TEXT=ILI9341_WHITE,
    TFT_MUTED=0x9cf3,TFT_AZ=ILI9341_CYAN,TFT_EL=ILI9341_YELLOW,TFT_OK=ILI9341_GREEN,
    TFT_WARN=ILI9341_ORANGE,TFT_STOP=ILI9341_RED,TFT_BLUE=ILI9341_BLUE,TFT_TARGET=ILI9341_MAGENTA;
#endif
#endif
public:
  Display(Runtime &runtime,DeviceActions &actions,Calibration &calibration,Hardware &hardware,
          Diagnostics &diagnostics,Network &network,Storage &storage)
      :runtime_(runtime),actions_(actions),calibration_(calibration),hardware_(hardware),diagnostics_(diagnostics),
       network_(network),knob_(runtime.controller),hudKnob_(runtime.hud,runtime.controller),storage_(storage) {}
  void invalidate() {redraw_=true;}
  void loadTouchCalibration();
  void startAfterCalibrationLoad();
  void initLocalDisplay();void serviceTouch();void updateLocalDisplay();
};
}
#elif DISPLAY_KIND == DISPLAY_LCD1602
#include <Arduino.h>
#include <LiquidCrystal.h>
#include "board.hpp"
#include "runtime.hpp"
namespace forkrot {
class DeviceActions; class Calibration; class Hardware; class Diagnostics; class Network; class Storage;
class Display {
  Runtime &runtime_;
  std::optional<LiquidCrystal> lcd_;
  uint32_t updatedAt_ = 0;
  uint8_t lcdPrevious_[2][16]{};
  bool redraw_ = true;
public:
  Display(Runtime &runtime, DeviceActions &, Calibration &, Hardware &, Diagnostics &, Network &, Storage &) : runtime_(runtime) {}
  void invalidate() { redraw_ = true; }
  void loadTouchCalibration() {}
  void startAfterCalibrationLoad() {}
  void serviceTouch() {}
  void initLocalDisplay();
  void updateLocalDisplay();
};
}
#else

namespace forkrot {
struct Runtime;
class DeviceActions;
class Calibration;
class Hardware;
class Diagnostics;
class Network;
class Storage;

// The same application interface without drivers, peripheral state or GPIO calls.
class Display {
public:
  Display(Runtime &, DeviceActions &, Calibration &, Hardware &, Diagnostics &, Network &,
          Storage &) {}
  void invalidate() {}
  void loadTouchCalibration() {}
  void startAfterCalibrationLoad() {}
  void serviceTouch() {}
  void initLocalDisplay() {}
  void updateLocalDisplay() {}
};
} // namespace forkrot

#endif
