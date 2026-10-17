// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#include "application.hpp"
namespace forkrot {
using namespace board;
void Application::updatePosition() {
  if (runtime_.hud.isHud()) {
    auto &c = runtime_.controller;
    runtime_.currentAz = c.axes[0].current;
    runtime_.currentEl = c.axes[1].current;
    runtime_.targetAz = c.axes[0].target;
    runtime_.targetEl = c.axes[1].target;
    runtime_.targetValid = c.targetKnown;
    runtime_.azTrack = c.axes[0].tracking;
    runtime_.elTrack = c.axes[1].tracking;
    return;
  }
#if DRIVE_SIMULATION
  runtime_.controller.tick(millis());
  runtime_.currentAz = runtime_.controller.axes[0].current;
  runtime_.currentEl = runtime_.controller.model == Model::G5500 ? runtime_.controller.axes[1].current : 0.f;
  runtime_.controller.feedback(runtime_.currentAz, runtime_.currentEl, true, true);
#else
  auto &calibrationState = calibration_.state();
  // factory-calibrated ESP32-S3 millivolts -> selected mechanical calibration -> angle.
  // Each axis uses the calibration of its active range profile.
  int az = hardware_.filteredMv(0),
      el = runtime_.controller.model == forkrot::Model::G5500 ? hardware_.filteredMv(1) : 0;
  runtime_.currentAz = calibration_.mapAz(az);
  runtime_.currentEl = calibration_.mapEl(el);
  calibrationState.cal.valid = calibration_.selectedCalibrationValid();
  if (runtime_.controller.model == forkrot::Model::G800DXA) {
    runtime_.currentEl = 0;
  }
  runtime_.controller.feedback(
      runtime_.currentAz, runtime_.currentEl,
#if SELECTED_BOARD == BOARD_CROWPANEL_21
      false, false);
#else
      calibrationState.calibrationMode == CAL_MULTI_POINT ? calibration_.activeAzMultiValid()
                                                          : calibration_.activeAzLinearValid(),
      calibrationState.calibrationMode == CAL_MULTI_POINT ? calibration_.activeElMultiValid()
                                                          : calibration_.activeElLinearValid());
#endif
#endif
}

bool Application::saveCal() {
  if (runtime_.hud.isHud()) return false;
  runtime_.controller.stopNow(-1, millis());
  hardware_.applyOutputs();
  if (!storage_.saveCal(runtime_, calibration_)) {
    loadPrefs();
    runtime_.controller.fault = "calibration_save_failed";
    updatePosition();
    return false;
  }
  if (runtime_.controller.fault == "calibration_save_failed" || runtime_.controller.fault == "calibration_storage_invalid") runtime_.controller.fault.clear();
  runtime_.controller.axes[0].accepted = runtime_.controller.axes[0].targetKnown = false;
  runtime_.controller.axes[1].accepted = runtime_.controller.axes[1].targetKnown = false;
  runtime_.controller.lastTarget = {0,0,false,false};
  runtime_.controller.targetKnown = runtime_.targetValid = false;
  updatePosition();
  invalidateDisplay();
  return true;
}
void Application::setCalibrationMode(CalibrationMode m) {
  if (runtime_.hud.isHud() || calibration_.state().calibrationMode == m) return;
  const auto old = calibration_.state();
  calibration_.state().calibrationMode = m;
  if (!saveCal()) calibration_.state() = old;
}
bool Application::configureAzRange(uint8_t profile, float start, float span) {
  if (runtime_.hud.isHud()) return false;
  Calibration next(runtime_.controller);
  next.state() = calibration_.state();
  if (!next.configureAz(profile, start, span) || next.state().storageFault) return false;
  const bool changed = !(runtime_.controller.azRange == next.activeAzRange()) || runtime_.controller.azProfile != profile;
  if (!changed) return true;
  runtime_.controller.stopNow(0, millis());
  hardware_.applyOutputs();
  if (!storage_.saveCal(runtime_, next)) {
    runtime_.controller.fault = "calibration_save_failed";
    return false;
  }
  calibration_.state() = next.state();
  if (runtime_.controller.fault == "calibration_save_failed") runtime_.controller.fault.clear();
  runtime_.controller.setRange(next.activeAzRange(), profile, millis());
  runtime_.protocol.resetSession();
  updatePosition();
  runtime_.controller.axes[0].target = runtime_.targetAz = runtime_.currentAz;
  runtime_.targetValid = runtime_.controller.targetKnown;
  invalidateDisplay();
  return true;
}
bool Application::configureElRange(uint8_t profile, float start, float span) {
  if (runtime_.hud.isHud()) return false;
  Calibration next(runtime_.controller);
  next.state() = calibration_.state();
  if (!next.configureEl(profile, start, span) || next.state().storageFault) return false;
  const bool changed = !(runtime_.controller.elRange == next.activeElRange()) || runtime_.controller.elProfile != profile;
  if (!changed) return true;
  runtime_.controller.stopNow(1, millis());
  hardware_.applyOutputs();
  if (!storage_.saveCal(runtime_, next)) {
    runtime_.controller.fault = "calibration_save_failed";
    return false;
  }
  calibration_.state() = next.state();
  if (runtime_.controller.fault == "calibration_save_failed") runtime_.controller.fault.clear();
  runtime_.controller.setElRange(next.activeElRange(), profile, millis());
  runtime_.protocol.resetSession();
  updatePosition();
  runtime_.controller.axes[1].target = runtime_.targetEl = runtime_.currentEl;
  runtime_.targetValid = runtime_.controller.targetKnown;
  invalidateDisplay();
  return true;
}
void Application::setGsAzMode(uint8_t m) {
  if (m == AZ_P36 || m == AZ_P45) configureAzRange(m, 0, m == AZ_P36 ? 360 : 450);
  else if (m == AZ_CUSTOM) {
    const auto r = calibration_.state().custom.initialized ? calibration_.state().custom.range : calibration_.activeAzRange();
    configureAzRange(m, r.start, r.span());
  }
}
bool Application::editAzPoint(const char *op, float angle) {
  if (runtime_.hud.isHud()) return false;
  const auto old = calibration_.state();
  if (!calibration_.editAzPoint(op, angle, hardware_.adcAvgMv(PIN_AZ_ADC))) return false;
  if (!saveCal()) { calibration_.state() = old; return false; }
  return true;
}
bool Application::editElPoint(const char *op, float angle) {
  if (runtime_.hud.isHud()) return false;
  const auto old = calibration_.state();
  if (!calibration_.editElPoint(op, angle, hardware_.adcAvgMv(PIN_EL_ADC))) return false;
  if (!saveCal()) { calibration_.state() = old; return false; }
  return true;
}

