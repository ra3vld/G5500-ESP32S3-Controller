// G5500 Fork; GPL-3.0.
#pragma once
#include <cmath>

namespace forkrot {
inline constexpr float DefaultEncoderStepDegrees = 20.f;
inline bool validEncoderStepDegrees(float value) {
  return std::isfinite(value) && value >= .1f && value <= 90.f;
}
inline float checkedEncoderStepDegrees(float value) {
  return validEncoderStepDegrees(value) ? value : DefaultEncoderStepDegrees;
}
}
