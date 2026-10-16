#pragma once
#include <cstdint>

namespace forkrot::crowpanel {
struct TouchRect {
  uint16_t x, y, width, height;
  constexpr bool contains(uint16_t px, uint16_t py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};
inline constexpr TouchRect ModeButton{100, 57, 280, 44};
inline constexpr TouchRect StopButton{158, 303, 164, 42};
enum class TouchAction { None, Mode, Stop };
class TouchButtons {
  bool down_ = false;
  TouchAction active_ = TouchAction::None;
public:
  TouchAction pressed() const { return active_; }
  TouchAction sample(bool down, uint16_t x, uint16_t y) {
    const auto hit = !down ? TouchAction::None : ModeButton.contains(x, y) ? TouchAction::Mode
        : StopButton.contains(x, y) ? TouchAction::Stop : TouchAction::None;
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
