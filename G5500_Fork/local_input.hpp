// Local hold-to-run controls; modifications by RA3VLD. GPL-3.0.
#pragma once
#include "control.hpp"
#include <cmath>

namespace forkrot {
// Releases stop immediately. A new direction needs 40 ms of stable contact;
// opposing contacts latch the axis off until both have been released.
class AxisButtons {
  int raw_ = 0;
  uint32_t changedAt_ = 0;
  bool blocked_ = true, neutral_ = false;
public:
  float sample(bool negative, bool positive, uint32_t now) {
    const int raw = negative && positive ? 2 : positive ? 1 : negative ? -1 : 0;
    if (raw != raw_) { raw_ = raw; changedAt_ = now; }
    if (raw == 2) { blocked_ = true; neutral_ = false; return 0; }
    if (!raw) {
      neutral_ = uint32_t(now - changedAt_) >= 40;
      if (neutral_) blocked_ = false;
      return 0;
    }
    neutral_ = false;
    return !blocked_ && uint32_t(now - changedAt_) >= 40 ? float(raw) : 0.f;
  }
  bool neutral() const { return neutral_; }
};

class JoystickAxis {
  int minimum_, center_, maximum_, deadzone_, hysteresis_;
  bool inverted_, active_ = false;
public:
  JoystickAxis(int minimum, int center, int maximum, int deadzone, int hysteresis, bool inverted)
      : minimum_(minimum), center_(center), maximum_(maximum), deadzone_(deadzone),
        hysteresis_(hysteresis), inverted_(inverted) {}
  float sample(int raw) {
    const int delta = raw - center_, distance = std::abs(delta);
    if (distance <= deadzone_) active_ = false;
    else if (distance >= deadzone_ + hysteresis_) active_ = true;
    if (!active_) return 0;
    const int span = delta < 0 ? center_ - minimum_ : maximum_ - center_;
    const float amount = std::clamp(float(distance - deadzone_) / float(span - deadzone_), 0.f, 1.f);
    return (delta < 0 ? -amount : amount) * (inverted_ ? -1.f : 1.f);
  }
  bool neutral(int raw) const { return std::abs(raw - center_) <= deadzone_; }
};

// Uses the controller's manual lease and ownership checks. Idle inputs send no
// commands, and releasing an old gesture cannot stop a newer external command.
// Local motor inputs intentionally do not control a selected remote HUD peer.
class LocalAxisInput {
  Controller &controller_;
  const uint8_t mask_;
  static constexpr uint64_t Source = 0x4c4f43414cULL;
  uint32_t gesture_ = 0, serial_ = 0, stopEpoch_ = 0, rangeRevision_ = 0;
  uint32_t epochs_[2]{}, sampledAt_ = 0, neutralAt_ = 0;
  Model model_ = Model::G5500;
  bool started_ = false, active_ = false, armed_ = false, neutral_ = false;
  void release(uint32_t now) {
    if (active_ && !controller_.remote)
      controller_.manualDrive(Source, 1, gesture_, ++serial_, mask_, 0, 0, true, now);
    active_ = armed_ = neutral_ = false;
  }
public:
  LocalAxisInput(Controller &controller, uint8_t mask) : controller_(controller), mask_(mask) {}
  void sample(float az, float el, uint8_t neutralMask, bool healthy, uint32_t now) {
    auto &c = controller_;
    const uint8_t mask = mask_ & (c.model == Model::G800DXA ? 1 : 3);
    if (!(mask & 1)) az = 0;
    if (!(mask & 2)) el = 0;
    bool changed = started_ && (c.stopEpoch != stopEpoch_ || c.rangeRevision != rangeRevision_ || c.model != model_);
    if (active_) for (unsigned i = 0; i < 2; ++i)
      if ((mask & (1u << i)) && c.motionEpoch[i] != epochs_[i]) changed = true;
    const bool stale = started_ && uint32_t(now - sampledAt_) >= 250;
    started_ = true; sampledAt_ = now;
    stopEpoch_ = c.stopEpoch; rangeRevision_ = c.rangeRevision; model_ = c.model;
    if (!healthy || c.remote || c.configurationLocked || c.hasDriveError() || c.autoTune.active() ||
        !mask || changed || stale || !std::isfinite(az) || !std::isfinite(el) || std::abs(az) > 1 || std::abs(el) > 1) {
      release(now);
      return;
    }
    if ((neutralMask & mask) == mask) {
      if (active_) release(now);
      if (!neutral_) { neutral_ = true; neutralAt_ = now; }
      if (uint32_t(now - neutralAt_) >= 40) armed_ = true;
      return;
    }
    neutral_ = false;
    if (!armed_ || (!az && !el && !active_)) return;
    if (!active_) {
      if (!++gesture_) ++gesture_;
      serial_ = 0;
    }
    const auto result = c.manualDrive(Source, 1, gesture_, ++serial_, mask, az, el, false, now);
    if (result != Result::Ok) { release(now); return; }
    active_ = true;
    for (unsigned i = 0; i < 2; ++i) epochs_[i] = c.motionEpoch[i];
  }
};
} // namespace forkrot
