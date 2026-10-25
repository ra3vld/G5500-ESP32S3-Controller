// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace forkrot::panel {
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
inline constexpr TouchRect SpeedTracks[2]{{100, 207, 280, 40}, {100, 302, 280, 40}};
inline constexpr TouchRect SpeedBack{160, 374, 160, 46};
enum class TouchAction { None, Mode, Target, Confirm, SpeedOpen, SpeedClose, SpeedValue, Stop, Axis, Settings };
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
  bool dualAxis = false, enabled = true, speedScreen = false;
  bool speedEnabled[2]{};
  bool confirmAllowed=true;
  uint32_t actionEpoch=0;
  TouchRect stopArea{};
  TouchRect mode=ModeButton, name=HudNameButton, peers=HudList, rows[3]{}, axis{}, settings{}, stop{};
  TouchRect speedTracks[2]{SpeedTracks[0],SpeedTracks[1]}, speedBack=SpeedBack;
  int centerX=DialCenter, centerY=DialCenter, innerRadius=DialTouchInner, outerRadius=DialTouchOuter;
  unsigned width=480,height=480;
  int swipeDeadzone=12, swipeFull=100;
  bool dialContains(uint16_t x,uint16_t y) const {
    if(x>=width || y>=height) return false;
    const int dx=int(x)-centerX,dy=int(y)-centerY,r=dx*dx+dy*dy;
    return r>=innerRadius*innerRadius && r<=outerRadius*outerRadius;
  }
  float bearing(uint16_t x,uint16_t y) const {
    return float((std::lround(std::atan2(float(int(x)-centerX),float(centerY-int(y))) *
        180.f/3.14159265358979323846f)+360)%360);
  }
};
struct TouchEvent {
  TouchAction action = TouchAction::None;
  bool manual = false, release = false, absoluteSpeed = false;
  uint32_t gesture = 0;
  uint8_t mask = 0;
  float az = 0, el = 0;
  unsigned axis = 0, percent = 5;
};
class TouchGesture {
  enum class Role { None, Mode, Ring, Target, Swipe, Arrow, Speed, Back, Stop, Axis, Settings } role_ = Role::None;
  bool down_ = false, cancelled_ = false, blocked_ = false;
  uint16_t startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0;
  float ringBearing_ = -1;
  unsigned arrow_ = 0;
  uint8_t mask_ = 0;
  uint32_t gesture_ = 0;
  TouchLayout captured_{};
  bool tap_ = false;
  uint32_t tapAt_ = 0, downAt_ = 0;
  uint16_t tapX_ = 0, tapY_ = 0;
  static float scale(int displacement, const TouchLayout &layout) {
    const int distance = std::abs(displacement);
    if (distance <= layout.swipeDeadzone) {
      return 0;
    }
    const float fraction = distance >= layout.swipeFull ? 1.f : float(distance - layout.swipeDeadzone) / float(layout.swipeFull-layout.swipeDeadzone);
    const float magnitude = fraction * fraction;
    return displacement < 0 ? -magnitude : magnitude;
  }

public:
  void cancel() { role_=Role::None;down_=false;blocked_=true;tap_=false; }
  bool manualActive() const { return down_ && (role_==Role::Swipe || role_==Role::Arrow); }
  bool contactDown() const {
    return down_;
  }
  TouchAction pressed() const {
    return down_ && role_ == Role::Mode ? TouchAction::Mode : TouchAction::None;
  }
  TouchEvent sample(bool down, uint16_t x, uint16_t y, const TouchLayout &layout, uint32_t now = 0) {
    TouchEvent e;
    if(role_==Role::Target && (captured_.actionEpoch!=layout.actionEpoch ||
        !captured_.confirmAllowed || !layout.confirmAllowed)) cancelled_=true;
    // A new STOP press is immediate even during input recovery. An existing
    // contact keeps its gesture when the button appears or moves underneath it.
    if (down && !down_ && layout.stop.contains(x,y)) {
      e.action=TouchAction::Stop;
      role_=Role::Stop;down_=true;blocked_=true;tap_=false;
      return e;
    }
    if (blocked_) {
      down_ = down;
      if (!down && layout.enabled) {
        blocked_ = false;role_=Role::None;
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
      downAt_ = now;
      startX_ = x;
      startY_ = y;
      mask_ = 0;
      cancelled_ = false;
      ringBearing_ = -1;
      role_ = Role::None;
      if (layout.enabled) {
        if (layout.speedScreen) {
          tap_ = false;
          if (layout.speedBack.contains(x, y)) role_ = Role::Back;
          for (unsigned i = 0; i < (layout.dualAxis ? 2u : 1u); ++i)
            if (layout.speedEnabled[i] && layout.speedTracks[i].contains(x, y)) { role_ = Role::Speed; arrow_ = i; }
        } else if (layout.mode.contains(x, y)) {
          role_ = Role::Mode;
          e.action = TouchAction::Mode;
        } else {
          if(layout.axis.contains(x,y) && layout.dualAxis) { role_=Role::Axis; e.action=TouchAction::Axis; }
          else if(layout.settings.contains(x,y)) { role_=Role::Settings; e.action=TouchAction::Settings; }
          for (unsigned i = 0; role_==Role::None && i < (layout.dualAxis ? 4u : 2u); ++i) {
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
            } else if (layout.dialContains(x, y)) {
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
    if (down && (std::abs(int(x) - startX_) > captured_.swipeDeadzone || std::abs(int(y) - startY_) > captured_.swipeDeadzone)) {
      cancelled_ = true;
      // A target tap becomes a captured swipe once it leaves the dead zone.
      if (role_ == Role::Target && layout.enabled) {
        role_ = Role::Swipe;
      }
    }
    if (down && role_ == Role::Speed && layout.enabled && layout.speedScreen) {
      e.action = TouchAction::SpeedValue; e.axis = arrow_;
      const auto &track=layout.speedTracks[arrow_];
      const int width=std::max(1,int(track.width));
      const int offset=std::clamp(int(x)-int(track.x),0,width);
      e.percent=unsigned(5+(95*offset+width/2)/width);
    } else if (!down && down_ && role_ == Role::Back && layout.enabled && layout.speedBack.contains(x, y)) {
      e.action = TouchAction::SpeedClose;
    } else if (role_ == Role::Swipe || role_ == Role::Arrow) {
      if (down || down_) {
        e.manual = true;
        e.absoluteSpeed = role_ == Role::Swipe;
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
            e.az = scale(int(x) - startX_, captured_);
            e.el = captured_.dualAxis ? scale(int(startY_) - y, captured_) : 0;
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
    } else if (down && role_ == Role::Ring && layout.enabled && layout.dialContains(x, y)) {
      const float bearing = layout.bearing(x, y);
      // Keep the contact's role, but only submit a target when its angle changes.
      if (bearing != ringBearing_) {
        ringBearing_ = bearing;
        e.action = TouchAction::Target;
      }
    } else if (!down && down_ && role_ == Role::Target && !cancelled_ && layout.enabled && captured_.confirmAllowed && layout.confirmAllowed &&
               captured_.target.contains(x, y)) {
      e.action = TouchAction::Confirm;
    }
    if (!down && down_ && role_ == Role::Swipe && !cancelled_ && layout.enabled &&
        captured_.current.contains(x, y) && uint32_t(now - downAt_) <= 250) {
      if (tap_ && uint32_t(now - tapAt_) <= 350 &&
          std::abs(int(x) - tapX_) <= 30 && std::abs(int(y) - tapY_) <= 30) {
        e.action = TouchAction::SpeedOpen; tap_ = false;
      } else { tap_ = true; tapAt_ = now; tapX_ = x; tapY_ = y; }
    } else if (cancelled_ || !layout.enabled || (down && role_ != Role::Swipe)) tap_ = false;
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
} // namespace forkrot::panel
