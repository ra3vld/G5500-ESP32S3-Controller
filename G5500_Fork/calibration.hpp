// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "control.hpp"
namespace forkrot {

enum CalibrationMode : uint8_t { CAL_GS232_2POINT = 0, CAL_MULTI_POINT = 1 };
inline constexpr uint8_t AZ36_CAL_N = 5, AZ45_CAL_N = 6, EL_CAL_N = 5;
inline constexpr float az36CalDeg[AZ36_CAL_N] = {0, 90, 180, 270, 360};
inline constexpr float az45CalDeg[AZ45_CAL_N] = {0, 90, 180, 270, 360, 450};
inline constexpr float elCalDeg[EL_CAL_N] = {0, 45, 90, 135, 180};
struct CalibrationEndpoints {
  int az36_0 = 0, az36Max = 3100, az45_0 = 0, az45Max = 3100;
  int el0 = 0, elMax = 3100;
  bool valid = false;
};
struct CalibrationState {
  CalibrationEndpoints cal;
  CalibrationMode calibrationMode = CAL_GS232_2POINT;
  uint8_t gsAzMode = 45;
  int az36CalMv[AZ36_CAL_N] = {-1, -1, -1, -1, -1};
  int az45CalMv[AZ45_CAL_N] = {-1, -1, -1, -1, -1, -1};
  int elCalMv[EL_CAL_N] = {-1, -1, -1, -1, -1};
  bool az36MultiValid = false, az45MultiValid = false, elMultiValid = false;
  bool az36LinearValid = false, az45LinearValid = false, elLinearValid = false;
  bool az36OSet = false, az36FSet = false, az45OSet = false, az45FSet = false;
  bool elOSet = false, elFSet = false;
};

float clampf(float v, float lo, float hi);
float mapCal(int raw, int a, int b, float maxd);
bool calPointsMonotonic(const int *adc, uint8_t n);
float mapPiecewise(int raw, const int *adc, const float *deg, uint8_t n);
class Calibration {
public:
  explicit Calibration(Controller &controller) : controller_(controller) {}
  CalibrationState &state() {
    return state_;
  }
  const CalibrationState &state() const {
    return state_;
  }
  bool activeAzLinearValid() const;
  bool activeAzMultiValid() const;
  int activeAz2Zero() const;
  int activeAz2Full() const;
  bool activeAzOSet() const;
  bool activeAzFSet() const;
  float activeAzMaxDeg() const;
  void refreshMultiCalValidity();
  bool selectedCalibrationValid() const;
  const char *calibrationModeName() const;

private:
  Controller &controller_;
  CalibrationState state_;
};
} // namespace forkrot
