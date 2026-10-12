// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once
#include "board.hpp"
namespace forkrot {
struct TouchCalibration {
  bool touchCalValid = false, touchSwapXY = true;
  int touchX0Raw = board::TOUCH_RAW_Y_MIN, touchX1Raw = board::TOUCH_RAW_Y_MAX;
  int touchY0Raw = board::TOUCH_RAW_X_MAX, touchY1Raw = board::TOUCH_RAW_X_MIN;
};
} // namespace forkrot
