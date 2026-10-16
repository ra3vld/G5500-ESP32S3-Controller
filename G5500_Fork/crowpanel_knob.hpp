#pragma once
#include "control.hpp"
#include <algorithm>
namespace forkrot {
// UI gestures are independent of GPIO and use the same controller as web/GS-232.
class CrowPanelKnob {
  Controller &controller_;
  bool raw_ = false, down_ = false, blocked_ = true, long_ = false;
  bool handledOnPress_ = false, click_ = false, inverted_ = true;
  bool canResume_ = false;
  Target resumeTarget_{0, 0, false, false};
  uint32_t rawAt_ = 0, downAt_ = 0, clickAt_ = 0, epoch_ = 0;
  unsigned axis_ = 0;
  float maximum_ = 0;
  Model model_ = Model::G5500;
  void synchronize(bool inverted) {
    if (model_ != controller_.model || maximum_ != controller_.maximum || epoch_ != controller_.stopEpoch || inverted_ != inverted) {
      click_ = false;
      canResume_ = false;
      model_ = controller_.model;
      maximum_ = controller_.maximum;
      epoch_ = controller_.stopEpoch;
      inverted_ = inverted;
    }
    if (model_ == Model::G800DXA) axis_ = 0;
    if (canResume_ && (controller_.moving() || controller_.axes[0].wanted || controller_.axes[1].wanted ||
        controller_.pendingValid || !controller_.targetKnown ||
        (resumeTarget_.hasAz && controller_.axes[0].target != resumeTarget_.az) ||
        (resumeTarget_.hasEl && controller_.axes[1].target != resumeTarget_.el))) canResume_ = false;
  }
public:
  static constexpr float StepDegrees = 20.f;
  bool modeChanged = false;
  Result lastResult = Result::Ok;
  explicit CrowPanelKnob(Controller &controller) : controller_(controller) {}
  unsigned axis() const { return axis_; }
  void selectAxis() { axis_ = controller_.model == Model::G5500 ? 1 - axis_ : 0; canResume_ = false; }
  void stop(uint32_t now) {
    controller_.stopNow(-1, now);
    epoch_ = controller_.stopEpoch;
    click_ = false;
    canResume_ = false;
  }
  void toggleMode(uint32_t now) {
    stop(now);
    controller_.setMode(controller_.mode == Mode::Manual ? Mode::Auto : Mode::Manual, now);
    modeChanged = true;
    long_ = true; // A held knob must not confirm a target after a touch toggle.
  }
  void confirm(uint32_t now) {
    if (controller_.moving() || controller_.axes[0].wanted || controller_.axes[1].wanted) {
      // Only a knob stop pauses a tracked target; touch/fault/mode STOP cancels it.
      const Target interrupted{controller_.axes[0].target, controller_.axes[1].target,
                               controller_.axes[0].tracking, controller_.axes[1].tracking};
      stop(now);
      resumeTarget_ = interrupted;
      canResume_ = interrupted.hasAz || interrupted.hasEl;
      return;
    }
    if (canResume_ && !controller_.pendingValid) {
      lastResult = controller_.target(resumeTarget_, false, now);
      if (lastResult == Result::Ok) canResume_ = false;
      return;
    }
    canResume_ = false;
    lastResult = controller_.execute(now);
  }
  // A failed PCF8574 read cancels the gesture; recovery requires stable release.
  void sample(bool healthy, bool pressed, int delta, uint32_t now, bool inverted = true) {
    synchronize(inverted);
    if (!healthy) {
      stop(now);
      blocked_ = true; raw_ = down_ = false; long_ = false; rawAt_ = now;
      return;
    }
    if (pressed != raw_) { raw_ = pressed; rawAt_ = now; }
    if (blocked_) {
      if (!pressed && uint32_t(now - rawAt_) >= 30) blocked_ = false;
      else return;
    }
    bool released = false;
    // Act on the first pressed sample. Debounce only release so a short STOP
    // cannot vanish between polls, and contact bounce cannot become resume.
    if (raw_ != down_ && (raw_ || uint32_t(now - rawAt_) >= 30)) {
      down_ = raw_;
      if (down_) {
        downAt_ = now; long_ = false;
        handledOnPress_ = controller_.moving() || controller_.axes[0].wanted || controller_.axes[1].wanted;
        if (handledOnPress_) { click_ = false; confirm(now); }
      } else {
        released = true;
        if (!long_ && !handledOnPress_) {
          if (click_ && uint32_t(now - clickAt_) <= 350) { selectAxis(); click_ = false; }
          else { click_ = true; clickAt_ = now; }
        }
      }
    }
    if (down_ && !long_ && uint32_t(now - downAt_) >= 1200) {
      toggleMode(now);
    }
    if (click_ && !down_ && !raw_ && uint32_t(now - clickAt_) > 350) {
      click_ = false; confirm(now);
    }
    if (delta && !raw_ && !down_ && !released) {
      canResume_ = false;
      const auto &axis = controller_.axes[axis_];
      const float origin = axis.tracking ? axis.target : axis.current;
      const float next = std::clamp(origin + (inverted_ ? -float(delta) : float(delta)) * StepDegrees, 0.f,
                                   axis_ ? 180.f : controller_.maximum);
      // Every detent updates the controller immediately, including during motion.
      Target target{axis_ ? 0.f : next, axis_ ? next : 0.f, axis_ == 0, axis_ == 1};
      lastResult = controller_.target(target, false, now);
      click_ = false;
    }
  }
};
}
