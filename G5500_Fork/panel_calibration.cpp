// LVGL pages for ILI9341 axis and resistive-touch calibration. GPL-3.0.
#include "display.hpp"
#if DISPLAY_SUPPORT && DISPLAY_KIND == DISPLAY_ILI9341
#include "calibration.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include "device_actions.hpp"
#include <algorithm>
namespace forkrot {
using namespace board;
namespace {
lv_color_t from565(uint16_t value) {
  return lv_color_make(((value>>11)&31)*255/31,((value>>5)&63)*255/63,(value&31)*255/31);
}
lv_obj_t *calBox(lv_obj_t *parent,int x,int y,int w,int h,uint16_t fill,uint16_t border) {
  const int width=lv_display_get_horizontal_resolution(lv_obj_get_display(parent));
  const int height=lv_display_get_vertical_resolution(lv_obj_get_display(parent));
  auto *box=lv_obj_create(parent);lv_obj_remove_style_all(box);
  lv_obj_set_pos(box,x*width/320,y*height/240);lv_obj_set_size(box,std::max(1,w*width/320),std::max(1,h*height/240));
  lv_obj_set_style_bg_opa(box,LV_OPA_COVER,0);lv_obj_set_style_bg_color(box,from565(fill),0);
  lv_obj_set_style_border_width(box,1,0);lv_obj_set_style_border_color(box,from565(border),0);
  lv_obj_set_scrollable(box,false);return box;
}
}
void Display::tftText(int16_t x,int16_t y,uint8_t size,uint16_t fg,uint16_t,const String &text) {
  auto *label=lv_label_create(calScreen_);lv_obj_set_pos(label,x*int(screenWidth())/320,y*int(screenHeight())/240);
  lv_obj_set_style_text_font(label,size>1&&screenWidth()>=320?&panel_font_14:&crowpanel_font_11,0);
  lv_obj_set_style_text_color(label,from565(fg),0);lv_label_set_text(label,text.c_str());
}
void Display::tftButton(int x,int y,int w,int h,const String &text,bool active,bool saved) {
  auto *box=calBox(calScreen_,x,y,w,h,saved?0x03e0:active?TFT_BLUE:TFT_PANEL,saved?TFT_OK:active?TFT_AZ:TFT_WARN);
  auto *label=lv_label_create(box);lv_obj_set_style_text_font(label,&panel_font_14,0);
  lv_obj_set_style_text_color(label,from565(TFT_TEXT),0);lv_label_set_text(label,text.c_str());lv_obj_center(label);
}
void Display::drawCalHeader(const String &title) {
  lv_obj_clean(calScreen_);
  calBox(calScreen_,3,3,314,29,TFT_BLUE,TFT_BLUE);
  tftText(10, 9, 2, TFT_TEXT, TFT_BLUE, title);
  tftButton(252, 5, 63, 25, "BACK");
}

void Display::drawCalHome() {
  auto &calibrationState = calibration_.state();
  drawCalHeader("CALIBRATION");

  tftText(8, 39, 2, TFT_MUTED, TFT_BG, "MODE");
  tftButton(8, 55, 145, 34, "2-POINT", calibrationState.calibrationMode == CAL_GS232_2POINT);
  tftButton(167, 55, 145, 34, "MULTI", calibrationState.calibrationMode == CAL_MULTI_POINT);

  tftText(8, 106, 2, TFT_MUTED, TFT_BG, "SELECT AXIS");
  tftButton(180,99,132,56,"TOUCH");

  tftButton(8, 166, 145, 52, "AZ CAL");
  if (runtime_.controller.model == forkrot::Model::G5500) {
    tftButton(167, 166, 145, 52, "EL CAL");
  }

  tftText(8, 223, 2, TFT_MUTED, TFT_BG,
          String("A") + String(hardware_.positionValue(0)) + (runtime_.controller.hardware.sensorType(0)?" ct":" mV") +
              (runtime_.controller.model == forkrot::Model::G5500
                   ? String(" E") + String(hardware_.positionValue(1)) + (runtime_.controller.hardware.sensorType(1)?" ct":" mV")
                   : String("")));
}

void Display::drawCalAxis(bool az) {
  const auto &state = calibration_.state();
  const auto range = az ? calibration_.activeAzRange() : calibration_.activeElRange();
  const String axis = az ? "AZ" : "EL";
  const char *unit=runtime_.controller.hardware.sensorType(az?0:1)?" ct":" mV";
  drawCalHeader(axis + " CAL");
  if (az) {
    tftButton(8, 36, 96, 26, "P36", state.gsAzMode == AZ_P36);
    tftButton(112, 36, 96, 26, "P45", state.gsAzMode == AZ_P45);
    tftButton(216, 36, 96, 26, "CUSTOM", state.gsAzMode == AZ_CUSTOM);
  } else {
    tftButton(8, 36, 145, 26, "0..180", state.elProfile == EL_STANDARD);
    tftButton(167, 36, 145, 26, "CUSTOM", state.elProfile == EL_CUSTOM);
  }

  if (state.calibrationMode == CAL_GS232_2POINT) {
    const bool startSet = az ? calibration_.activeAzOSet() : calibration_.activeElOSet();
    const bool endSet = az ? calibration_.activeAzFSet() : calibration_.activeElFSet();
    tftButton(8, 70, 145, 48, String(range.start, 1), false, startSet);
    tftButton(167, 70, 145, 48, String(range.end, 1), false, endSet);
    tftText(8, 125, 1, TFT_MUTED, TFT_BG, "START");
    tftText(167, 125, 1, TFT_MUTED, TFT_BG, "END");
    tftText(8, 140, 2, TFT_MUTED, TFT_BG,
            startSet ? String(az ? calibration_.activeAz2Zero() : calibration_.activeEl2Zero()) + unit : String("---")+unit);
    tftText(167, 140, 2, TFT_MUTED, TFT_BG,
            endSet ? String(az ? calibration_.activeAz2Full() : calibration_.activeEl2Full()) + unit : String("---")+unit);
    tftButton(8, 166, 304, 45, "RESET " + axis);
    tftText(8, 222, 1, TFT_MUTED, TFT_BG, "START -> END | Stop, then tap angle");
  } else {
    const bool custom = az ? state.gsAzMode == AZ_CUSTOM : state.elProfile == EL_CUSTOM;
    const auto &cal = az ? state.custom : state.customEl;
    const unsigned n = custom ? cal.count : az ? (state.gsAzMode == AZ_P36 ? AZ36_CAL_N : AZ45_CAL_N) : EL_CAL_N;
    auto &page = az ? azCalPage_ : elCalPage_;
    if (page * 6 >= n) page = 0;
    for (unsigned local = 0; local < 6 && page * 6 + local < n; ++local) {
      const unsigned i = page * 6 + local;
      const float deg = custom ? cal.range.start + cal.degrees[i] : az ? float(i * 90) : elCalDeg[i];
      const int mv = custom ? cal.millivolts[i] : az ? (state.gsAzMode == AZ_P36 ? state.az36CalMv[i] : state.az45CalMv[i]) : state.elCalMv[i];
      tftButton(8 + (local % 3) * 104, 68 + (local / 3) * 58, 96, 44, String(deg, 1), false, mv >= 0);
    }
    tftText(8, 175, 1, TFT_MUTED, TFT_BG, String(page + 1) + "/" + String((n + 5) / 6) + " | Stop, then tap angle");
    tftButton(8, 190, 80, 38, "<");
    tftButton(92, 190, 136, 38, "RESET " + axis);
    tftButton(232, 190, 80, 38, ">");
  }
}

void Display::drawTouchCalibration() {
  lv_obj_clean(calScreen_);
  calBox(calScreen_,3,3,314,29,TFT_BLUE,TFT_BLUE);
  tftText(10, 9, 2, TFT_TEXT, TFT_BLUE, "TOUCH CALIBRATION");

  if (touchCalStep >= 4) {
    tftText(42, 64, 2, touchCalibration_.touchCalValid ? TFT_OK : TFT_STOP, TFT_BG,
            touchCalibration_.touchCalValid ? "CALIBRATION SAVED" : "CALIBRATION FAILED");
    if (touchCalibration_.touchCalValid) {
      tftText(12, 90, 2, TFT_MUTED, TFT_BG,
              String("X ") + String(touchCalibration_.touchX0Raw) + "-" +
                  String(touchCalibration_.touchX1Raw));
      tftText(12, 108, 2, TFT_MUTED, TFT_BG,
              String("Y ") + String(touchCalibration_.touchY0Raw) + "-" +
                  String(touchCalibration_.touchY1Raw));
      tftText(12, 126, 2, TFT_MUTED, TFT_BG,
              String("SWAP ") + (touchCalibration_.touchSwapXY ? "YES" : "NO"));
    } else {
      tftText(12, 104, 2, TFT_WARN, TFT_BG, "PRESS FIRMLY");
    }
    tftButton(65, 155, 190, 48, touchCalibration_.touchCalValid ? "BACK" : "RETRY");
    return;
  }

  static const char *names[4] = {"TOP LEFT", "TOP RIGHT", "BOTTOM RIGHT", "BOTTOM LEFT"};
  static const int px[4] = {20, 299, 299, 20};
  static const int py[4] = {20, 20, 219, 219};

  tftText(16, 39, 2, TFT_MUTED, TFT_BG, "PRESS CROSS");
  tftText(16, 57, 2, TFT_TEXT, TFT_BG,
          String(touchCalStep + 1) + "/4  " + String(names[touchCalStep]));

  int x = px[touchCalStep], y = py[touchCalStep];
  calBox(calScreen_,x-15,y-1,31,2,TFT_WARN,TFT_WARN);
  calBox(calScreen_,x-1,y-15,2,31,TFT_WARN,TFT_WARN);
}

void Display::startTouchCalibration() {
  runtime_.controller.stopNow(-1,millis());hardware_.controlRotator();touchButtons_.cancel();
  touchCalStep = 0;
  touchWaitRelease = true;
  for (int i = 0; i < 4; i++) {
    touchCalRawX[i] = 0;
    touchCalRawY[i] = 0;
  }
  tftPage = TFT_PAGE_TOUCH_CAL;
  tftNeedsRedraw = true;
  drawTftPage();
}

bool Display::readTouchRawAverage(int &rx, int &ry) {
  if (!touch->touched()) {
    return false;
  }
  long sx = 0, sy = 0;
  int n = 0;
  for (int i = 0; i < 12; i++) {
    if (touch->touched()) {
      TS_Point p = touch->getPoint();
      if (p.z == 0 || p.z >= 150) {
        sx += p.x;
        sy += p.y;
        n++;
      }
    }
    delay(3);
  }
  if (n < 5) {
    return false;
  }
  rx = (int)(sx / n);
  ry = (int)(sy / n);
  return true;
}

void Display::finishTouchCalibration() {
  touchCalibration_.touchCalValid=fitTouchCalibration(touchCalibration_,touchCalRawX,touchCalRawY,runtime_.displaySettings);
  saveTouchCalibration();
}

void Display::serviceTouchCalibration() {
  if (tftPage != TFT_PAGE_TOUCH_CAL) {
    return;
  }

  if (touchCalStep >= 4) {
    if (!touch->touched()) {
      touchWaitRelease = false;
      return;
    }
    if (touchWaitRelease) {
      return;
    }

    int rx, ry;
    if (!readTouchRawAverage(rx, ry)) {
      return;
    }

    if (!touchCalibration_.touchCalValid) {
      // On failed calibration, accept the lower half of the panel in RAW form
      // as RETRY so recovery never depends on a bad coordinate map.
      startTouchCalibration();
      return;
    }

    int x, y;
    if (readCalibrationXY(x, y) && hit(x, y, 45, 145, 230, 68)) {
      tftPage = TFT_PAGE_DASH;
      tftNeedsRedraw = true;
      drawTftPage();
      touchWaitRelease = true;
    }
    return;
  }

  if (!touch->touched()) {
    touchWaitRelease = false;
    return;
  }
  if (touchWaitRelease) {
    return;
  }

  int rx, ry;
  if (!readTouchRawAverage(rx, ry)) {
    return;
  }

  touchCalRawX[touchCalStep] = rx;
  touchCalRawY[touchCalStep] = ry;
  touchCalStep++;
  touchWaitRelease = true;

  if (touchCalStep >= 4) {
    finishTouchCalibration();
  }
  tftNeedsRedraw = true;
  drawTftPage();
}

bool Display::readTouchXY(int &x, int &y) {
  if (!touch->touched()) {
    return false;
  }
  TS_Point p = touch->getPoint();

  // Ignore extremely weak/noisy contacts.
  if (p.z > 0 && p.z < 150) {
    return false;
  }

  int rawX = touchCalibration_.touchSwapXY ? p.y : p.x;
  int rawY = touchCalibration_.touchSwapXY ? p.x : p.y;

  long sx, sy;
  if (touchCalibration_.touchCalValid) {
    sx = 20L + (long)(rawX - touchCalibration_.touchX0Raw) * (299 - 20) /
                   (long)(touchCalibration_.touchX1Raw - touchCalibration_.touchX0Raw);
    sy = 20L + (long)(rawY - touchCalibration_.touchY0Raw) * (219 - 20) /
                   (long)(touchCalibration_.touchY1Raw - touchCalibration_.touchY0Raw);
  } else {
    sx = map((long)p.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, 319);
    sy = map((long)p.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 239, 0);
  }

  x = constrain((int)sx, 0, 319);
  y = constrain((int)sy, 0, 239);
  return true;
}

bool Display::readCalibrationXY(int &x,int &y) {
  if(!readTouchXY(x,y))return false;
  const auto p=runtime_.displaySettings.correctedTouch(x,y,320,240);
  x=p.x*319/int(screenWidth()-1);y=p.y*239/int(screenHeight()-1);return true;
}

void Display::resetTouchCalAxis(bool az) {
  auto &calibrationState = calibration_.state();
  if (az) {
    calibration_.resetAz(calibrationState.calibrationMode == CAL_MULTI_POINT);
    actions_.saveCal(); tftNeedsRedraw=true; return;
  }
  calibration_.resetEl(calibrationState.calibrationMode == CAL_MULTI_POINT);
  actions_.saveCal();
  tftNeedsRedraw = true;
}

void Display::captureTouchMultiAz(uint8_t idx) {
  auto &calibrationState = calibration_.state();
  int mv = hardware_.positionValue(0);
  if(mv<0) return;
  if (calibrationState.gsAzMode == AZ_CUSTOM) {
    if (idx < calibrationState.custom.count) actions_.editAzPoint("capture", calibrationState.custom.range.start+calibrationState.custom.degrees[idx]);
    tftNeedsRedraw=true; return;
  }
  if (calibrationState.gsAzMode == 36) {
    if (idx < AZ36_CAL_N) {
      calibrationState.az36CalMv[idx] = mv;
    }
  } else {
    if (idx < AZ45_CAL_N) {
      calibrationState.az45CalMv[idx] = mv;
    }
  }
  calibration_.refreshMultiCalValidity();
  actions_.saveCal();
  tftNeedsRedraw = true;
}

void Display::captureTouchMultiEl(uint8_t idx) {
  if (runtime_.controller.model != forkrot::Model::G5500) {
    return;
  }
  const auto &cal=calibration_.state().customEl;
  if(calibration_.state().elProfile==EL_CUSTOM) {
    if(idx<cal.count) actions_.editElPoint("capture",cal.range.start+cal.degrees[idx]);
    tftNeedsRedraw=true;return;
  }
  if (idx >= EL_CAL_N) {
    return;
  }
  const int value=hardware_.positionValue(1);
  if(value<0) return;
  calibration_.state().elCalMv[idx] = value;
  calibration_.refreshMultiCalValidity();
  actions_.saveCal();
  tftNeedsRedraw = true;
}

void Display::serviceTouchRecoveryHold() {
  if (tftPage != TFT_PAGE_DASH || speedScreen_ || runtime_.controller.moving() || touchButtons_.manualActive()) {
    touchHoldStartMs = 0;
    touchHoldArmed = false;
    return;
  }

  if (touch->touched()) {
    if (!touchHoldArmed) {
      touchHoldArmed = true;
      touchHoldStartMs = millis();
    } else if (millis() - touchHoldStartMs >= 2500) {
      touchHoldArmed = false;
      touchHoldStartMs = 0;
      startTouchCalibration();
    }
  } else {
    touchHoldArmed = false;
    touchHoldStartMs = 0;
  }
}

void Display::drawTftPage() {
  if(tftPage==TFT_PAGE_DASH) {
    lv_screen_load(ui_.root());
    touchButtons_.cancel();redraw_=true;return;
  }
  if(!calScreen_) {
    calScreen_=lv_obj_create(nullptr);lv_obj_set_style_bg_color(calScreen_,lv_color_hex(0x0a111c),0);
    lv_obj_set_scrollable(calScreen_,false);
  }
  lv_screen_load(calScreen_);
  if(tftPage==TFT_PAGE_CAL_HOME) drawCalHome();
  else if(tftPage==TFT_PAGE_CAL_AZ || tftPage==TFT_PAGE_CAL_EL) drawCalAxis(tftPage==TFT_PAGE_CAL_AZ);
  else drawTouchCalibration();
  tftNeedsRedraw=false;
}
void Display::saveTouchCalibration() {storage_.saveTouchCalibration(touchCalibration_);}
void Display::serviceCalibrationTouch() {
  auto &calibrationState=calibration_.state();
  if(tftPage==TFT_PAGE_TOUCH_CAL) {serviceTouchCalibration();return;}
  if(!touch->touched()) {touchWaitRelease=false;return;}
  if(touchWaitRelease) return;
  int x,y;if(!readCalibrationXY(x,y)) return;
  touchWaitRelease=true;
  if(hit(x,y,235,0,85,40)) {
    tftPage=tftPage==TFT_PAGE_CAL_HOME?TFT_PAGE_DASH:TFT_PAGE_CAL_HOME;
    drawTftPage();return;
  }
  if(tftPage==TFT_PAGE_CAL_HOME && hit(x,y,180,99,132,56)) {startTouchCalibration();return;}
  if (tftPage == TFT_PAGE_CAL_HOME) {
    if (hit(x, y, 8, 55, 145, 34)) {
      actions_.setCalibrationMode(CAL_GS232_2POINT);
      tftNeedsRedraw = true;
    } else if (hit(x, y, 167, 55, 145, 34)) {
      actions_.setCalibrationMode(CAL_MULTI_POINT);
      tftNeedsRedraw = true;
    } else if (hit(x, y, 8, 166, 145, 52)) {
      tftPage = TFT_PAGE_CAL_AZ;
      tftNeedsRedraw = true;
    } else if (runtime_.controller.model == forkrot::Model::G5500 && hit(x, y, 167, 166, 145, 52)) {
      tftPage = TFT_PAGE_CAL_EL;
      tftNeedsRedraw = true;
    }
    if (tftNeedsRedraw) {
      drawTftPage();
    }
    return;
  }

  if (tftPage == TFT_PAGE_CAL_AZ || tftPage == TFT_PAGE_CAL_EL) {
    const bool az = tftPage == TFT_PAGE_CAL_AZ;
    auto &page = az ? azCalPage_ : elCalPage_;
    bool profileSelected = false;
    if (az) {
      for (unsigned i = 0; i < 3; ++i) {
        if (hit(x, y, 8 + i * 104, 36, 96, 26)) {
          actions_.setGsAzMode(i == 0 ? AZ_P36 : i == 1 ? AZ_P45 : AZ_CUSTOM);
          profileSelected = true;
        }
      }
    } else if (hit(x, y, 8, 36, 145, 26) || hit(x, y, 167, 36, 145, 26)) {
      const bool custom = x >= 167;
      const auto r = calibrationState.customEl.initialized ? calibrationState.customEl.range : calibration_.activeElRange();
      actions_.configureElRange(custom ? EL_CUSTOM : EL_STANDARD, custom ? r.start : 0, custom ? r.span() : 180);
      profileSelected = true;
    }
    if (profileSelected) {
      page = 0;
    } else if (calibrationState.calibrationMode == CAL_GS232_2POINT) {
      if (hit(x, y, 8, 70, 145, 48)) {
        actions_.captureGs232Cal(az ? "O" : "O2");
      } else if (hit(x, y, 167, 70, 145, 48)) {
        actions_.captureGs232Cal(az ? "F" : "F2");
      } else if (hit(x, y, 8, 166, 304, 45)) {
        resetTouchCalAxis(az);
      }
    } else {
      const unsigned n = az ? (calibrationState.gsAzMode == AZ_CUSTOM ? calibrationState.custom.count : calibrationState.gsAzMode == AZ_P36 ? AZ36_CAL_N : AZ45_CAL_N)
                            : calibrationState.elProfile == EL_CUSTOM ? calibrationState.customEl.count : EL_CAL_N;
      for (unsigned local = 0; local < 6 && page * 6 + local < n; ++local) {
        if (hit(x, y, 8 + (local % 3) * 104, 68 + (local / 3) * 58, 96, 44)) {
          if (az) captureTouchMultiAz(uint8_t(page * 6 + local));
          else captureTouchMultiEl(uint8_t(page * 6 + local));
        }
      }
      const unsigned pages = (n + 5) / 6;
      if (hit(x, y, 8, 190, 80, 38)) page = uint8_t((page + pages - 1) % pages);
      else if (hit(x, y, 232, 190, 80, 38)) page = uint8_t((page + 1) % pages);
      else if (hit(x, y, 92, 190, 136, 38)) resetTouchCalAxis(az);
    }
    tftNeedsRedraw = true;
    drawTftPage();
  }
}
}
#endif
