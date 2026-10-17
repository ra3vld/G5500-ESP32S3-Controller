// HUD selection and knob gestures; no GPIO or display dependencies. GPL-3.0.
#pragma once
#include "hud.hpp"
#include <algorithm>
namespace forkrot {
class HudKnob {
  Hud &hud_;
  Controller &controller_;
  bool raw_ = false, down_ = false, long_ = false, blocked_ = true, click_ = false;
  bool resume_ = false;
  Target resumeTarget_{};
  uint32_t pauseRequest_ = 0, pauseEpoch_ = 0;
  uint32_t edgeAt_ = 0, downAt_ = 0, clickAt_ = 0, epoch_ = 0, menuAt_ = 0, stopEpoch_ = 0;
  uint32_t busyAt_ = 0, busyEpoch_ = 0;
  bool busyNotice_ = false;
  unsigned axis_ = 0;
  void recordResult(Result result, uint32_t now) {
    lastResult = result;
    busyNotice_ = result == Result::Busy;
    busyAt_ = now;
    busyEpoch_ = hud_.selectionEpoch;
  }
  void sync() {
    if (epoch_ != hud_.selectionEpoch) {
      epoch_ = hud_.selectionEpoch;
      axis_ = 0;
      resume_ = click_ = false;
      pauseRequest_ = 0;
      menu = false;
      long_ = true;
    }
    if (pauseRequest_) {
      const auto result = hud_.result(pauseRequest_).result;
      if (result != Result::Deferred) {
        if (result != Result::Ok || controller_.stopEpoch != pauseEpoch_ || controller_.moving())
          resume_ = false;
        stopEpoch_ = controller_.stopEpoch;
        pauseRequest_ = 0;
      }
    } else if (stopEpoch_ != controller_.stopEpoch) {
      stopEpoch_ = controller_.stopEpoch;
      resume_ = false;
    }
    if (controller_.model == Model::G800DXA)
      axis_ = 0;
  }

public:
  bool menu = false, selectionChanged = false;
  uint64_t highlighted = 0;
  Result lastResult = Result::Ok;
  explicit HudKnob(Hud &h, Controller &c) : hud_(h), controller_(c) {}
  unsigned axis() const { return axis_; }
  bool busyNotice(uint32_t now) {
    // A rejected command is a short notification, not the rotator's state.
    if (busyNotice_ && (uint32_t(now - busyAt_) >= 1000 ||
                        busyEpoch_ != hud_.selectionEpoch || !hud_.available()))
      busyNotice_ = false;
    return busyNotice_;
  }
  void confirmPending(uint32_t now) {
    sync();
    if (menu || controller_.mode != Mode::Manual || !controller_.pendingValid ||
        !(axis_ ? controller_.pending.hasEl : controller_.pending.hasAz)) return;
    resume_ = click_ = false;
    pauseRequest_ = 0;
    long_ = true;
    recordResult(controller_.execute(now), now);
  }
  void setBearing(float bearing, uint32_t now) {
    sync();
    if (menu) return;
    resume_ = click_ = false;
    pauseRequest_ = 0;
    long_ = true;
    const float target = axis_ ? bearing : nearest(bearing, controller_.axes[0].current, controller_.maximum);
    recordResult(controller_.target({axis_ ? 0.f : target, axis_ ? target : 0.f, axis_ == 0, axis_ == 1}, false, now), now);
  }
  void stop(uint32_t now) {
    controller_.stopNow(-1, now);
    resume_ = click_ = false;
    recordResult(hud_.lastResult(), now);
  }
  void toggleMode(uint32_t now) {
    ControlCommand c;
    c.op = ControlOp::ToggleMode;
    recordResult(hud_.submit(c), now);
    resume_ = click_ = false;
    long_ = true;
  }
  void choose(uint64_t id) {
    if (hud_.select(id)) {
      selectionChanged = true;
      menu = false;
      resume_ = click_ = false;
      epoch_ = hud_.selectionEpoch;
      axis_ = 0;
    }
  }
  void moveSelection(int delta) {
    const size_t count = hud_.count();
    if (!count)
      return;
    size_t index = 0, current = 0;
    for (auto *p = hud_.peers(); p; p = p->next.get())
      if (p->saved) {
        if (p->id == highlighted)
          current = index;
        ++index;
      }
    const int64_t next = (int64_t(current) + delta) % int64_t(count);
    const size_t wanted = size_t(next < 0 ? next + int64_t(count) : next);
    index = 0;
    for (auto *p = hud_.peers(); p; p = p->next.get())
      if (p->saved && index++ == wanted) {
        highlighted = p->id;
        break;
      }
    menuAt_ = hud_.now;
  }
  void open() {
    const size_t count = hud_.count();
    if (count < 2)
      return;
    highlighted = hud_.config.selected;
    if (count == 2) {
      moveSelection(1);
      choose(highlighted);
    } else {
      menu = true;
      menuAt_ = hud_.now;
      if (!hud_.peer(highlighted))
        for (auto *p = hud_.peers(); p; p = p->next.get())
          if (p->saved) {
            highlighted = p->id;
            break;
          }
    }
    click_ = resume_ = false;
  }
  void sample(bool healthy, bool pressed, int delta, uint32_t now, bool inverted = true) {
    sync();
    if (menu && uint32_t(now - menuAt_) >= 10000)
      menu = false;
    if (!healthy) {
      if (!blocked_)
        stop(now);
      blocked_ = true;
      raw_ = down_ = false;
      click_ = false;
      return;
    }
    if (pressed != raw_) {
      raw_ = pressed;
      edgeAt_ = now;
    }
    if (blocked_) {
      if (!pressed && uint32_t(now - edgeAt_) >= 30)
        blocked_ = false;
      else
        return;
    }
    if (raw_ != down_ && (raw_ || uint32_t(now - edgeAt_) >= 30)) {
      down_ = raw_;
      if (down_) {
        downAt_ = now;
        long_ = false;
      } else if (!long_) {
        if (menu) {
          choose(highlighted);
        } else if (controller_.moving()) {
          resumeTarget_ = {controller_.axes[0].target, controller_.axes[1].target,
                           controller_.axes[0].tracking, controller_.axes[1].tracking};
          pauseEpoch_ = controller_.stopEpoch + 1;
          stop(now);
          pauseRequest_ = hud_.requestId();
          resume_ = resumeTarget_.hasAz || resumeTarget_.hasEl;
        } else if (click_ && uint32_t(now - clickAt_) <= 350) {
          axis_ = controller_.model == Model::G5500 ? 1 - axis_ : 0;
          click_ = resume_ = false;
        } else {
          click_ = true;
          clickAt_ = now;
        }
      }
    }
    if (down_ && !long_ && uint32_t(now - downAt_) >= 1200) {
      long_ = true;
      if (menu)
        menu = false;
      else
        open();
    }
    if (click_ && !raw_ && !down_ && uint32_t(now - clickAt_) > 350) {
      click_ = false;
      recordResult(resume_ && !controller_.pendingValid ? controller_.target(resumeTarget_, false, now)
                                                       : controller_.execute(now), now);
      resume_ = false;
    }
    if (delta && !raw_ && !down_) {
      if (menu) {
        moveSelection(delta);
        return;
      }
      resume_ = click_ = false;
      const auto &a = controller_.axes[axis_];
      const float base = controller_.intendedTarget(axis_, a.tracking ? a.target : a.current);
      const float next = std::clamp(base + (inverted ? -float(delta) : float(delta)) * 20.f, 0.f,
                                    axis_ ? 180.f : controller_.maximum);
      recordResult(
          controller_.target({axis_ ? 0.f : next, axis_ ? next : 0.f, axis_ == 0, axis_ == 1}, false, now), now);
    }
  }
};
} // namespace forkrot
