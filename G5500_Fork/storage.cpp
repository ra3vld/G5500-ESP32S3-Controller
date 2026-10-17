// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "storage.hpp"
#include "board.hpp"
#include <time.h>
#include <cstdlib>
#include <cstring>
namespace forkrot {
using namespace board;
const char *Storage::calNamespace(const Runtime &runtime) {
  return runtime.controller.model == forkrot::Model::G800DXA ? "g800cal" : "g5500";
}

void Storage::loadPrefs(Runtime &runtime, Calibration &calibration, WifiCredentials &credentials) {
  auto &calibrationState = calibration.state();
  wifiCredentialsReadOk_ = prefs_.begin("g5500", true);
  credentials.wifiSsid = wifiCredentialsReadOk_ ? prefs_.getString("ssid", "") : String("");
  credentials.wifiPass = wifiCredentialsReadOk_ ? prefs_.getString("pass", "") : String("");
  credentials.apPass = wifiCredentialsReadOk_ ? prefs_.getString("appass", AP_PASS) : String(AP_PASS);

  // v1.6.23 format 4: independent P36/P45 + explicit two-point endpoint flags.
  prefs_.end();
  if (runtime.hud.isHud())
    return;
  prefs_.begin(calNamespace(runtime), true);
  uint8_t calFmt = prefs_.getUChar("calfmt", 0);

  calibrationState.cal.az36_0 = 0;
  calibrationState.cal.az36Max = 3100;
  calibrationState.cal.az45_0 = 0;
  calibrationState.cal.az45Max = 3100;
  calibrationState.cal.el0 = 0;
  calibrationState.cal.elMax = 3100;

  calibrationState.az36OSet = calibrationState.az36FSet = calibrationState.az45OSet =
      calibrationState.az45FSet = false;
  calibrationState.elOSet = calibrationState.elFSet = false;
  calibrationState.az36LinearValid = calibrationState.az45LinearValid =
      calibrationState.elLinearValid = false;

  for (uint8_t i = 0; i < AZ36_CAL_N; i++) {
    calibrationState.az36CalMv[i] = -1;
  }
  for (uint8_t i = 0; i < AZ45_CAL_N; i++) {
    calibrationState.az45CalMv[i] = -1;
  }
  for (uint8_t i = 0; i < EL_CAL_N; i++) {
    calibrationState.elCalMv[i] = -1;
  }

  if (calFmt == 4) {
    calibrationState.cal.az36_0 = prefs_.getInt("a36z", 0);
    calibrationState.cal.az36Max = prefs_.getInt("a36f", 3100);
    calibrationState.cal.az45_0 = prefs_.getInt("a45z", 0);
    calibrationState.cal.az45Max = prefs_.getInt("a45f", 3100);
    calibrationState.cal.el0 = prefs_.getInt("el0mv", 0);
    calibrationState.cal.elMax = prefs_.getInt("elmaxmv", 3100);

    calibrationState.az36OSet = prefs_.getBool("a36os", false);
    calibrationState.az36FSet = prefs_.getBool("a36fs", false);
    calibrationState.az45OSet = prefs_.getBool("a45os", false);
    calibrationState.az45FSet = prefs_.getBool("a45fs", false);
    calibrationState.elOSet = prefs_.getBool("elos", false);
    calibrationState.elFSet = prefs_.getBool("elfs", false);

    calibrationState.az36CalMv[0] = prefs_.getInt("a36m0", -1);
    calibrationState.az36CalMv[1] = prefs_.getInt("a36m90", -1);
    calibrationState.az36CalMv[2] = prefs_.getInt("a36m180", -1);
    calibrationState.az36CalMv[3] = prefs_.getInt("a36m270", -1);
    calibrationState.az36CalMv[4] = prefs_.getInt("a36m360", -1);

    calibrationState.az45CalMv[0] = prefs_.getInt("a45m0", -1);
    calibrationState.az45CalMv[1] = prefs_.getInt("a45m90", -1);
    calibrationState.az45CalMv[2] = prefs_.getInt("a45m180", -1);
    calibrationState.az45CalMv[3] = prefs_.getInt("a45m270", -1);
    calibrationState.az45CalMv[4] = prefs_.getInt("a45m360", -1);
    calibrationState.az45CalMv[5] = prefs_.getInt("a45m450", -1);

    calibrationState.elCalMv[0] = prefs_.getInt("elm0", -1);
    calibrationState.elCalMv[1] = prefs_.getInt("elm45", -1);
    calibrationState.elCalMv[2] = prefs_.getInt("elm90", -1);
    calibrationState.elCalMv[3] = prefs_.getInt("elm135", -1);
    calibrationState.elCalMv[4] = prefs_.getInt("elm180", -1);

  } else if (calFmt == 3) {
    // Migration from 1.5.58. Its per-endpoint state was unreliable.
    // Preserve measured 2-point values but force endpoints to be captured again.
    calibrationState.cal.az36_0 = prefs_.getInt("a36z", 0);
    calibrationState.cal.az36Max = prefs_.getInt("a36f", 3100);
    calibrationState.cal.az45_0 = prefs_.getInt("a45z", 0);
    calibrationState.cal.az45Max = prefs_.getInt("a45f", 3100);
    calibrationState.cal.el0 = prefs_.getInt("el0mv", 0);
    calibrationState.cal.elMax = prefs_.getInt("elmaxmv", 3100);

    // AZ MULTI was independent in 1.5.58 and is safe to preserve.
    calibrationState.az36CalMv[0] = prefs_.getInt("a36m0", -1);
    calibrationState.az36CalMv[1] = prefs_.getInt("a36m90", -1);
    calibrationState.az36CalMv[2] = prefs_.getInt("a36m180", -1);
    calibrationState.az36CalMv[3] = prefs_.getInt("a36m270", -1);
    calibrationState.az36CalMv[4] = prefs_.getInt("a36m360", -1);

    calibrationState.az45CalMv[0] = prefs_.getInt("a45m0", -1);
    calibrationState.az45CalMv[1] = prefs_.getInt("a45m90", -1);
    calibrationState.az45CalMv[2] = prefs_.getInt("a45m180", -1);
    calibrationState.az45CalMv[3] = prefs_.getInt("a45m270", -1);
    calibrationState.az45CalMv[4] = prefs_.getInt("a45m360", -1);
    calibrationState.az45CalMv[5] = prefs_.getInt("a45m450", -1);

    // EL MULTI endpoints in 1.5.58 could be inherited from 2-point.
    // Keep only the unambiguous middle points; recapture 0° and 180°.
    calibrationState.elCalMv[0] = -1;
    calibrationState.elCalMv[1] = prefs_.getInt("elm45", -1);
    calibrationState.elCalMv[2] = prefs_.getInt("elm90", -1);
    calibrationState.elCalMv[3] = prefs_.getInt("elm135", -1);
    calibrationState.elCalMv[4] = -1;

  } else if (calFmt == 2) {
    // Migration from the earlier single-AZ format with explicit flags when available.
    int oldAz0 = prefs_.getInt("az0mv", 0);
    int oldAzMax = prefs_.getInt("azmaxmv", 3100);
    float oldMaxDeg = prefs_.getFloat("az2maxdeg", 450.0f);
    bool oldO = prefs_.getBool("az2oset", false);
    bool oldF = prefs_.getBool("az2fset", false);

    if (oldMaxDeg < 400.0f) {
      calibrationState.cal.az36_0 = oldAz0;
      calibrationState.cal.az36Max = oldAzMax;
      calibrationState.az36OSet = oldO;
      calibrationState.az36FSet = oldF;
    } else {
      calibrationState.cal.az45_0 = oldAz0;
      calibrationState.cal.az45Max = oldAzMax;
      calibrationState.az45OSet = oldO;
      calibrationState.az45FSet = oldF;
    }

    calibrationState.cal.el0 = prefs_.getInt("el0mv", 0);
    calibrationState.cal.elMax = prefs_.getInt("elmaxmv", 3100);
    calibrationState.elOSet = prefs_.getBool("el2oset", false);
    calibrationState.elFSet = prefs_.getBool("el2fset", false);

    int oldAz[6];
    oldAz[0] = prefs_.getInt("azm0", -1);
    oldAz[1] = prefs_.getInt("azm90", -1);
    oldAz[2] = prefs_.getInt("azm180", -1);
    oldAz[3] = prefs_.getInt("azm270", -1);
    oldAz[4] = prefs_.getInt("azm360", -1);
    oldAz[5] = prefs_.getInt("azm450", -1);
    for (uint8_t i = 0; i < AZ36_CAL_N; i++) {
      calibrationState.az36CalMv[i] = oldAz[i];
    }
    for (uint8_t i = 0; i < AZ45_CAL_N; i++) {
      calibrationState.az45CalMv[i] = oldAz[i];
    }

    calibrationState.elCalMv[0] = prefs_.getInt("elm0", -1);
    calibrationState.elCalMv[1] = prefs_.getInt("elm45", -1);
    calibrationState.elCalMv[2] = prefs_.getInt("elm90", -1);
    calibrationState.elCalMv[3] = prefs_.getInt("elm135", -1);
    calibrationState.elCalMv[4] = prefs_.getInt("elm180", -1);
  }

  uint8_t savedMode = prefs_.getUChar("calmode", 255);
  uint8_t savedAzMode = prefs_.getUChar("azmode", 45);
  prefs_.end();

  calibrationState.az36LinearValid =
      (calibrationState.az36OSet && calibrationState.az36FSet &&
       abs(calibrationState.cal.az36Max - calibrationState.cal.az36_0) > 100);
  calibrationState.az45LinearValid =
      (calibrationState.az45OSet && calibrationState.az45FSet &&
       abs(calibrationState.cal.az45Max - calibrationState.cal.az45_0) > 100);
  calibrationState.elLinearValid =
      (calibrationState.elOSet && calibrationState.elFSet &&
       abs(calibrationState.cal.elMax - calibrationState.cal.el0) > 100);

  calibration.refreshMultiCalValidity();
  calibrationState.gsAzMode = (savedAzMode == 36) ? 36 : 45;

  if (savedMode == CAL_GS232_2POINT || savedMode == CAL_MULTI_POINT) {
    calibrationState.calibrationMode = (CalibrationMode)savedMode;
  } else {
    bool anyMulti = (calibrationState.az36MultiValid || calibrationState.az45MultiValid) &&
                    calibrationState.elMultiValid;
    calibrationState.calibrationMode = anyMulti ? CAL_MULTI_POINT : CAL_GS232_2POINT;
  }

  calibrationState.cal.valid = calibration.selectedCalibrationValid();
  if (calFmt != 4) {
    saveCal(runtime, calibration);
  }
}

void Storage::saveCal(const Runtime &runtime, Calibration &calibration) {
  if (runtime.hud.isHud())
    return;
  auto &calibrationState = calibration.state();
  calibrationState.az36LinearValid =
      (calibrationState.az36OSet && calibrationState.az36FSet &&
       abs(calibrationState.cal.az36Max - calibrationState.cal.az36_0) > 100);
  calibrationState.az45LinearValid =
      (calibrationState.az45OSet && calibrationState.az45FSet &&
       abs(calibrationState.cal.az45Max - calibrationState.cal.az45_0) > 100);
  calibrationState.elLinearValid =
      (calibrationState.elOSet && calibrationState.elFSet &&
       abs(calibrationState.cal.elMax - calibrationState.cal.el0) > 100);

  calibration.refreshMultiCalValidity();
  calibrationState.cal.valid = calibration.selectedCalibrationValid();

  prefs_.begin(calNamespace(runtime), false);
  prefs_.putUChar("calfmt", 4);
  prefs_.putUChar("calmode", (uint8_t)calibrationState.calibrationMode);
  prefs_.putUChar("azmode", calibrationState.gsAzMode);

  prefs_.putInt("a36z", calibrationState.cal.az36_0);
  prefs_.putInt("a36f", calibrationState.cal.az36Max);
  prefs_.putInt("a45z", calibrationState.cal.az45_0);
  prefs_.putInt("a45f", calibrationState.cal.az45Max);
  prefs_.putBool("a36os", calibrationState.az36OSet);
  prefs_.putBool("a36fs", calibrationState.az36FSet);
  prefs_.putBool("a45os", calibrationState.az45OSet);
  prefs_.putBool("a45fs", calibrationState.az45FSet);

  prefs_.putInt("el0mv", calibrationState.cal.el0);
  prefs_.putInt("elmaxmv", calibrationState.cal.elMax);
  prefs_.putBool("elos", calibrationState.elOSet);
  prefs_.putBool("elfs", calibrationState.elFSet);

  prefs_.putInt("a36m0", calibrationState.az36CalMv[0]);
  prefs_.putInt("a36m90", calibrationState.az36CalMv[1]);
  prefs_.putInt("a36m180", calibrationState.az36CalMv[2]);
  prefs_.putInt("a36m270", calibrationState.az36CalMv[3]);
  prefs_.putInt("a36m360", calibrationState.az36CalMv[4]);

  prefs_.putInt("a45m0", calibrationState.az45CalMv[0]);
  prefs_.putInt("a45m90", calibrationState.az45CalMv[1]);
  prefs_.putInt("a45m180", calibrationState.az45CalMv[2]);
  prefs_.putInt("a45m270", calibrationState.az45CalMv[3]);
  prefs_.putInt("a45m360", calibrationState.az45CalMv[4]);
  prefs_.putInt("a45m450", calibrationState.az45CalMv[5]);

  prefs_.putInt("elm0", calibrationState.elCalMv[0]);
  prefs_.putInt("elm45", calibrationState.elCalMv[1]);
  prefs_.putInt("elm90", calibrationState.elCalMv[2]);
  prefs_.putInt("elm135", calibrationState.elCalMv[3]);
  prefs_.putInt("elm180", calibrationState.elCalMv[4]);
  prefs_.end();
}

WifiSaveResult Storage::saveWifi(const String &s, const String &p) {
  if (!s.length() || s.length() > 32) return WifiSaveResult::InvalidSsid;
  if (p.length() && (p.length() < 8 || p.length() > 64)) return WifiSaveResult::InvalidPassword;
  if (p.length() == 64) {
    for (size_t i = 0; i < p.length(); ++i) {
      char c = p.c_str()[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        return WifiSaveResult::InvalidPassword;
    }
  }
  if (!prefs_.begin("g5500", false)) return WifiSaveResult::OpenFailed;
  const size_t ssidWritten = prefs_.putString("ssid", s);
  const size_t passwordWritten = prefs_.putString("pass", p);
  prefs_.end();
  if (ssidWritten != s.length() || passwordWritten != p.length()) return WifiSaveResult::WriteFailed;

  // Reopen NVS rather than trusting the request or an in-memory copy. An empty
  // password returns zero even on success, so read back both keys with a sentinel.
  if (!prefs_.begin("g5500", true)) return WifiSaveResult::VerifyFailed;
  const bool verified = prefs_.getString("ssid", "\x01") == s &&
                        prefs_.getString("pass", "\x01") == p;
  prefs_.end();
  return verified ? WifiSaveResult::Ok : WifiSaveResult::VerifyFailed;
}

void Storage::saveApPassword(const String &p) {
  prefs_.begin("g5500", false);
  prefs_.putString("appass", p);
  prefs_.end();
}

void Storage::loadTouchCalibration(TouchCalibration &touchCalibration) {
  prefs_.begin("g5500", true);
  touchCalibration.touchCalValid = prefs_.getBool("tcalok", false);
  touchCalibration.touchSwapXY = prefs_.getBool("tswap", true);
  touchCalibration.touchX0Raw = prefs_.getInt("tx0", TOUCH_RAW_Y_MIN);
  touchCalibration.touchX1Raw = prefs_.getInt("tx1", TOUCH_RAW_Y_MAX);
  touchCalibration.touchY0Raw = prefs_.getInt("ty0", TOUCH_RAW_X_MAX);
  touchCalibration.touchY1Raw = prefs_.getInt("ty1", TOUCH_RAW_X_MIN);
  prefs_.end();
  if (abs(touchCalibration.touchX1Raw - touchCalibration.touchX0Raw) < 500 ||
      abs(touchCalibration.touchY1Raw - touchCalibration.touchY0Raw) < 500) {
    touchCalibration.touchCalValid = false;
  }
}

void Storage::saveTouchCalibration(const TouchCalibration &touchCalibration) {
  prefs_.begin("g5500", false);
  prefs_.putBool("tcalok", touchCalibration.touchCalValid);
  prefs_.putBool("tswap", touchCalibration.touchSwapXY);
  prefs_.putInt("tx0", touchCalibration.touchX0Raw);
  prefs_.putInt("tx1", touchCalibration.touchX1Raw);
  prefs_.putInt("ty0", touchCalibration.touchY0Raw);
  prefs_.putInt("ty1", touchCalibration.touchY1Raw);
  prefs_.end();
}

void Storage::loadForkSettings(Runtime &runtime) {
  prefs_.begin("forkcfg", true);
  runtime.controller.model =
      prefs_.getUChar("model", 0) == 1 ? forkrot::Model::G800DXA : forkrot::Model::G5500;
  runtime.controller.restoreMode(prefs_.getUChar("mode", 0) == 1 ? forkrot::Mode::Auto
                                                                 : forkrot::Mode::Manual);
#if DRIVE_INTERFACE == DRIVE_YAESU
  runtime.controller.speedEnabled = prefs_.getBool("speedon", false);
  runtime.controller.speedPercent = constrain(prefs_.getUInt("speed", 50), 25u, 100u);
  runtime.controller.startSpeedPercent = constrain(prefs_.getUInt("startspd", 25), 25u, 100u);
  runtime.controller.approachSpeedPercent = constrain(prefs_.getUInt("approachspd", 25), 25u, 100u);
  runtime.controller.accelerationMs = constrain(prefs_.getUInt("accelms", 2000), 100u, 10000u);
  runtime.controller.decelerationMs = constrain(prefs_.getUInt("decelms", 1500), 100u, 10000u);
  runtime.controller.slowdownDegrees = constrain(prefs_.getUInt("slowdeg", 10), 1u, 90u);
#endif
  const String savedName = prefs_.getString("name", "G5500");
  runtime.protocol.rotorName = (runtime.hud.isHud() ? prefs_.getString("hudname", savedName) : savedName).c_str();
  runtime.loggerSettings.n1mmPort = prefs_.getUShort("nport", 12040);
  runtime.loggerSettings.pstPort = prefs_.getUShort("pport", 1234);
  runtime.loggerSettings.reportPort = prefs_.getUShort("rport", 13010);
  runtime.loggerSettings.reportHost = prefs_.getString("rhost", "").c_str();
#if SELECTED_BOARD == BOARD_CROWPANEL_21
  runtime.loggerSettings.encoderInverted = prefs_.getBool("encinv", true);
#endif
  prefs_.end();
#if DRIVE_INTERFACE != DRIVE_YAESU
  prefs_.begin("forkpwm", true);
  for (unsigned i=0; i<2; ++i) {
    auto &speed = runtime.controller.speeds[i];
    const String prefix = i ? "el" : "az";
    speed.speedPercent = constrain(prefs_.getUInt((prefix + "work").c_str(), 100), 1u, 100u);
    speed.startSpeedPercent = constrain(prefs_.getUInt((prefix + "start").c_str(), 18), 1u, 100u);
    speed.approachSpeedPercent = constrain(prefs_.getUInt((prefix + "approach").c_str(), 18), 1u, 100u);
    speed.accelerationMs = constrain(prefs_.getUInt((prefix + "accel").c_str(), 1800), 100u, 10000u);
    speed.decelerationMs = constrain(prefs_.getUInt((prefix + "decel").c_str(), 1200), 100u, 10000u);
    speed.slowdownDegrees = constrain(prefs_.getUInt((prefix + "slow").c_str(), i ? 2 : 3), 1u, 90u);
  }
  prefs_.end();
#endif
}

void Storage::saveForkSettings(const Runtime &runtime) {
  prefs_.begin("forkcfg", false);
  if (!runtime.hud.isHud()) {
    prefs_.putUChar("model", runtime.controller.model == forkrot::Model::G800DXA ? 1 : 0);
    prefs_.putUChar("mode", runtime.controller.mode == forkrot::Mode::Auto ? 1 : 0);
#if DRIVE_INTERFACE == DRIVE_YAESU
  prefs_.putBool("speedon", runtime.controller.speedEnabled);
  prefs_.putUInt("speed", runtime.controller.speedPercent);
  prefs_.putUInt("startspd", runtime.controller.startSpeedPercent);
  prefs_.putUInt("approachspd", runtime.controller.approachSpeedPercent);
  prefs_.putUInt("accelms", runtime.controller.accelerationMs);
  prefs_.putUInt("decelms", runtime.controller.decelerationMs);
  prefs_.putUInt("slowdeg", runtime.controller.slowdownDegrees);
#endif
  prefs_.putString("name", runtime.protocol.rotorName.c_str());
  } else
    prefs_.putString("hudname", runtime.protocol.rotorName.c_str());
  prefs_.putUShort("nport", runtime.loggerSettings.n1mmPort);
  prefs_.putUShort("pport", runtime.loggerSettings.pstPort);
  prefs_.putUShort("rport", runtime.loggerSettings.reportPort);
  prefs_.putString("rhost", runtime.loggerSettings.reportHost.c_str());
#if SELECTED_BOARD == BOARD_CROWPANEL_21
  prefs_.putBool("encinv", runtime.loggerSettings.encoderInverted);
#endif
  prefs_.end();
#if DRIVE_INTERFACE != DRIVE_YAESU
  if (runtime.hud.isHud())
    return;
  prefs_.begin("forkpwm", false);
  for (unsigned i=0; i<2; ++i) {
    const auto &speed = runtime.controller.speeds[i];
    const String prefix = i ? "el" : "az";
    prefs_.putUInt((prefix + "work").c_str(), speed.speedPercent);
    prefs_.putUInt((prefix + "start").c_str(), speed.startSpeedPercent);
    prefs_.putUInt((prefix + "approach").c_str(), speed.approachSpeedPercent);
    prefs_.putUInt((prefix + "accel").c_str(), speed.accelerationMs);
    prefs_.putUInt((prefix + "decel").c_str(), speed.decelerationMs);
    prefs_.putUInt((prefix + "slow").c_str(), speed.slowdownDegrees);
  }
  prefs_.end();
#endif
}

const char *Storage::presetNamespace(const Runtime &runtime) {
  return runtime.controller.model == forkrot::Model::G800DXA ? "fork800" : "fork5500";
}

void Storage::loadProfilePresets(Runtime &runtime) {
  if (runtime.hud.isHud())
    return;
  prefs_.begin(presetNamespace(runtime), true);
  for (unsigned i = 0; i < 4; ++i) {
    String base = "p" + String(i);
    runtime.controller.presets[i].label =
        prefs_.getString((base + "n").c_str(), String(i * 90) + "°").c_str();
    runtime.controller.presets[i].target = {prefs_.getFloat((base + "a").c_str(), i * 90),
                                            prefs_.getFloat((base + "e").c_str(), 0), true,
                                            runtime.controller.model == forkrot::Model::G5500};
  }
  prefs_.end();
}

void Storage::saveProfilePresets(const Runtime &runtime) {
  if (runtime.hud.isHud())
    return;
  prefs_.begin(presetNamespace(runtime), false);
  for (unsigned i = 0; i < 4; ++i) {
    String base = "p" + String(i);
    prefs_.putString((base + "n").c_str(), runtime.controller.presets[i].label.c_str());
    prefs_.putFloat((base + "a").c_str(), runtime.controller.presets[i].target.az);
    prefs_.putFloat((base + "e").c_str(), runtime.controller.presets[i].target.el);
  }
  prefs_.end();
}

void Storage::addResetHistoryEntry(int bootResetReason, const String &bootResetReasonText) {
  prefs_.begin("g5500rst", false);

  uint8_t head = prefs_.getUChar("head", 0);
  uint8_t count = prefs_.getUChar("count", 0);
  if (head >= RESET_HISTORY_MAX) {
    head = 0;
  }
  if (count > RESET_HISTORY_MAX) {
    count = RESET_HISTORY_MAX;
  }

  time_t now = time(nullptr);
  uint64_t epoch = (now > 1700000000) ? (uint64_t)now : 0ULL;

  char kt[8], kr[8], kc[8];
  snprintf(kt, sizeof(kt), "t%02u", head);
  snprintf(kr, sizeof(kr), "r%02u", head);
  snprintf(kc, sizeof(kc), "c%02u", head);

  prefs_.putULong64(kt, epoch);
  prefs_.putString(kr, bootResetReasonText);
  prefs_.putInt(kc, (int)bootResetReason);

  head = (head + 1) % RESET_HISTORY_MAX;
  if (count < RESET_HISTORY_MAX) {
    count++;
  }

  prefs_.putUChar("head", head);
  prefs_.putUChar("count", count);
  prefs_.end();
}

bool Storage::writeResetHistoryJson(JsonWriter &j) {
  prefs_.begin("g5500rst", true);
  uint8_t head = prefs_.getUChar("head", 0);
  uint8_t count = prefs_.getUChar("count", 0);
  if (head >= RESET_HISTORY_MAX) {
    head = 0;
  }
  if (count > RESET_HISTORY_MAX) {
    count = RESET_HISTORY_MAX;
  }

  j << "{\"count\":" << count << ",\"entries\":[";
  for (uint8_t n = 0; n < count; n++) {
    int idx = (int)head - 1 - (int)n;
    while (idx < 0) {
      idx += RESET_HISTORY_MAX;
    }

    char kt[8], kr[8], kc[8];
    snprintf(kt, sizeof(kt), "t%02d", idx);
    snprintf(kr, sizeof(kr), "r%02d", idx);
    snprintf(kc, sizeof(kc), "c%02d", idx);

    uint64_t epoch = prefs_.getULong64(kt, 0ULL);
    String reason = prefs_.getString(kr, "UNKNOWN");
    int code = prefs_.getInt(kc, 0);

    if (n) {
      j << ',';
    }
    j << "{\"epoch\":" << epoch << ",\"reason\":" << jsonQuoted(reason.c_str()) << ",\"code\":" << code << '}';
  }
  j << "]}";
  prefs_.end();
  return j.good();
}

void Storage::clearResetHistory() {
  prefs_.begin("g5500rst", false);
  prefs_.clear();
  prefs_.end();
}
void Storage::saveCalibrationMode(Model model, CalibrationMode mode) {
  prefs_.begin(model == Model::G800DXA ? "g800cal" : "g5500", false);
  prefs_.putUChar("calmode", (uint8_t)mode);
  prefs_.end();
}
void Storage::saveAzMode(Model model, uint8_t mode) {
  prefs_.begin(model == Model::G800DXA ? "g800cal" : "g5500", false);
  prefs_.putUChar("azmode", mode);
  prefs_.end();
}
void Storage::loadRssiYear(RssiHistory &history) {
  prefs_.begin("g5500rssi", true);
  size_t n = prefs_.getBytesLength("year");
  if (n == sizeof(history.rssiYear)) {
    prefs_.getBytes("year", history.rssiYear, sizeof(history.rssiYear));
  }
  history.rssiYearPos = prefs_.getUShort("ypos", 0);
  history.rssiYearCount = prefs_.getUShort("ycnt", 0);
  prefs_.end();
}
void Storage::persistRssiYear(const RssiHistory &history) {
  prefs_.begin("g5500rssi", false);
  prefs_.putBytes("year", history.rssiYear, sizeof(history.rssiYear));
  prefs_.putUShort("ypos", history.rssiYearPos);
  prefs_.putUShort("ycnt", history.rssiYearCount);
  prefs_.end();
}

} // namespace forkrot

namespace forkrot {
namespace {
struct HudDiskConfig {
  uint32_t version = 1;
  uint8_t role = 0, wifi = 0, uart = 0, reserved = 0;
  uint32_t port = 4534;
  uint32_t selectedLo = 0, selectedHi = 0;
  uint32_t crc = 0;
};
struct HudDiskPeer {
  uint32_t idLo = 0, idHi = 0, address = 0;
  uint16_t port = 4534;
  uint8_t uart = 0, reserved = 0;
  char label[49]{};
  uint8_t padding[3]{};
};
static_assert(sizeof(HudDiskPeer) == 68);
} // namespace
bool Storage::saveHudConfig(const HudConfig &c) {
  HudDiskConfig d;
  d.role = uint8_t(c.role);
  d.wifi = uint8_t(c.wifi);
  d.uart = c.uart;
  d.port = c.port;
  d.selectedLo = uint32_t(c.selected);
  d.selectedHi = uint32_t(c.selected >> 32);
  d.crc = hudCrc(reinterpret_cast<const uint8_t *>(&d), sizeof d - 4);
  if (!prefs_.begin("hudcfg", false)) {
    prefs_.end();
    return false;
  }
  prefs_.putBytes("config", &d, sizeof d);
  prefs_.end();
  HudDiskConfig check;
  const bool opened = prefs_.begin("hudcfg", true);
  const bool ok = opened && prefs_.getBytes("config", &check, sizeof check) == sizeof check &&
                  std::memcmp(&d, &check, sizeof d) == 0;
  prefs_.end();
  return ok;
}
bool Storage::saveHudPeers(const Hud &hud) {
  const size_t count = hud.count();
  if (count > (SIZE_MAX - 8) / sizeof(HudDiskPeer))
    return false;
  const size_t size = 8 + count * sizeof(HudDiskPeer);
  auto data = std::unique_ptr<uint8_t, decltype(&std::free)>(static_cast<uint8_t *>(std::calloc(1, size)),
                                                             &std::free);
  if (!data)
    return false;
  const uint32_t version = 1;
  std::memcpy(data.get(), &version, 4);
  size_t offset = 4;
  for (auto *p = hud.peers(); p; p = p->next.get())
    if (p->saved) {
      HudDiskPeer d;
      d.idLo = uint32_t(p->id);
      d.idHi = uint32_t(p->id >> 32);
      d.address = p->endpoint.address;
      d.port = p->endpoint.port;
      d.uart = p->endpoint.uart;
      std::memcpy(d.label, p->label, sizeof d.label);
      std::memcpy(data.get() + offset, &d, sizeof d);
      offset += sizeof d;
    }
  const uint32_t crc = hudCrc(data.get(), size - 4);
  std::memcpy(data.get() + size - 4, &crc, 4);
  if (!prefs_.begin("hudcfg", false)) {
    prefs_.end();
    return false;
  }
  prefs_.putBytes("peers", data.get(), size);
  prefs_.end();
  if (!prefs_.begin("hudcfg", true)) {
    prefs_.end();
    return false;
  }
  const bool ok = prefs_.getBytesLength("peers") == size &&
                  prefs_.getBytes("peers", data.get(), size) == size && hudCrc(data.get(), size - 4) == crc;
  prefs_.end();
  return ok;
}
void Storage::loadHud(Runtime &runtime) {
  auto &hud = runtime.hud;
  const bool opened = prefs_.begin("hudcfg", true);
  if (opened && prefs_.getBytesLength("config")) {
    HudDiskConfig d;
    if (prefs_.getBytesLength("config") != sizeof d || prefs_.getBytes("config", &d, sizeof d) != sizeof d ||
        d.version != 1 || d.role > 1 || d.wifi > 2 || d.uart > 1 || !d.port || d.port > 65535 ||
        hudCrc(reinterpret_cast<const uint8_t *>(&d), sizeof d - 4) != d.crc) {
      hud.config.role = DeviceRole::Hud;
      hud.catalogError = true;
    } else {
      hud.config.role = DeviceRole(d.role);
      hud.config.wifi = HudWifi(d.wifi);
      hud.config.uart = d.uart;
      hud.config.port = uint16_t(d.port);
      hud.config.selected = uint64_t(d.selectedLo) | (uint64_t(d.selectedHi) << 32);
    }
  }
  const size_t size = opened && hud.isHud() ? prefs_.getBytesLength("peers") : 0;
  if (size >= 8 && (size - 8) % sizeof(HudDiskPeer) == 0) {
    auto data =
        std::unique_ptr<uint8_t, decltype(&std::free)>(static_cast<uint8_t *>(std::malloc(size)), &std::free);
    if (!data || prefs_.getBytes("peers", data.get(), size) != size)
      hud.catalogError = true;
    else {
      uint32_t version, crc;
      std::memcpy(&version, data.get(), 4);
      std::memcpy(&crc, data.get() + size - 4, 4);
      if (version != 1 || crc != hudCrc(data.get(), size - 4))
        hud.catalogError = true;
      else
        for (size_t at = 4; at < size - 4; at += sizeof(HudDiskPeer)) {
          HudDiskPeer d;
          std::memcpy(&d, data.get() + at, sizeof d);
          const auto id = uint64_t(d.idLo) | (uint64_t(d.idHi) << 32);
          if (d.uart > 1 || d.label[48] || !hud.add(id, {d.address, d.port, bool(d.uart)}, d.label))
            hud.catalogError = true;
        }
    }
  } else if (size)
    hud.catalogError = true;
  prefs_.end();
  // Even an invalid saved role fails closed before any motor/ADC setup.
  runtime.controller.remote = hud.isHud() ? &hud : nullptr;
}
} // namespace forkrot
