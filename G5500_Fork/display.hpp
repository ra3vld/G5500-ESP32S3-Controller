// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once
#include "build_config.hpp"

#if TFT_DISPLAY_ENABLED
#include <SPI.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include "board.hpp"
#include "runtime.hpp"
#include "calibration.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "network.hpp"
#include "storage.hpp"
#include "device_actions.hpp"
#include "display_state.hpp"
namespace forkrot {
class Display {
public:
  Display(Runtime &runtime, DeviceActions &actions, Calibration &calibration, Hardware &hardware,
          Diagnostics &diagnostics, Network &network, Storage &storage)
      : runtime_(runtime), actions_(actions), calibration_(calibration), hardware_(hardware),
        diagnostics_(diagnostics), network_(network), storage_(storage) {}
  void invalidate() {
    tftNeedsRedraw = true;
  }
  void loadTouchCalibration() {
    storage_.loadTouchCalibration(touchCalibration_);
  }
  void startAfterCalibrationLoad();
  void serviceTouch();
  void initLocalDisplay();
  void updateLocalDisplay();

private:
  void tftText(int16_t x, int16_t y, uint8_t size, uint16_t fg, uint16_t bg, const String &txt);
  void tftButton(int x, int y, int w, int h, const String &label, bool active = false,
                 bool saved = false);
  String tftCalibrationText();
  void resetDashboardCache();
  void drawDashboardStatic();
  void updateDashboard();
  void drawCalHeader(const String &title);
  void drawCalHome();
  void drawCalAz();
  void drawCalEl();
  void drawTouchCalibration();
  void startTouchCalibration();
  bool readTouchRawAverage(int &rx, int &ry);
  void finishTouchCalibration();
  void serviceTouchCalibration();
  void showHeaderImage();
  void serviceHeaderImage();
  void drawTftPage();
  bool hit(int x, int y, int bx, int by, int bw, int bh);
  bool readTouchXY(int &x, int &y);
  void resetTouchCalAxis(bool az);
  void captureTouchMultiAz(uint8_t idx);
  void captureTouchMultiEl(uint8_t idx);
  void serviceTouchRecoveryHold();
  void saveTouchCalibration() {
    storage_.saveTouchCalibration(touchCalibration_);
  }
  Runtime &runtime_;
  DeviceActions &actions_;
  Calibration &calibration_;
  Hardware &hardware_;
  Diagnostics &diagnostics_;
  Network &network_;
  Storage &storage_;
  TouchCalibration touchCalibration_;
  Adafruit_ILI9341 tft{&SPI, board::PIN_TFT_DC, board::PIN_TFT_CS, board::PIN_TFT_RST};
  XPT2046_Touchscreen touch{board::PIN_TOUCH_CS, board::PIN_TOUCH_IRQ};
  uint32_t lastDisplayMs = 0;
  enum TftPage : uint8_t {
    TFT_PAGE_DASH = 0,
    TFT_PAGE_CAL_HOME = 1,
    TFT_PAGE_CAL_AZ = 2,
    TFT_PAGE_CAL_EL = 3,
    TFT_PAGE_TOUCH_CAL = 4,
    TFT_PAGE_IMAGE = 5
  };
  TftPage tftPage = TFT_PAGE_DASH;
  uint32_t lastTouchMs = 0;
  uint32_t imagePageStartedMs = 0;
  static constexpr uint32_t IMAGE_PAGE_DURATION_MS = 3000;
  bool tftNeedsRedraw = true;

  // Dashboard anti-flicker cache.
  // Dynamic regions are repainted only when their visible value actually changes.
  float dashLastAz = -9999.0f, dashLastEl = -9999.0f;
  float dashLastTargetAz = -9999.0f, dashLastTargetEl = -9999.0f;
  bool dashLastTargetValid = false;
  int dashLastAzBar = -1, dashLastElBar = -1;
  String dashLastAzState = "", dashLastElState = "";
  String dashLastCal = "", dashLastWifi = "", dashLastBt = "", dashLastIp = "", dashLastTouch = "";
  bool dashLastTcpConnected = false;
  uint8_t touchCalStep = 0;
  int touchCalRawX[4] = {0, 0, 0, 0};
  int touchCalRawY[4] = {0, 0, 0, 0};
  bool touchWaitRelease = false;
  uint32_t touchHoldStartMs = 0;
  bool touchHoldArmed = false;

  static constexpr uint16_t TFT_BG = ILI9341_BLACK;
  static constexpr uint16_t TFT_PANEL = 0x18E3;
  static constexpr uint16_t TFT_BORDER = 0x4208;
  static constexpr uint16_t TFT_TEXT = ILI9341_WHITE;
  static constexpr uint16_t TFT_MUTED = 0x9CF3;
  static constexpr uint16_t TFT_AZ = ILI9341_CYAN;
  static constexpr uint16_t TFT_EL = ILI9341_YELLOW;
  static constexpr uint16_t TFT_OK = ILI9341_GREEN;
  static constexpr uint16_t TFT_WARN = ILI9341_ORANGE;
  static constexpr uint16_t TFT_STOP = ILI9341_RED;
  static constexpr uint16_t TFT_BLUE = ILI9341_BLUE;
  static constexpr uint16_t TFT_TARGET = ILI9341_MAGENTA;
};
} // namespace forkrot

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
