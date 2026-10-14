// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "runtime.hpp"
#include "calibration.hpp"
#include "network_state.hpp"
#include "display_state.hpp"
#include "diagnostics_state.hpp"
namespace forkrot {
class Storage {
public:
  void loadPrefs(Runtime &runtime, Calibration &calibration, WifiCredentials &credentials);
  void saveCal(const Runtime &runtime, Calibration &calibration);
  void saveWifi(String s, String p);
  void saveApPassword(const String &p);
  void loadTouchCalibration(TouchCalibration &touchCalibration);
  void saveTouchCalibration(const TouchCalibration &touchCalibration);
  void loadForkSettings(Runtime &runtime);
  void saveForkSettings(const Runtime &runtime);
  void loadProfilePresets(Runtime &runtime);
  void saveProfilePresets(const Runtime &runtime);
  void addResetHistoryEntry(int bootResetReason, const String &bootResetReasonText);
  String resetHistoryJson();
  void clearResetHistory();
  void saveCalibrationMode(Model model, CalibrationMode mode);
  void saveAzMode(Model model, uint8_t mode);
  void loadRssiYear(RssiHistory &history);
  void persistRssiYear(const RssiHistory &history);

private:
  const char *calNamespace(const Runtime &runtime);
  const char *presetNamespace(const Runtime &runtime);
  static constexpr uint8_t RESET_HISTORY_MAX = 20;
  Preferences prefs_;
};
} // namespace forkrot
