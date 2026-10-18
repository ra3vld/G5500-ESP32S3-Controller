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
// Geometry is supplied by the actual LVGL layout. A target tap may become a swipe;
// started gestures keep ownership until release.
struct TouchLayout {
  TouchRect current{}, target{}, arrows[4]{}; // left, right, up, down
  bool dualAxis = false, enabled = true;
};
struct TouchEvent {
  TouchAction action = TouchAction::None;
  bool manual = false, release = false;
  uint32_t gesture = 0;
  uint8_t mask = 0;
  float az = 0, el = 0;
};
class TouchGesture {
  enum class Role { None, Mode, Ring, Target, Swipe, Arrow } role_ = Role::None;
  bool down_ = false, cancelled_ = false, blocked_ = false;
  uint16_t startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0;
  float ringBearing_ = -1;
  unsigned arrow_ = 0;
  uint8_t mask_ = 0;
  uint32_t gesture_ = 0;
  TouchLayout captured_{};
  static float scale(int displacement) {
    const int distance = std::abs(displacement);
    if (distance <= 12) {
      return 0;
    }
    const float magnitude = distance >= 100 ? 1.f : float(distance - 12) / 88.f;
    return displacement < 0 ? -magnitude : magnitude;
  }

public:
  bool contactDown() const {
    return down_;
  }
  TouchAction pressed() const {
    return down_ && role_ == Role::Mode ? TouchAction::Mode : TouchAction::None;
  }
  TouchEvent sample(bool down, uint16_t x, uint16_t y, const TouchLayout &layout) {
    TouchEvent e;
    if (blocked_) {
      down_ = down;
      if (!down && layout.enabled) {
        blocked_ = false;
      }
      return e;
    }
    if (down) {
      lastX_ = x;
      lastY_ = y;
    } else {
      x = lastX_;
      y = lastY_;
    }
    if (down && !down_) {
      captured_ = layout;
      startX_ = x;
      startY_ = y;
      mask_ = 0;
      cancelled_ = false;
      ringBearing_ = -1;
      role_ = Role::None;
      if (layout.enabled) {
        if (ModeButton.contains(x, y)) {
          role_ = Role::Mode;
          e.action = TouchAction::Mode;
        } else {
          for (unsigned i = 0; i < (layout.dualAxis ? 4u : 2u); ++i) {
            if (layout.arrows[i].contains(x, y)) {
              role_ = Role::Arrow;
              arrow_ = i;
              break;
            }
          }
          if (role_ == Role::None) {
            if (layout.current.contains(x, y)) {
              role_ = Role::Swipe;
            } else if (layout.target.contains(x, y)) {
              role_ = Role::Target;
            } else if (dialContains(x, y)) {
              role_ = Role::Ring;
            }
          }
        }
      }
      if (role_ == Role::Swipe || role_ == Role::Arrow || role_ == Role::Target) {
        if (!++gesture_) {
          ++gesture_;
        }
      }
    }
    if (down && (std::abs(int(x) - startX_) > 12 || std::abs(int(y) - startY_) > 12)) {
      cancelled_ = true;
      // A target tap becomes a captured swipe once it leaves the dead zone.
      if (role_ == Role::Target && layout.enabled) {
        role_ = Role::Swipe;
      }
    }
    if (role_ == Role::Swipe || role_ == Role::Arrow) {
      if (down || down_) {
        e.manual = true;
        e.release = !down || !layout.enabled;
        e.gesture = gesture_;
        if (!e.release) {
          if (role_ == Role::Arrow) {
            if (arrow_ < 2) {
              e.az = arrow_ ? 1 : -1;
            } else {
              e.el = arrow_ == 2 ? 1 : -1;
            }
          } else {
            e.az = scale(int(x) - startX_);
            e.el = captured_.dualAxis ? scale(int(startY_) - y) : 0;
          }
          if (e.az) {
            mask_ |= 1;
          }
          if (e.el) {
            mask_ |= 2;
          }
        }
        e.mask = mask_;
      }
    } else if (down && role_ == Role::Ring && layout.enabled && dialContains(x, y)) {
      const float bearing = dialBearing(x, y);
      // Keep the contact's role, but only submit a target when its angle changes.
      if (bearing != ringBearing_) {
        ringBearing_ = bearing;
        e.action = TouchAction::Target;
      }
    } else if (!down && down_ && role_ == Role::Target && !cancelled_ && layout.enabled &&
               captured_.target.contains(x, y)) {
      e.action = TouchAction::Confirm;
    }
    if (!layout.enabled) {
      role_ = Role::None;
      blocked_ = true;
    }
    if (!down) {
      role_ = Role::None;
    }
    down_ = down;
    return e;
  }
};
// CST8xx registers 0x02..0x06: finger count, event/XH, XL, YH, YL.
// Native panel coordinates, matching Elecrow Board21.h (no UI-specific offset).
inline bool touchFrameHealthy(const uint8_t (&data)[5]) {
  const unsigned fingers = data[0] & 0x0f;
  // An idle frame has no coordinates/event; CST8xx may leave those bytes at 0xff.
  if (!fingers) return true;
  const unsigned px = ((data[1] & 0x0f) << 8) | data[2];
  const unsigned py = ((data[3] & 0x0f) << 8) | data[4];
  return fingers == 1 && (data[1] >> 6) != 3 && px < 480 && py < 480;
}
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
