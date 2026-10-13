#include "display.hpp"
#include <type_traits>

// This file deliberately has no Arduino or display libraries in its include path.
static_assert(!TFT_DISPLAY_ENABLED);
static_assert(std::is_empty_v<forkrot::Display>);

void exerciseDisplayInterface(forkrot::Display &display) {
  display.invalidate();
  display.initLocalDisplay();
  display.loadTouchCalibration();
  display.startAfterCalibrationLoad();
  display.serviceTouch();
  display.updateLocalDisplay();
}
