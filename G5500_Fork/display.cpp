// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "build_config.hpp"

#if TFT_DISPLAY_ENABLED
#include "display.hpp"
#include "cat_image.h"
namespace forkrot {
using namespace board;
void Display::tftText(int16_t x, int16_t y, uint8_t size, uint16_t fg, uint16_t bg,
                      const String &txt) {
  tft.setTextWrap(false);
  tft.setTextSize(size);
  tft.setTextColor(fg, bg);
  tft.setCursor(x, y);
  tft.print(txt);
}

void Display::tftButton(int x, int y, int w, int h, const String &label, bool active, bool saved) {
  uint16_t fill = TFT_PANEL;
  uint16_t frame = TFT_WARN; // normal calibration button: colored frame
  if (active) {
    fill = TFT_BLUE;
    frame = TFT_AZ; // selected mode/range: bright cyan frame
  }
  if (saved) {
    fill = 0x03E0;
    frame = TFT_OK; // captured calibration point: green frame
  }

  tft.fillRoundRect(x, y, w, h, 5, fill);

  // Two-pixel frame so it is clearly visible on the 2.4" TFT.
  tft.drawRoundRect(x, y, w, h, 5, frame);
  if (w > 4 && h > 4) {
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 4, frame);
  }

  int tw = (int)label.length() * 6 * 2;
  int tx = x + (w - tw) / 2;
  int ty = y + (h - 16) / 2;
  tftText(tx, ty, 2, TFT_TEXT, fill, label);
}

String Display::tftCalibrationText() {
  if (runtime_.hud.isHud())
    return runtime_.hud.available() ? (runtime_.controller.axes[0].calibrated ? "REMOTE OK" : "REMOTE CAL")
                                    : "OFFLINE";
  if (calibration_.state().calibrationMode == CAL_MULTI_POINT) {
    return calibration_.selectedCalibrationValid() ? "MULTI OK" : "MULTI";
  }
  return calibration_.selectedCalibrationValid() ? "2PT OK" : "2-POINT";
}

void Display::resetDashboardCache() {
  dashLastAz = -9999.0f;
  dashLastEl = -9999.0f;
  dashLastTargetAz = -9999.0f;
  dashLastTargetEl = -9999.0f;
  dashLastTargetValid = !runtime_.targetValid; // force first target redraw
  dashLastAzBar = -1;
  dashLastElBar = -1;
  dashLastSpeed[0] = dashLastSpeed[1] = -1;
  dashLastAzState = "";
  dashLastElState = "";
  dashLastCal = "";
  dashLastWifi = "";
  dashLastBt = "";
  dashLastIp = "";
  dashLastTouch = "";
  dashLastTcpConnected = false;
}

void Display::drawDashboardStatic() {
  tft.fillScreen(TFT_BG);
  resetDashboardCache();
  tft.fillRoundRect(3, 3, 314, 29, 5, TFT_BLUE);
  if (runtime_.hud.isHud())
    tftText(10, 9, 2, TFT_TEXT, TFT_BLUE,
            runtime_.hud.active() ? String(runtime_.hud.active()->label).substring(0, 24)
                                  : "HUD: select rotator");
  else {
    tftText(10, 9, 2, TFT_TEXT, TFT_BLUE, "G-5500 ROTATOR");
    tftText(190, 9, 2, TFT_TEXT, TFT_BLUE, "(c)UA1CFM");
  }

  tft.drawRoundRect(3, 36, 154, 139, 6, TFT_BORDER);
  tft.drawRoundRect(163, 36, 154, 139, 6, TFT_BORDER);
  tft.fillRect(4, 37, 152, 21, TFT_PANEL);
  tft.fillRect(164, 37, 152, 21, TFT_PANEL);
  tftText(39, 42, 2, TFT_TEXT, TFT_PANEL, "AZIMUTH");
  tftText(188, 42, 2, TFT_TEXT, TFT_PANEL, "ELEVATION");

  // TARGET label and value are intentionally on ONE line.
  // DELTA is not used in this TFT branch.
  tftText(8, 112, 2, TFT_MUTED, TFT_BG, "TARGET ");
  tftText(168, 112, 2, TFT_MUTED, TFT_BG, "TARGET ");

  tft.drawRoundRect(11, 143, 138, 12, 3, TFT_BORDER);
  tft.drawRoundRect(171, 143, 138, 12, 3, TFT_BORDER);

  // Short large range labels.
  tftText(12, 160, 2, TFT_MUTED, TFT_BG, calibration_.state().gsAzMode == 36 ? "P36" : "P45");
  tftText(172, 160, 2, TFT_MUTED, TFT_BG, "EL");

  // Bottom controls/status. All visible labels are size 2.
  tft.drawRoundRect(3, 180, 118, 26, 4, TFT_WARN);
  tft.drawRoundRect(4, 181, 116, 24, 3, TFT_WARN);
  tft.drawRoundRect(126, 180, 103, 26, 4, TFT_BORDER);
  tft.drawRoundRect(234, 180, 83, 26, 4, TFT_BORDER);
  tft.drawRoundRect(3, 210, 194, 26, 4, TFT_BORDER);
  tft.drawRoundRect(202, 210, 115, 26, 4, TFT_WARN);
  tft.drawRoundRect(203, 211, 113, 24, 3, TFT_WARN);

  tftText(8, 186, 2, TFT_MUTED, TFT_BG, "CAL");
  tftText(131, 186, 2, TFT_MUTED, TFT_BG, "WIFI");
  tftText(239, 186, 2, TFT_MUTED, TFT_BG, "BT");
  tftText(8, 216, 2, TFT_MUTED, TFT_BG, "IP");
  tftText(207, 216, 2, TFT_MUTED, TFT_BG, "TOUCH");
}

