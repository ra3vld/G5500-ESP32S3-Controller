#pragma once
#include <cstdint>
#include <cmath>

namespace forkrot::crowpanel {
struct TouchRect {
  uint16_t x, y, width, height;
  constexpr bool contains(uint16_t px, uint16_t py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};
inline constexpr TouchRect HudNameButton{70, 38, 340, 28};
inline constexpr TouchRect ModeButton{140, 84, 200, 44};
inline constexpr TouchRect HudList{35, 129, 410, 185};
inline constexpr int HudRowTop = HudList.y + 10, HudRowHeight = 58;
inline constexpr int DialCenter = 240, DialTouchInner = 140, DialTouchOuter = 240;
inline bool dialContains(uint16_t x, uint16_t y) {
  if (x >= 480 || y >= 480) return false;
  const int dx = int(x) - DialCenter, dy = int(y) - DialCenter;
  const int radiusSquared = dx * dx + dy * dy;
  return radiusSquared >= DialTouchInner * DialTouchInner &&
         radiusSquared <= DialTouchOuter * DialTouchOuter;
}
inline float dialBearing(uint16_t x, uint16_t y) {
  const float degrees = std::atan2(float(int(x) - DialCenter), float(DialCenter - int(y))) *
                        (180.f / 3.14159265358979323846f);
  return float((std::lround(degrees) + 360) % 360);
}
enum class TouchAction { None, Mode, Target, Confirm };
class TouchButtons {
  bool down_ = false;
  TouchAction active_ = TouchAction::None;
public:
  TouchAction pressed() const { return active_; }
  TouchAction sample(bool down, uint16_t x, uint16_t y, bool targetAllowed = true, bool confirmHit = false) {
    const auto hit = !down ? TouchAction::None : ModeButton.contains(x, y) ? TouchAction::Mode :
                     targetAllowed && confirmHit ? TouchAction::Confirm :
                     targetAllowed && dialContains(x, y) ? TouchAction::Target : TouchAction::None;
    const bool fresh = down && !down_;
    if (fresh) active_ = hit;
    else if (!down || active_ != hit) active_ = TouchAction::None;
    down_ = down;
    return fresh ? active_ : TouchAction::None;
  }
};
// CST8xx registers 0x02..0x06: finger count, event/XH, XL, YH, YL.
// Native panel coordinates, matching Elecrow Board21.h (no UI-specific offset).
inline bool decodeTouch(const uint8_t (&data)[5], uint16_t &x, uint16_t &y) {
  if ((data[0] & 0x0f) != 1 || (data[1] >> 6) == 1 ||
      (data[1] >> 6) == 3)
    return false;
  const uint16_t px = ((data[1] & 0x0f) << 8) | data[2];
  const uint16_t py = ((data[3] & 0x0f) << 8) | data[4];
  if (px >= 480 || py >= 480)
    return false;
  x = px;
  y = py;
  return true;
}
} // namespace forkrot::crowpanel