void Application::captureGs232Cal(const String &cmd) {
  if (runtime_.hud.isHud())
    return;
  auto &calibrationState = calibration_.state();
  if (runtime_.controller.model == forkrot::Model::G800DXA && (cmd == "O2" || cmd == "F2")) {
    return;
  }

  int az = hardware_.adcAvgMv(PIN_AZ_ADC);
  int el = hardware_.adcAvgMv(PIN_EL_ADC);

  if (cmd == "O" || cmd == "F") {
    calibration_.captureAzEndpoint(cmd == "F", az);
  } else if (cmd == "O2") {
    calibration_.captureElEndpoint(false, el);
  } else if (cmd == "F2") {
    calibration_.captureElEndpoint(true, el);
  } else {
    return;
  }

  calibrationState.az36LinearValid =
      (calibrationState.az36OSet && calibrationState.az36FSet &&
       abs(calibrationState.cal.az36Max - calibrationState.cal.az36_0) > 100);
  calibrationState.az45LinearValid =
      (calibrationState.az45OSet && calibrationState.az45FSet &&
       abs(calibrationState.cal.az45Max - calibrationState.cal.az45_0) > 100);
  calibrationState.elLinearValid =
      (calibrationState.elOSet && calibrationState.elFSet &&
       abs(calibrationState.cal.elMax - calibrationState.cal.el0) > 100);
  calibrationState.cal.valid = calibration_.selectedCalibrationValid();
  saveCal();

  diagnostics_.dbgPrint("[CAL GS232] ");
  diagnostics_.dbgPrint(cmd);
  diagnostics_.dbgPrint(" P");
  diagnostics_.dbgPrint(String(calibrationState.gsAzMode));
  diagnostics_.dbgPrint(" AZ0=");
  diagnostics_.dbgPrint(String(calibration_.activeAz2Zero()));
  diagnostics_.dbgPrint(" AZFULL=");
  diagnostics_.dbgPrint(String(calibration_.activeAz2Full()));
  diagnostics_.dbgPrint(" EL0=");
  diagnostics_.dbgPrint(String(calibrationState.cal.el0));
  diagnostics_.dbgPrint(" EL180=");
  diagnostics_.dbgPrintln(String(calibrationState.cal.elMax));
}
} // namespace forkrot
