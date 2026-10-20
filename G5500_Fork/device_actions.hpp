// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <Arduino.h>
#include "calibration.hpp"
#include "api.hpp"
namespace forkrot {
// UI/transport callbacks into the application. No service locator or globals.
class DeviceActions {
public:
  virtual ~DeviceActions() = default;
  virtual void updatePosition() = 0;
  virtual void setCalibrationMode(CalibrationMode mode) = 0;
  virtual void setGsAzMode(uint8_t mode) = 0;
  virtual void captureGs232Cal(const String &command) = 0;
  virtual bool saveCal() = 0;
  virtual bool configureAzRange(uint8_t profile, float start, float span) = 0;
  virtual bool editAzPoint(const char *operation, float angle) = 0;
  virtual bool configureElRange(uint8_t profile, float start, float span) = 0;
  virtual bool editElPoint(const char *operation, float angle) = 0;
  virtual void loadPrefs() = 0;
  virtual void loadMotionSettings() = 0;
  virtual void loadProfilePresets() = 0;
  virtual void saveProfilePresets() = 0;
  virtual void saveForkSettings() = 0;
  virtual void setupWeb() = 0;
  virtual void invalidateDisplay() = 0;
  virtual void deviceRequest(std::string_view, std::string_view, FormView, JsonWriter &out,
                             int &status, const char *&type, bool = false) {
    status = 404; type = "application/json"; out << "{\"error\":\"not_found\"}";
  }
  virtual void writeWebDiagnostics(JsonWriter &out) { out << "\"livePort\":0"; }
};
} // namespace forkrot
