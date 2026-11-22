#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
namespace forkrot {
inline constexpr uint8_t AZ_CUSTOM = 0, AZ_P36 = 36, AZ_P45 = 45;
// Continuous mechanical coordinates; compass bearings are a separate view.
struct RotationRange {
  float start = 0, end = 450;
  float span() const { return end - start; }
  static bool tenth(float v) {
    return std::isfinite(v) && v == float(std::round(double(v) * 10) / 10);
  }
  bool valid() const {
    // The computed end may differ from its decimal representation by one float ULP.
    const float roundedEnd = float(std::round(double(end) * 10) / 10);
    const float rounding = std::numeric_limits<float>::epsilon() * std::max(1.f, std::abs(end));
    return tenth(start) && std::isfinite(end) && std::abs(end - roundedEnd) <= rounding && start >= -3600 && start <= 3600 &&
           span() >= 1 && span() <= 3600;
  }
  bool contains(float v) const { return std::isfinite(v) && v >= start && v <= end; }
  float clamp(float v) const { return std::clamp(v, start, end); }
  bool operator==(const RotationRange &r) const { return start == r.start && end == r.end; }
};
using AzRange = RotationRange;
inline constexpr uint8_t EL_CUSTOM = 0, EL_STANDARD = 180;
inline bool validElProfile(uint8_t p, RotationRange r) {
  return r.valid() && (p == EL_CUSTOM || (p == EL_STANDARD && r == RotationRange{0,180}));
}
inline const char *elProfileName(uint8_t p) { return p == EL_STANDARD ? "standard" : "custom"; }
inline bool validAzProfile(uint8_t p, AzRange r) {
  return r.valid() && (p == AZ_CUSTOM ||
         (p == AZ_P36 && r == AzRange{0, 360}) || (p == AZ_P45 && r == AzRange{0, 450}));
}
inline const char *azProfileName(uint8_t p) { return p == 36 ? "p36" : p == 45 ? "p45" : "custom"; }
}
