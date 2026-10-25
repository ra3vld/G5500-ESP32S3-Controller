#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace forkrot::crowpanel {
inline constexpr float LimitWarningDegrees = 50.f;
inline constexpr int LimitGradientDegrees = 60;
inline uint8_t limitOpacity(float distance) {
  if (!std::isfinite(distance) || distance >= LimitWarningDegrees) return 0;
  const float proximity = std::clamp(1.f - distance / LimitWarningDegrees, 0.f, 1.f);
  return uint8_t(std::lround(255.f * proximity * (2.f - proximity)));
}
inline float shortestBearingDelta(float from, float to) {
  float delta = std::fmod(to - from, 360.f);
  if (delta > 180.f) delta -= 360.f;
  if (delta < -180.f) delta += 360.f;
  return delta;
}
}
