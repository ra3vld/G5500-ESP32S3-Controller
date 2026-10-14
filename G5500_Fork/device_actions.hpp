// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <Arduino.h>
#include "calibration.hpp"
namespace forkrot {
// UI/transport callbacks into the application. No service locator or globals.
class DeviceActions {
public:
  virtual ~DeviceActions() = default;
  virtual void updatePosition() = 0;
  virtual void setCalibrationMode(CalibrationMode mode) = 0;
  virtual void setGsAzMode(uint8_t mode) = 0;
  virtual void captureGs232Cal(const String &command) = 0;
  virtual void saveCal() = 0;
  virtual void loadPrefs() = 0;
  virtual void loadProfilePresets() = 0;
  virtual void saveProfilePresets() = 0;
  virtual void saveForkSettings() = 0;
  virtual void setupWeb() = 0;
  virtual void invalidateDisplay() = 0;
};
} // namespace forkrot
