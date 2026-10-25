// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "json_writer.hpp"
#include <Arduino.h>
#include <Preferences.h>
#include "runtime.hpp"
#include "calibration.hpp"
#include "network_state.hpp"
#include "display_state.hpp"
#include "diagnostics_state.hpp"
namespace forkrot {
enum class WifiSaveResult { Ok, InvalidSsid, InvalidPassword, OpenFailed, WriteFailed, VerifyFailed };
class Storage {
public:
  bool loadSetup(Runtime &runtime);
  bool saveSetup(const SetupState &state);
  bool loadNetwork(NetworkConfig &config);
  bool saveNetwork(const NetworkConfig &config);
  bool loadModbus(Runtime &runtime);
  bool saveModbus(const ModbusConfig &config);
  void loadHardware(Runtime &runtime, bool forceRecovery = false);
  bool stageHardware(Runtime &runtime, const HardwareConfig &config);
  bool confirmHardware(Runtime &runtime);
  bool enterRecovery(Runtime &runtime, RecoveryReason reason);
  bool finishHardwareBoot(Runtime &runtime, uint32_t uptime = 0);
  bool probeHardwareStorage(Runtime &runtime);
  void loadHud(Runtime &runtime, bool peers = true);
  bool saveHudConfig(const HudConfig &config);
  bool saveHudPeers(const Hud &hud);
  void loadPrefs(Runtime &runtime, Calibration &calibration, WifiCredentials &credentials, bool wifiOnly = false);
  bool saveCal(const Runtime &runtime, Calibration &calibration);
  WifiSaveResult saveWifi(const String &s, const String &p);
  bool wifiCredentialsReadOk() const { return wifiCredentialsReadOk_; }
  bool saveApPassword(const String &p);
  void loadTouchCalibration(TouchCalibration &touchCalibration);
  void saveTouchCalibration(const TouchCalibration &touchCalibration);
  void loadForkSettings(Runtime &runtime);
  void loadMotionSettings(Runtime &runtime);
  bool saveForkSettings(const Runtime &runtime);
  bool serviceRuntimeSpeeds(Runtime &runtime, uint32_t now, bool force = false);
  void loadProfilePresets(Runtime &runtime);
  void saveProfilePresets(const Runtime &runtime);
  bool addResetHistoryEntry(int bootResetReason, const String &bootResetReasonText);
  bool updateResetHistoryTime();
  bool writeResetHistoryJson(JsonWriter &j);
  void clearResetHistory();
  void saveCalibrationMode(Model model, CalibrationMode mode);
  void saveAzMode(Model model, uint8_t mode);
  void loadRssiYear(RssiHistory &history);
  void persistRssiYear(const RssiHistory &history);

private:
  bool writeHardware(const HardwareRecord &record);
  bool writeRecovery(const RecoveryHistory &history);
  const char *calNamespace(const Runtime &runtime);
  const char *presetNamespace(const Runtime &runtime);
  static constexpr uint8_t RESET_HISTORY_MAX = 20;
  int8_t resetHistorySlot_ = -1;
  Preferences prefs_;
  bool wifiCredentialsReadOk_ = false;
};
} // namespace forkrot
