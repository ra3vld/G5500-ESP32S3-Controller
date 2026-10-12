// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#include "application.hpp"
namespace forkrot {
using namespace board;
void Application::updatePosition() {
  auto &calibrationState = calibration_.state();
  // factory-calibrated ESP32-S3 millivolts -> selected mechanical calibration -> angle.
  // AZ table is selected by P36/P45; EL table is shared.
  int az = hardware_.adcAvgMv(PIN_AZ_ADC),
      el = runtime_.controller.model == forkrot::Model::G5500 ? hardware_.adcAvgMv(PIN_EL_ADC) : 0;
  float azMax = calibration_.activeAzMaxDeg();

  if (calibrationState.calibrationMode == CAL_MULTI_POINT) {
    if (calibrationState.gsAzMode == 36 && calibrationState.az36MultiValid) {
      runtime_.currentAz = mapPiecewise(az, calibrationState.az36CalMv, az36CalDeg, AZ36_CAL_N);
    } else if (calibrationState.gsAzMode == 45 && calibrationState.az45MultiValid) {
      runtime_.currentAz = mapPiecewise(az, calibrationState.az45CalMv, az45CalDeg, AZ45_CAL_N);
    } else {
      runtime_.currentAz = clampf(az * azMax / 3100.0f, 0, azMax);
    }

    if (calibrationState.elMultiValid) {
      runtime_.currentEl = mapPiecewise(el, calibrationState.elCalMv, elCalDeg, EL_CAL_N);
    } else {
      runtime_.currentEl = clampf(el * 180.0f / 3100.0f, 0, 180);
    }
  } else {
    if (calibration_.activeAzLinearValid()) {
      runtime_.currentAz =
          mapCal(az, calibration_.activeAz2Zero(), calibration_.activeAz2Full(), azMax);
    } else {
      runtime_.currentAz = clampf(az * azMax / 3100.0f, 0, azMax);
    }

    if (calibrationState.elLinearValid) {
      runtime_.currentEl = mapCal(el, calibrationState.cal.el0, calibrationState.cal.elMax, 180.0f);
    } else {
      runtime_.currentEl = clampf(el * 180.0f / 3100.0f, 0, 180);
    }
  }
  calibrationState.cal.valid = calibration_.selectedCalibrationValid();
  if (runtime_.controller.model == forkrot::Model::G800DXA) {
    runtime_.currentEl = 0;
  }
  runtime_.controller.feedback(
      runtime_.currentAz, runtime_.currentEl,
      calibrationState.calibrationMode == CAL_MULTI_POINT ? calibration_.activeAzMultiValid()
                                                          : calibration_.activeAzLinearValid(),
      calibrationState.calibrationMode == CAL_MULTI_POINT ? calibrationState.elMultiValid
                                                          : calibrationState.elLinearValid);
}

void Application::setCalibrationMode(CalibrationMode m) {
  auto &calibrationState = calibration_.state();
  calibrationState.calibrationMode = m;
  storage_.saveCalibrationMode(runtime_.controller.model, calibrationState.calibrationMode);
}

void Application::setGsAzMode(uint8_t m) {
  auto &calibrationState = calibration_.state();
  if (m != 36 && m != 45) {
    return;
  }

  // Repeating the already selected range must not interrupt tracking.
  if (m == calibrationState.gsAzMode) {
    storage_.saveAzMode(runtime_.controller.model, calibrationState.gsAzMode);
    calibrationState.cal.valid = calibration_.selectedCalibrationValid();

    // LIVE P36/P45 TFT indication:
    // even a repeated GS-232 range command is confirmed on the display.
    invalidateDisplay();
    return;
  }

  // SAFETY: prevent an unreachable old target after 450 -> 360 switch.
  hardware_.stopAzimuth();

  runtime_.controller.setMaximum(m == 36 ? 360 : 450);
  calibrationState.gsAzMode = m;
  storage_.saveAzMode(runtime_.controller.model, calibrationState.gsAzMode);

  updatePosition();
  runtime_.targetAz = runtime_.currentAz; // cancel stale AZ target without disturbing EL target
  calibrationState.cal.valid = calibration_.selectedCalibrationValid();

  // P36/P45 is part of the static dashboard, so force immediate redraw.
  // Result: received P36 -> display P36, received P45 -> display P45.
  invalidateDisplay();

  diagnostics_.dbgPrint("[AZ MODE] P");
  diagnostics_.dbgPrint(String(calibrationState.gsAzMode));
  diagnostics_.dbgPrintln(" selected; AZ stopped and stale AZ target cancelled");
}

void Application::captureGs232Cal(const String &cmd) {
  auto &calibrationState = calibration_.state();
  if (runtime_.controller.model == forkrot::Model::G800DXA && (cmd == "O2" || cmd == "F2")) {
    return;
  }

  int az = hardware_.adcAvgMv(PIN_AZ_ADC);
  int el = hardware_.adcAvgMv(PIN_EL_ADC);

  if (cmd == "O") {
    if (calibrationState.gsAzMode == 36) {
      calibrationState.cal.az36_0 = az;
      calibrationState.az36OSet = true;
    } else {
      calibrationState.cal.az45_0 = az;
      calibrationState.az45OSet = true;
    }
  } else if (cmd == "F") {
    if (calibrationState.gsAzMode == 36) {
      calibrationState.cal.az36Max = az;
      calibrationState.az36FSet = true;
    } else {
      calibrationState.cal.az45Max = az;
      calibrationState.az45FSet = true;
    }
  } else if (cmd == "O2") {
    calibrationState.cal.el0 = el;
    calibrationState.elOSet = true;
  } else if (cmd == "F2") {
    calibrationState.cal.elMax = el;
    calibrationState.elFSet = true;
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
