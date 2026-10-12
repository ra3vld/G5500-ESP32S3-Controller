// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#include "calibration.hpp"
namespace forkrot {
float clampf(float value, float minimum, float maximum) {
  return value < minimum ? minimum : (value > maximum ? maximum : value);
}

float mapCal(int raw, int zeroPoint, int fullScalePoint, float maximumDegrees) {
  if (zeroPoint == fullScalePoint) {
    return 0;
  }
  return clampf((raw - zeroPoint) * maximumDegrees / (float)(fullScalePoint - zeroPoint), 0,
                maximumDegrees);
}

bool calPointsMonotonic(const int *millivolts, uint8_t pointCount) {
  if (pointCount < 2) {
    return false;
  }
  for (uint8_t i = 0; i < pointCount; i++) {
    if (millivolts[i] < 0) {
      return false;
    }
  }
  bool increasing = millivolts[pointCount - 1] > millivolts[0];
  if (millivolts[pointCount - 1] == millivolts[0]) {
    return false;
  }
  for (uint8_t i = 1; i < pointCount; i++) {
    if (increasing && millivolts[i] <= millivolts[i - 1]) {
      return false;
    }
    if (!increasing && millivolts[i] >= millivolts[i - 1]) {
      return false;
    }
  }
  return true;
}

float mapPiecewise(int raw, const int *millivolts, const float *degrees, uint8_t pointCount) {
  if (!calPointsMonotonic(millivolts, pointCount)) {
    return 0.0f;
  }
  bool increasing = millivolts[pointCount - 1] > millivolts[0];
  if ((increasing && raw <= millivolts[0]) || (!increasing && raw >= millivolts[0])) {
    return degrees[0];
  }
  if ((increasing && raw >= millivolts[pointCount - 1]) ||
      (!increasing && raw <= millivolts[pointCount - 1])) {
    return degrees[pointCount - 1];
  }
  for (uint8_t i = 0; i < pointCount - 1; i++) {
    bool inside = increasing ? (raw >= millivolts[i] && raw <= millivolts[i + 1])
                             : (raw <= millivolts[i] && raw >= millivolts[i + 1]);
    if (inside) {
      float fraction = (raw - millivolts[i]) / (float)(millivolts[i + 1] - millivolts[i]);
      return degrees[i] + fraction * (degrees[i + 1] - degrees[i]);
    }
  }
  return degrees[pointCount - 1];
}

bool Calibration::activeAzLinearValid() const {
  return state_.gsAzMode == 36 ? state_.az36LinearValid : state_.az45LinearValid;
}

bool Calibration::activeAzMultiValid() const {
  return state_.gsAzMode == 36 ? state_.az36MultiValid : state_.az45MultiValid;
}

int Calibration::activeAz2Zero() const {
  return state_.gsAzMode == 36 ? state_.cal.az36_0 : state_.cal.az45_0;
}

int Calibration::activeAz2Full() const {
  return state_.gsAzMode == 36 ? state_.cal.az36Max : state_.cal.az45Max;
}

bool Calibration::activeAzOSet() const {
  return state_.gsAzMode == 36 ? state_.az36OSet : state_.az45OSet;
}

bool Calibration::activeAzFSet() const {
  return state_.gsAzMode == 36 ? state_.az36FSet : state_.az45FSet;
}

float Calibration::activeAzMaxDeg() const {
  return state_.gsAzMode == 36 ? 360.0f : 450.0f;
}

void Calibration::refreshMultiCalValidity() {
  state_.az36MultiValid = calPointsMonotonic(state_.az36CalMv, AZ36_CAL_N);
  state_.az45MultiValid = calPointsMonotonic(state_.az45CalMv, AZ45_CAL_N);
  state_.elMultiValid = calPointsMonotonic(state_.elCalMv, EL_CAL_N);
}

bool Calibration::selectedCalibrationValid() const {
  bool azOk =
      state_.calibrationMode == CAL_MULTI_POINT ? activeAzMultiValid() : activeAzLinearValid();
  bool elOk =
      state_.calibrationMode == CAL_MULTI_POINT ? state_.elMultiValid : state_.elLinearValid;
  return azOk && (controller_.model == forkrot::Model::G800DXA || elOk);
}

const char *Calibration::calibrationModeName() const {
  return state_.calibrationMode == CAL_MULTI_POINT ? "MULTI-POINT" : "GS232 2-POINT";
}
} // namespace forkrot