void Display::updateDashboard() {
  if (runtime_.hud.isHud()) {
    const uint32_t now = millis();
    if (!tftNeedsRedraw && uint32_t(now - lastDisplayMs) < 20) return;
    lastDisplayMs = now;
    if (tftNeedsRedraw) {
      tft.fillScreen(TFT_BG);
      for (auto &field : hudFields_) field = "\1";
      tftText(12,45,2,TFT_MUTED,TFT_BG,"AZ");tftText(170,45,2,TFT_MUTED,TFT_BG,"EL");
      tftButton(110,170,100,36,"STOP");tftButton(216,170,100,36,"EXEC");
      tftText(12,218,1,TFT_MUTED,TFT_BG,"Tap title to select / hold bottom to calibrate touch");
      tftNeedsRedraw = false;
    }
    const auto &c = runtime_.controller;
    const auto *p = runtime_.hud.active();
    const bool online = runtime_.hud.available();
    const auto field = [&](unsigned i,int x,int y,int w,int h,int size,uint16_t color,const String &text) {
      if (hudFields_[i] == text) return;
      hudFields_[i] = text;tft.fillRect(x,y,w,h,TFT_BG);tftText(x,y,size,color,TFT_BG,text);
    };
    field(0,10,9,300,22,2,TFT_TEXT,p ? String(p->label).substring(0,24) : "HUD: select rotator");
    field(1,12,70,150,36,4,TFT_AZ,online ? String(c.axes[0].current,0) : "---");
    field(2,170,70,145,36,4,TFT_EL,online && c.model==Model::G5500 ? String(c.axes[1].current,0) : "---");
    field(3,12,115,300,22,2,TFT_TEXT,online ? String(c.pendingValid ? "WAIT " : "TARGET ")+String(c.pendingValid?c.pending.az:c.axes[0].target,0) : "HUD OFFLINE");
    field(4,12,143,300,16,1,TFT_MUTED,online ? (c.moving()?"MOVING":"STOPPED") : "No current position");
    const String mode=c.mode==Mode::Manual?"MANUAL":"AUTO";
    if(hudFields_[5]!=mode){hudFields_[5]=mode;tftButton(4,170,100,36,mode);}
    return;
  }
  auto &calibrationState = calibration_.state();
  if (runtime_.controller.model == forkrot::Model::G800DXA) {
    if (millis() - lastDisplayMs < 20 && !tftNeedsRedraw) {
      return;
    }
    lastDisplayMs = millis();
    if (tftNeedsRedraw) {
      tft.fillScreen(TFT_BG);
      tft.fillRoundRect(3, 3, 314, 29, 5, TFT_BLUE);
      tftText(10, 9, 2, TFT_TEXT, TFT_BLUE, "G-800DXA / UA1CFM");
      for(auto &field:hudFields_)field="\1";
      tftButton(3, 180, 118, 26, "CAL");
      tftButton(202, 210, 115, 26, "TOUCH");
      tftNeedsRedraw = false;
    }
    const auto field = [&](unsigned i,int x,int y,int w,int h,int size,uint16_t color,const String &text) {
      if(hudFields_[i]==text)return;
      hudFields_[i]=text;tft.fillRect(x,y,w,h,TFT_BG);tftText(x,y,size,color,TFT_BG,text);
    };
    field(0,12,40,250,18,2,TFT_MUTED,"AZIMUTH");
    field(1,12,62,290,42,5,TFT_AZ,String(runtime_.currentAz,1));
    field(2,12,110,290,22,2,TFT_TEXT,String("TARGET ")+String(runtime_.targetAz,1));
    field(3,12,136,205,14,1,TFT_MUTED,String(runtime_.controller.pendingValid?"WAIT ":"MODE ")+
          (runtime_.controller.pendingValid?String(runtime_.controller.pending.az,1):String(Controller::modeName(runtime_.controller.mode))));
    field(4,12,160,300,14,1,TFT_TEXT,String(calibrationState.gsAzMode==36?"P36 ":"P45 ")+hardware_.motion()+" "+tftCalibrationText());
    const String speed=String(runtime_.controller.axes[0].speedOutputPercent,0)+"%";
    if(hudFields_[5]!=speed){
      tft.fillRect(12,150,296,5,TFT_PANEL);
      tft.fillRect(12,150,int(runtime_.controller.axes[0].speedOutputPercent*2.96f),5,TFT_AZ);
      field(5,220,136,80,14,1,TFT_AZ,speed);
    }
    field(6,130,187,185,18,1,TFT_TEXT,WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString());
    return;
  }
  // Cached fields bound SPI writes; service live state every 20 ms.
  if (millis() - lastDisplayMs < 20 && !tftNeedsRedraw) {
    return;
  }
  lastDisplayMs = millis();

  if (tftNeedsRedraw) {
    drawDashboardStatic();
    tftNeedsRedraw = false;
  }

  // AZ / EL: show integer degrees only.
  int azShown = (int)lroundf(runtime_.currentAz);
  int elShown = (int)lroundf(runtime_.currentEl);

  if ((int)dashLastAz != azShown) {
    tft.fillRect(10, 63, 140, 37, TFT_BG);
    tft.setTextSize(4);
    tft.setTextColor(TFT_AZ, TFT_BG);
    tft.setCursor(16, 67);
    tft.print(azShown);
    tft.setTextSize(2);
    tft.print((char)247);
    dashLastAz = (float)azShown;
  }

  if ((int)dashLastEl != elShown) {
    tft.fillRect(170, 63, 140, 37, TFT_BG);
    tft.setTextSize(4);
    tft.setTextColor(TFT_EL, TFT_BG);
    tft.setCursor(176, 67);
    tft.print(elShown);
    tft.setTextSize(2);
    tft.print((char)247);
    dashLastEl = (float)elShown;
  }

  // TARGET only, integer degrees, on the SAME line as "TARGET -".
  int taShown = (int)lroundf(runtime_.targetAz);
  int teShown = (int)lroundf(runtime_.targetEl);

  bool needTarget = (runtime_.targetValid != dashLastTargetValid) ||
                    (runtime_.targetValid &&
                     ((int)dashLastTargetAz != taShown || (int)dashLastTargetEl != teShown));

  if (needTarget) {
    tft.fillRect(100, 109, 50, 24, TFT_BG);
    tft.fillRect(260, 109, 50, 24, TFT_BG);

    if (runtime_.targetValid) {
      tftText(100, 112, 2, TFT_TARGET, TFT_BG, String(taShown) + String((char)247));
      tftText(260, 112, 2, TFT_TARGET, TFT_BG, String(teShown) + String((char)247));
    } else {
      tftText(100, 112, 2, TFT_MUTED, TFT_BG, "---");
      tftText(260, 112, 2, TFT_MUTED, TFT_BG, "---");
    }

    dashLastTargetValid = runtime_.targetValid;
    dashLastTargetAz = (float)taShown;
    dashLastTargetEl = (float)teShown;
  }

  // Progress bars: only change when width changes.
  float azMax = calibration_.activeAzMaxDeg();
  int azw = (int)constrain(runtime_.currentAz / azMax * 134.0f, 0.0f, 134.0f);
  int elw = (int)constrain(runtime_.currentEl / 180.0f * 134.0f, 0.0f, 134.0f);

  if (azw != dashLastAzBar) {
    // Instead of clearing the whole bar every time, change only the difference.
    if (dashLastAzBar < 0) {
      tft.fillRect(13, 145, 134, 8, TFT_BG);
      if (azw > 0) {
        tft.fillRect(13, 145, azw, 8, TFT_AZ);
      }
    } else if (azw > dashLastAzBar) {
      tft.fillRect(13 + dashLastAzBar, 145, azw - dashLastAzBar, 8, TFT_AZ);
    } else if (azw < dashLastAzBar) {
      tft.fillRect(13 + azw, 145, dashLastAzBar - azw, 8, TFT_BG);
    }
    dashLastAzBar = azw;
  }

  if (elw != dashLastElBar) {
    if (dashLastElBar < 0) {
      tft.fillRect(173, 145, 134, 8, TFT_BG);
      if (elw > 0) {
        tft.fillRect(173, 145, elw, 8, TFT_EL);
      }
    } else if (elw > dashLastElBar) {
      tft.fillRect(173 + dashLastElBar, 145, elw - dashLastElBar, 8, TFT_EL);
    } else if (elw < dashLastElBar) {
      tft.fillRect(173 + elw, 145, dashLastElBar - elw, 8, TFT_BG);
    }
    dashLastElBar = elw;
  }

  for (unsigned i=0; i<2; ++i) {
    const int percent = int(lroundf(runtime_.controller.axes[i].speedOutputPercent));
    if (percent != dashLastSpeed[i]) {
      const int x = i ? 173 : 13;
      tft.fillRect(x, 132, 90, 4, TFT_PANEL);
      tft.fillRect(x, 132, percent*90/100, 4, i ? TFT_EL : TFT_AZ);
      tft.fillRect(x+92, 129, 42, 9, TFT_BG);
      tftText(x+92, 129, 1, TFT_MUTED, TFT_BG, String(percent) + "%");
      dashLastSpeed[i] = percent;
    }
  }
  // Motion text: update only on state change.
  // Read each axis directly from its GPIOs so simultaneous motion
  // (e.g. UP+RIGHT) is displayed correctly.
  bool left = runtime_.controller.axes[0].direction < 0;
  bool right = runtime_.controller.axes[0].direction > 0;
  bool up = runtime_.controller.axes[1].direction > 0;
  bool down = runtime_.controller.axes[1].direction < 0;

  bool azMoving = left || right;
  bool elMoving = up || down;

  String azState = right ? "RIGHT" : left ? "LEFT" : "STOP";

  String elState = up ? "UP" : down ? "DOWN" : "STOP";

  if (azState != dashLastAzState) {
    tft.fillRect(60, 158, 90, 17, TFT_BG);
    tftText(64, 160, 2, azMoving ? TFT_OK : TFT_STOP, TFT_BG, azState);
    dashLastAzState = azState;
  }

  if (elState != dashLastElState) {
    tft.fillRect(218, 158, 92, 17, TFT_BG);
    tftText(222, 160, 2, elMoving ? TFT_OK : TFT_STOP, TFT_BG, elState);
    dashLastElState = elState;
  }

  // Bottom status fields: repaint only when text changes.
  String calText = (calibrationState.calibrationMode == CAL_MULTI_POINT) ? "MULTI" : "2PT";
  if (calText != dashLastCal) {
    tft.fillRect(48, 183, 68, 20, TFT_BG);
    tftText(50, 186, 2, calibration_.selectedCalibrationValid() ? TFT_OK : TFT_WARN, TFT_BG,
            calText);
    dashLastCal = calText;
  }

  String wifiText;
  uint16_t wifiColor;
  if (WiFi.status() == WL_CONNECTED) {
    wifiText = String(WiFi.RSSI());
    wifiColor = TFT_OK;
  } else {
    wifiText = "AP";
    wifiColor = TFT_WARN;
  }
  if (wifiText != dashLastWifi) {
    tft.fillRect(183, 183, 40, 20, TFT_BG);
    tftText(184, 186, 2, wifiColor, TFT_BG, wifiText);
    dashLastWifi = wifiText;
  }

  bool bt = network_.bluetoothConnected();
  String btText = BLUETOOTH_ENABLED ? (bt ? "ON" : "OFF") : "N/A";
  if (btText != dashLastBt) {
    tft.fillRect(271, 183, 40, 20, TFT_BG);
    tftText(272, 186, 2, bt ? TFT_OK : TFT_MUTED, TFT_BG, btText);
    dashLastBt = btText;
  }

  String ip =
      (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  bool tcpConnected = (network_.tcpConnected());
  if (ip != dashLastIp || tcpConnected != dashLastTcpConnected) {
    tft.fillRect(34, 213, 160, 20, TFT_BG);
    tftText(36, 216, 2, tcpConnected ? TFT_OK : TFT_TEXT, TFT_BG, ip);
    dashLastIp = ip;
    dashLastTcpConnected = tcpConnected;
  }

  String touchText = touchCalibration_.touchCalValid ? "OK" : "CAL";
  if (touchText != dashLastTouch) {
    tft.fillRect(273, 213, 39, 20, TFT_BG);
    tftText(274, 216, 2, touchCalibration_.touchCalValid ? TFT_OK : TFT_WARN, TFT_BG, touchText);
    dashLastTouch = touchText;
  }
}

void Display::drawCalHeader(const String &title) {
  tft.fillScreen(TFT_BG);
  tft.fillRoundRect(3, 3, 314, 29, 5, TFT_BLUE);
  tftText(10, 9, 2, TFT_TEXT, TFT_BLUE, title);
  tftButton(252, 5, 63, 25, "BACK");
}

void Display::drawCalHome() {
  auto &calibrationState = calibration_.state();
  drawCalHeader("CALIBRATION");

  tftText(8, 39, 2, TFT_MUTED, TFT_BG, "MODE");
  tftButton(8, 55, 145, 34, "2-POINT", calibrationState.calibrationMode == CAL_GS232_2POINT);
  tftButton(167, 55, 145, 34, "MULTI", calibrationState.calibrationMode == CAL_MULTI_POINT);

  tftText(8, 96, 2, TFT_MUTED, TFT_BG, "AZ RANGE");
  tftButton(8, 112, 145, 34, "P36", calibrationState.gsAzMode == 36);
  tftButton(167, 112, 145, 34, "P45", calibrationState.gsAzMode == 45);

  tftButton(8, 166, 145, 52, "AZ CAL");
  if (runtime_.controller.model == forkrot::Model::G5500) {
    tftButton(167, 166, 145, 52, "EL CAL");
  }

  tftText(8, 223, 2, TFT_MUTED, TFT_BG,
          String("A") + String(hardware_.adcAvgMv(PIN_AZ_ADC)) +
              (runtime_.controller.model == forkrot::Model::G5500
                   ? String(" E") + String(hardware_.adcAvgMv(PIN_EL_ADC))
                   : String("")) +
              " mV");
}

void Display::drawCalAz() {
  auto &calibrationState = calibration_.state();
  drawCalHeader(String("AZ CAL ") + (calibrationState.gsAzMode == 36 ? "P36" : "P45"));
  int mv = hardware_.adcAvgMv(PIN_AZ_ADC);
  tftText(8, 40, 2, TFT_AZ, TFT_BG, String("ADC ") + String(mv) + " mV");

  if (calibrationState.calibrationMode == CAL_GS232_2POINT) {
    tftButton(8, 70, 145, 48, "O = 0", false, calibration_.activeAzOSet());
    tftButton(167, 70, 145, 48, calibrationState.gsAzMode == 36 ? "F = 360" : "F = 450", false,
              calibration_.activeAzFSet());
    tftText(8, 130, 2, TFT_MUTED, TFT_BG,
            String("O:") +
                (calibration_.activeAzOSet() ? String(calibration_.activeAz2Zero()) : "---"));
    tftText(167, 130, 2, TFT_MUTED, TFT_BG,
            String("F:") +
                (calibration_.activeAzFSet() ? String(calibration_.activeAz2Full()) : "---"));
    tftButton(8, 166, 304, 45, "RESET AZ");
  } else {
    const int vals36[5] = {0, 90, 180, 270, 360};
    const int vals45[6] = {0, 90, 180, 270, 360, 450};
    int n = calibrationState.gsAzMode == 36 ? 5 : 6;
    for (int i = 0; i < n; i++) {
      int row = i / 3, col = i % 3;
      int x = 8 + col * 104, y = 68 + row * 58;
      int deg = calibrationState.gsAzMode == 36 ? vals36[i] : vals45[i];
      int val = calibrationState.gsAzMode == 36 ? calibrationState.az36CalMv[i]
                                                : calibrationState.az45CalMv[i];
      tftButton(x, y, 96, 44, String(deg), false, val >= 0);
    }
    tftButton(8, 190, 304, 38, "RESET AZ");
  }
}

void Display::drawCalEl() {
  auto &calibrationState = calibration_.state();
  drawCalHeader("EL CAL");
  int mv = hardware_.adcAvgMv(PIN_EL_ADC);
  tftText(8, 40, 2, TFT_EL, TFT_BG, String("ADC ") + String(mv) + " mV");

  if (calibrationState.calibrationMode == CAL_GS232_2POINT) {
    tftButton(8, 70, 145, 48, "O2 = 0", false, calibrationState.elOSet);
    tftButton(167, 70, 145, 48, "F2 = 180", false, calibrationState.elFSet);
    tftText(8, 130, 2, TFT_MUTED, TFT_BG,
            String("O2:") + (calibrationState.elOSet ? String(calibrationState.cal.el0) : "---"));
    tftText(167, 130, 2, TFT_MUTED, TFT_BG,
            String("F2:") + (calibrationState.elFSet ? String(calibrationState.cal.elMax) : "---"));
    tftButton(8, 166, 304, 45, "RESET EL");
  } else {
    const int vals[5] = {0, 45, 90, 135, 180};
    for (int i = 0; i < 5; i++) {
      int row = i / 3, col = i % 3;
      int x = 8 + col * 104, y = 68 + row * 58;
      tftButton(x, y, 96, 44, String(vals[i]), false, calibrationState.elCalMv[i] >= 0);
    }
    tftButton(8, 190, 304, 38, "RESET EL");
  }
}

void Display::drawTouchCalibration() {
  tft.fillScreen(TFT_BG);
  tft.fillRoundRect(3, 3, 314, 29, 5, TFT_BLUE);
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
  tft.drawCircle(x, y, 10, TFT_WARN);
  tft.drawFastHLine(x - 15, y, 31, TFT_WARN);
  tft.drawFastVLine(x, y - 15, 31, TFT_WARN);
}

void Display::startTouchCalibration() {
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
  if (!touch.touched()) {
    return false;
  }
  long sx = 0, sy = 0;
  int n = 0;
  for (int i = 0; i < 12; i++) {
    if (touch.touched()) {
      TS_Point p = touch.getPoint();
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
  long horizDX = labs(touchCalRawX[1] - touchCalRawX[0]) + labs(touchCalRawX[2] - touchCalRawX[3]);
  long horizDY = labs(touchCalRawY[1] - touchCalRawY[0]) + labs(touchCalRawY[2] - touchCalRawY[3]);

  touchCalibration_.touchSwapXY = (horizDY > horizDX);

  int sx0 = touchCalibration_.touchSwapXY ? touchCalRawY[0] : touchCalRawX[0];
  int sx1 = touchCalibration_.touchSwapXY ? touchCalRawY[1] : touchCalRawX[1];
  int sx2 = touchCalibration_.touchSwapXY ? touchCalRawY[2] : touchCalRawX[2];
  int sx3 = touchCalibration_.touchSwapXY ? touchCalRawY[3] : touchCalRawX[3];

  int sy0 = touchCalibration_.touchSwapXY ? touchCalRawX[0] : touchCalRawY[0];
  int sy1 = touchCalibration_.touchSwapXY ? touchCalRawX[1] : touchCalRawY[1];
  int sy2 = touchCalibration_.touchSwapXY ? touchCalRawX[2] : touchCalRawY[2];
  int sy3 = touchCalibration_.touchSwapXY ? touchCalRawX[3] : touchCalRawY[3];

  touchCalibration_.touchX0Raw = (sx0 + sx3) / 2;
  touchCalibration_.touchX1Raw = (sx1 + sx2) / 2;
  touchCalibration_.touchY0Raw = (sy0 + sy1) / 2;
  touchCalibration_.touchY1Raw = (sy2 + sy3) / 2;

  touchCalibration_.touchCalValid =
      (abs(touchCalibration_.touchX1Raw - touchCalibration_.touchX0Raw) > 500 &&
       abs(touchCalibration_.touchY1Raw - touchCalibration_.touchY0Raw) > 500);
  saveTouchCalibration();
}

void Display::serviceTouchCalibration() {
  if (tftPage != TFT_PAGE_TOUCH_CAL) {
    return;
  }

  if (touchCalStep >= 4) {
    if (!touch.touched()) {
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
    if (readTouchXY(x, y) && hit(x, y, 45, 145, 230, 68)) {
      tftPage = TFT_PAGE_DASH;
      tftNeedsRedraw = true;
      drawTftPage();
      touchWaitRelease = true;
    }
    return;
  }

  if (!touch.touched()) {
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

void Display::showHeaderImage() {
  // Full-screen image, but controller logic keeps running normally.
  tft.fillScreen(TFT_BG);
  tft.drawRGBBitmap(0, 0, catImage, CAT_IMAGE_W, CAT_IMAGE_H);
  imagePageStartedMs = millis();
  tftPage = TFT_PAGE_IMAGE;
  tftNeedsRedraw = false;
}

void Display::serviceHeaderImage() {
  if (tftPage != TFT_PAGE_IMAGE) {
    return;
  }
  if (millis() - imagePageStartedMs >= IMAGE_PAGE_DURATION_MS) {
    tftPage = TFT_PAGE_DASH;
    tftNeedsRedraw = true;
    drawTftPage();
  }
}

void Display::drawTftPage() {
  if (tftPage == TFT_PAGE_HUD) {
    drawHudList();
    return;
  }
  if (tftPage == TFT_PAGE_DASH) {
    drawDashboardStatic();
  } else if (tftPage == TFT_PAGE_CAL_HOME) {
    drawCalHome();
  } else if (tftPage == TFT_PAGE_CAL_AZ) {
    drawCalAz();
  } else if (tftPage == TFT_PAGE_CAL_EL) {
    drawCalEl();
  } else if (tftPage == TFT_PAGE_TOUCH_CAL) {
    drawTouchCalibration();
  } else if (tftPage == TFT_PAGE_IMAGE) {
    return;
  }
  tftNeedsRedraw = false;
}

bool Display::hit(int x, int y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

bool Display::readTouchXY(int &x, int &y) {
  if (!touch.touched()) {
    return false;
  }
  TS_Point p = touch.getPoint();

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

void Display::resetTouchCalAxis(bool az) {
  auto &calibrationState = calibration_.state();
  if (calibrationState.calibrationMode == CAL_GS232_2POINT) {
    if (az) {
      if (calibrationState.gsAzMode == 36) {
        calibrationState.cal.az36_0 = 0;
        calibrationState.cal.az36Max = 3100;
        calibrationState.az36OSet = false;
        calibrationState.az36FSet = false;
        calibrationState.az36LinearValid = false;
      } else {
        calibrationState.cal.az45_0 = 0;
        calibrationState.cal.az45Max = 3100;
        calibrationState.az45OSet = false;
        calibrationState.az45FSet = false;
        calibrationState.az45LinearValid = false;
      }
    } else {
      calibrationState.cal.el0 = 0;
      calibrationState.cal.elMax = 3100;
      calibrationState.elOSet = false;
      calibrationState.elFSet = false;
      calibrationState.elLinearValid = false;
    }
  } else {
    if (az) {
      if (calibrationState.gsAzMode == 36) {
        for (uint8_t i = 0; i < AZ36_CAL_N; i++) {
          calibrationState.az36CalMv[i] = -1;
        }
      } else {
        for (uint8_t i = 0; i < AZ45_CAL_N; i++) {
          calibrationState.az45CalMv[i] = -1;
        }
      }
    } else {
      for (uint8_t i = 0; i < EL_CAL_N; i++) {
        calibrationState.elCalMv[i] = -1;
      }
    }
    calibration_.refreshMultiCalValidity();
  }
  actions_.saveCal();
  tftNeedsRedraw = true;
}

void Display::captureTouchMultiAz(uint8_t idx) {
  auto &calibrationState = calibration_.state();
  int mv = hardware_.adcAvgMv(PIN_AZ_ADC);
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
  if (runtime_.controller.model == forkrot::Model::G800DXA) {
    return;
  }
  if (idx >= EL_CAL_N) {
    return;
  }
  calibration_.state().elCalMv[idx] = hardware_.adcAvgMv(PIN_EL_ADC);
  calibration_.refreshMultiCalValidity();
  actions_.saveCal();
  tftNeedsRedraw = true;
}

void Display::serviceTouchRecoveryHold() {
  if (tftPage != TFT_PAGE_DASH) {
    touchHoldStartMs = 0;
    touchHoldArmed = false;
    return;
  }

  if (touch.touched()) {
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

void Display::drawHudList() {
  tft.fillScreen(TFT_BG);
  tftText(12, 8, 2, TFT_TEXT, TFT_BG, "HUD: select / back");
  size_t index = 0, shown = 0;
  for (auto *p = runtime_.hud.peers(); p; p = p->next.get())
    if (p->saved) {
      if (index++ < hudPageOffset_)
        continue;
      if (shown == 3)
        break;
      tftText(12, 44 + int(shown) * 53, 2, p->id == runtime_.hud.config.selected ? TFT_OK : TFT_TEXT, TFT_BG,
              p->label);
      tftText(
          12, 65 + int(shown) * 53, 1, TFT_MUTED, TFT_BG,
          p->received
              ? String(p->fresh(millis())
                           ? ((p->state.axes[0].direction || p->state.axes[1].direction) ? "MOVE " : "STOP ")
                           : "OLD ") +
                    String(p->state.axes[0].current, 1) + " / " + String(p->state.axes[1].current, 1) +
                    "  age " + String((millis() - p->receivedAt) / 1000) + "s"
              : "OFFLINE");
      ++shown;
    }
  tftText(20, 215, 2, TFT_TEXT, TFT_BG, "< PREV      NEXT >");
}
void Display::serviceTouch() {
  auto &calibrationState = calibration_.state();
  if (tftPage == TFT_PAGE_IMAGE) {
    return;
  }
  if (tftPage == TFT_PAGE_TOUCH_CAL) {
    serviceTouchCalibration();
    return;
  }

  // Raw long-press recovery does not depend on touch calibration.
  serviceTouchRecoveryHold();
  if (tftPage == TFT_PAGE_TOUCH_CAL) {
    return;
  }

  int x, y;
  if (!readTouchXY(x, y)) {
    return;
  }
  if (millis() - lastTouchMs < 260) {
    return;
  }
  lastTouchMs = millis();

  // BACK exists on every calibration page.
  if (tftPage != TFT_PAGE_DASH && tftPage != TFT_PAGE_HUD && hit(x, y, 235, 0, 85, 40)) {
    if (tftPage == TFT_PAGE_CAL_HOME) {
      tftPage = TFT_PAGE_DASH;
    } else {
      tftPage = TFT_PAGE_CAL_HOME;
    }
    tftNeedsRedraw = true;
    drawTftPage();
    return;
  }

  if (runtime_.hud.isHud() && tftPage == TFT_PAGE_HUD) {
    if (y < 40) {
      tftPage = TFT_PAGE_DASH;
      resetDashboardCache();
      drawTftPage();
      return;
    }
    if (y >= 200) {
      if (x < 160)
        hudPageOffset_ = hudPageOffset_ >= 3 ? hudPageOffset_ - 3 : 0;
      else if (hudPageOffset_ + 3 < runtime_.hud.count())
        hudPageOffset_ += 3;
      drawHudList();
      return;
    }
    const size_t wanted = hudPageOffset_ + size_t((y - 40) / 53);
    size_t index = 0;
    for (auto *p = runtime_.hud.peers(); p; p = p->next.get())
      if (p->saved && index++ == wanted) {
        runtime_.hud.select(p->id);
        if (!storage_.saveHudConfig(runtime_.hud.config))
          runtime_.hud.catalogError = true;
        tftPage = TFT_PAGE_DASH;
        resetDashboardCache();
        drawTftPage();
        break;
      }
    return;
  }
  if (runtime_.hud.isHud() && tftPage == TFT_PAGE_DASH) {
    if (y < 40) {
      tftPage = TFT_PAGE_HUD;
      drawHudList();
    } else if (y >= 170 && y < 206) {
      if (x < 106) {
        ControlCommand command;
        command.op = ControlOp::ToggleMode;
        runtime_.hud.submit(command);
      } else if (x < 212)
        runtime_.controller.stopNow(-1, millis());
      else
        runtime_.controller.execute(millis());
      tftNeedsRedraw = true;
    }
    return;
  }
  if (tftPage == TFT_PAGE_DASH) {
    // Tap the top title bar to show the cat image for 3 seconds.
    if (hit(x, y, 0, 0, 320, 40)) {
      showHeaderImage();
    } else if (hit(x, y, 0, 174, 125, 36)) {
      tftPage = TFT_PAGE_CAL_HOME;
      tftNeedsRedraw = true;
      drawTftPage();
    } else if (hit(x, y, 198, 204, 122, 36)) {
      startTouchCalibration();
    }
    return;
  }

  if (tftPage == TFT_PAGE_CAL_HOME) {
    if (hit(x, y, 8, 55, 145, 34)) {
      actions_.setCalibrationMode(CAL_GS232_2POINT);
      actions_.saveCal();
      tftNeedsRedraw = true;
    } else if (hit(x, y, 167, 55, 145, 34)) {
      actions_.setCalibrationMode(CAL_MULTI_POINT);
      actions_.saveCal();
      tftNeedsRedraw = true;
    } else if (hit(x, y, 8, 112, 145, 34)) {
      actions_.setGsAzMode(36);
      actions_.saveCal();
      tftNeedsRedraw = true;
    } else if (hit(x, y, 167, 112, 145, 34)) {
      actions_.setGsAzMode(45);
      actions_.saveCal();
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

  if (tftPage == TFT_PAGE_CAL_AZ) {
    if (calibrationState.calibrationMode == CAL_GS232_2POINT) {
      if (hit(x, y, 8, 70, 145, 48)) {
        actions_.captureGs232Cal("O");
      } else if (hit(x, y, 167, 70, 145, 48)) {
        actions_.captureGs232Cal("F");
      } else if (hit(x, y, 4, 158, 312, 62)) {
        resetTouchCalAxis(true);
      }
    } else {
      int n = calibrationState.gsAzMode == 36 ? 5 : 6;
      for (int i = 0; i < n; i++) {
        int row = i / 3, col = i % 3;
        if (hit(x, y, 8 + col * 104, 68 + row * 58, 96, 44)) {
          captureTouchMultiAz((uint8_t)i);
          break;
        }
      }
      if (hit(x, y, 4, 184, 312, 52)) {
        resetTouchCalAxis(true);
      }
    }
    tftNeedsRedraw = true;
    drawTftPage();
    return;
  }

  if (tftPage == TFT_PAGE_CAL_EL) {
    if (calibrationState.calibrationMode == CAL_GS232_2POINT) {
      if (hit(x, y, 8, 70, 145, 48)) {
        actions_.captureGs232Cal("O2");
      } else if (hit(x, y, 167, 70, 145, 48)) {
        actions_.captureGs232Cal("F2");
      } else if (hit(x, y, 4, 158, 312, 62)) {
        resetTouchCalAxis(false);
      }
    } else {
      for (int i = 0; i < 5; i++) {
        int row = i / 3, col = i % 3;
        if (hit(x, y, 8 + col * 104, 68 + row * 58, 96, 44)) {
          captureTouchMultiEl((uint8_t)i);
          break;
        }
      }
      if (hit(x, y, 4, 184, 312, 52)) {
        resetTouchCalAxis(false);
      }
    }
    tftNeedsRedraw = true;
    drawTftPage();
  }
}

void Display::initLocalDisplay() {
  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TOUCH_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TOUCH_CS, HIGH);

  SPI.begin(PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.begin();
  tft.setRotation(1);

  touch.begin(SPI);
  touch.setRotation(1);

  tft.fillScreen(TFT_BG);
  tftText(23, 62, 2, TFT_TEXT, TFT_BG, "G-5500 " FORK_CHIP_NAME);
  tftText(40, 92, 2, TFT_TARGET, TFT_BG, "TFT + TOUCH");
  tftText(72, 122, 2, TFT_OK, TFT_BG, "UA1CFM");
  tftText(38, 154, 2, TFT_WARN, TFT_BG, "FW v1.6.23 P36/P45");
  delay(1200);

  tftPage = TFT_PAGE_DASH;
  tftNeedsRedraw = true;
}

void Display::updateLocalDisplay() {
  if (tftPage == TFT_PAGE_IMAGE) {
    serviceHeaderImage();
    return;
  }
  if (tftPage == TFT_PAGE_DASH) {
    updateDashboard();
  } else if (tftPage == TFT_PAGE_HUD && uint32_t(millis() - lastDisplayMs) >= 1000) {
    lastDisplayMs = millis();
    drawHudList();
  } else if (tftNeedsRedraw) {
    drawTftPage();
  }
}
void Display::startAfterCalibrationLoad() {
  if (!touchCalibration_.touchCalValid) {
    startTouchCalibration();
  } else {
    tftPage = TFT_PAGE_DASH;
    tftNeedsRedraw = true;
    drawTftPage();
  }
}

} // namespace forkrot

#elif DISPLAY_KIND == DISPLAY_LCD1602
#include "display.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>
namespace forkrot {
void Display::initLocalDisplay() {
  lcd_.begin(16, 2);
  for (uint8_t width=1; width<=5; ++width) {
    uint8_t pixels[8];
    for (auto &row : pixels) row = uint8_t(0x1f << (5-width)) & 0x1f;
    lcd_.createChar(width, pixels);
  }
}
void Display::updateLocalDisplay() {
  const uint32_t now = millis();
  if (!redraw_ && uint32_t(now-updatedAt_) < 20) return;
  const bool force = redraw_;
  redraw_ = false; updatedAt_ = now;
  for (unsigned i=0; i<2; ++i) {
    char text[40];
    const auto &axis = runtime_.controller.axes[i];
    const char mode = runtime_.controller.pendingValid ? 'W' : runtime_.controller.mode == Mode::Manual ? 'M' : runtime_.controller.mode == Mode::AutoHour ? 'H' : 'A';
    if (runtime_.hud.isHud() && !runtime_.hud.available()) {
      std::snprintf(text, sizeof text, "%-16.16s",
                    i ? (runtime_.hud.active() ? runtime_.hud.active()->label : "Select via Web")
                      : "HUD OFFLINE");
    } else if (i && runtime_.controller.model == Model::G800DXA) {
      std::snprintf(text, sizeof text, "%s %-9s", runtime_.controller.pendingValid ? "WAIT" : "MODE", Controller::modeName(runtime_.controller.mode));
    } else {
      char target[12] = "---";
      const auto &c = runtime_.controller;
      const bool waiting = c.pendingValid && (i ? c.pending.hasEl : c.pending.hasAz);
      if (waiting || c.targetKnown) std::snprintf(target, sizeof target, "%3d", int(lroundf(waiting ? (i ? c.pending.el : c.pending.az) : axis.target)));
      std::snprintf(text, sizeof text, "%c%3d>%3s %3d%%%c%c", i ? 'E' : 'A', int(lroundf(axis.current)), target, int(lroundf(axis.speedOutputPercent)), mode, axis.direction > 0 ? '+' : axis.direction < 0 ? '-' : '=');
    }
    for (unsigned col=0; col<16; ++col) {
      uint8_t value;
      if (col==15 && !(i && runtime_.controller.model == Model::G800DXA)) {
        const uint8_t width=uint8_t(lroundf(axis.speedOutputPercent*5.f/100.f));
        value = width ? width : uint8_t(' ');
      } else value = col < std::strlen(text) ? text[col] : ' ';
      if(force || value!=lcdPrevious_[i][col]){lcd_.setCursor(col,i);lcd_.write(value);lcdPrevious_[i][col]=value;}
    }
  }
}
}
#endif
