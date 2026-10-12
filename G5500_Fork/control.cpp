// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
// G5500 Fork, GPL-3.0; shared by the ESP32 firmware and host simulator.
#include "control.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
namespace forkrot {
namespace {
constexpr uint32_t kAutoHourDurationMs = 3600000;
constexpr uint32_t kReverseDeadTimeMs = 300;
constexpr uint32_t kContinuousMotionTimeoutMs = 180000;
constexpr uint32_t kMinimumSequenceIntervalMs = 1000;
constexpr uint32_t kMaximumSequenceIntervalMs = 999000;
constexpr float kArrivalToleranceDegrees = 0.5f;
} // namespace

Controller::Controller() {
  for (unsigned i = 0; i < 4; ++i) {
    presets[i] = {std::to_string(i * 90) + "°", {float(i * 90), 0, true, true}};
  }
}
const char *Controller::modeName(Mode mode) {
  return mode == Mode::Auto ? "auto" : mode == Mode::AutoHour ? "auto_1h" : "manual";
}
const char *Controller::resultName(Result result) {
  switch (result) {
  case Result::Ok:
    return "ok";
  case Result::Pending:
    return "pending";
  case Result::Ignored:
    return "ignored";
  case Result::NotCalibrated:
    return "not_calibrated";
  default:
    return "invalid";
  }
}
void Controller::feedback(float azimuth, float elevation, bool azCalibrated, bool elCalibrated) {
  axes[0].current = azimuth;
  axes[0].calibrated = azCalibrated && std::isfinite(azimuth);
  axes[1].current = model == Model::G800DXA ? 0 : elevation;
  axes[1].calibrated = model == Model::G5500 && elCalibrated && std::isfinite(elevation);
}
bool Controller::valid(Target candidate) const {
  return (candidate.hasAz || candidate.hasEl) &&
         (!candidate.hasAz ||
          (std::isfinite(candidate.az) && candidate.az >= 0 && candidate.az <= maximum)) &&
         (!candidate.hasEl ||
          (std::isfinite(candidate.el) && candidate.el >= 0 && candidate.el <= 180 &&
           (model == Model::G5500 || candidate.el == 0)));
}
Result Controller::start(Target nextTarget, uint32_t) {
  if (!valid(nextTarget)) {
    return Result::Invalid;
  }
  if ((nextTarget.hasAz && !axes[0].calibrated) ||
      (nextTarget.hasEl && model == Model::G5500 && !axes[1].calibrated)) {
    return Result::NotCalibrated;
  }
  fault.clear();
  targetKnown = true;
  for (unsigned i = 0; i < 2; ++i) {
    if ((i == 0 ? nextTarget.hasAz : nextTarget.hasEl) && (i == 0 || model == Model::G5500)) {
      Axis &axis = axes[i];
      axis.target = i ? nextTarget.el : nextTarget.az;
      float difference = axis.target - axis.current;
      axis.wanted = std::abs(difference) <= kArrivalToleranceDegrees ? 0 : difference > 0 ? 1 : -1;
      axis.tracking = axis.wanted != 0;
    }
  }
  return Result::Ok;
}
Result Controller::target(Target nextTarget, bool external, uint32_t now) {
  expire(now);
  if (!valid(nextTarget)) {
    return Result::Invalid;
  }
  if (external && presetActive) {
    return Result::Ignored;
  }
  cancelSequence();
  if (external && mode == Mode::Manual) {
    pending = nextTarget;
    pendingValid = true;
    return Result::Pending;
  }
  if (!external) {
    presetActive = false;
    presetSlot = -1;
  }
  auto result = start(nextTarget, now);
  if (result == Result::Ok) {
    pendingValid = false;
  }
  return result;
}
Result Controller::jog(unsigned axis, int direction, bool external, uint32_t now) {
  expire(now);
  if (axis > 1 || (axis == 1 && model == Model::G800DXA) || direction < -1 || direction > 1) {
    return Result::Invalid;
  }
  if (direction == 0) {
    stop(axis);
    return Result::Ok;
  }
  if (external && presetActive) {
    return Result::Ignored;
  }
  cancelSequence();
  if (!external) {
    presetActive = false;
    presetSlot = -1;
  }
  axes[axis].tracking = false;
  axes[axis].wanted = direction;
  fault.clear();
  return Result::Ok;
}
void Controller::stop(int axis) {
  if (axis < -1 || axis > 1) {
    return;
  }
  ++stopEpoch;
  cancelSequence();
  pendingValid = false;
  presetActive = false;
  presetSlot = -1;
  for (int i = 0; i < 2; ++i) {
    if (axis < 0 || axis == i) {
      axes[i].wanted = 0;
      axes[i].tracking = false;
      axes[i].speedOutputPercent = 0;
    }
  }
}
Result Controller::execute(uint32_t now) {
  expire(now);
  if (presetActive) {
    return Result::Ignored;
  }
  if (!pendingValid) {
    return Result::Invalid;
  }
  auto result = start(pending, now);
  if (result == Result::Ok) {
    pendingValid = false;
    if (!pendingSequence_.empty()) {
      sequence_ = pendingSequence_;
      pendingSequence_.clear();
      sequenceIndex_ = 0;
      sequenceAdvancedAt_ = now;
    }
  }
  return result;
}
Result Controller::setMode(Mode nextMode, uint32_t now) {
  mode = nextMode;
  autoHourStartedAt_ = now;
  if (nextMode == Mode::Manual) {
    sequence_.clear();
    return Result::Ok;
  }
  if (pendingValid) {
    auto result = execute(now);
    if (result == Result::NotCalibrated) {
      fault = "target_not_calibrated";
      return Result::Ok;
    }
    return result;
  }
  return Result::Ok;
}
void Controller::restoreMode(Mode savedMode) {
  mode = savedMode == Mode::Auto ? Mode::Auto : Mode::Manual;
}
void Controller::setModel(Model nextModel) {
  stop();
  model = nextModel;
  mode = Mode::Manual;
  targetKnown = false;
  fault.clear();
  for (auto &axis : axes) {
    axis.calibrated = false;
    axis.hasRun = false;
  }
}
void Controller::setMaximum(float nextMaximum) {
  if (nextMaximum != 360 && nextMaximum != 450) {
    return;
  }
  stop();
  maximum = nextMaximum;
}
Result Controller::savePreset(unsigned slot, const std::string &label, Target newTarget) {
  if (slot >= 4 || label.empty() || label.size() > 48 || !valid(newTarget)) {
    return Result::Invalid;
  }
  for (unsigned char ch : label) {
    if (ch < 32) {
      return Result::Invalid;
    }
  }
  presets[slot] = {label, newTarget};
  return Result::Ok;
}
Result Controller::preset(unsigned slot, uint32_t now) {
  expire(now);
  if (slot >= 4) {
    return Result::Invalid;
  }
  if (!valid(presets[slot].target)) {
    return Result::Invalid;
  }
  auto result = start(presets[slot].target, now);
  if (result == Result::Ok) {
    cancelSequence();
    pendingValid = false;
    presetSlot = slot;
    presetActive = true;
  }
  return result;
}
Result Controller::sequence(const std::vector<Target> &points, uint32_t interval, uint32_t now) {
  expire(now);
  if (points.empty() || points.size() > 16 || interval < kMinimumSequenceIntervalMs ||
      interval > kMaximumSequenceIntervalMs) {
    return Result::Invalid;
  }
  for (auto point : points) {
    if (!valid(point)) {
      return Result::Invalid;
    }
  }
  if (presetActive) {
    return Result::Ignored;
  }
  cancelSequence();
  sequenceIntervalMs_ = interval;
  if (mode == Mode::Manual) {
    pendingSequence_ = points;
    pending = points[0];
    pendingValid = true;
    return Result::Pending;
  }
  auto result = start(points[0], now);
  if (result == Result::Ok) {
    sequence_ = points;
    sequenceIndex_ = 0;
    sequenceAdvancedAt_ = now;
    pendingValid = false;
  }
  return result;
}
void Controller::cancelSequence() {
  sequence_.clear();
  pendingSequence_.clear();
}
uint32_t Controller::remaining(uint32_t now) const {
  auto elapsed = uint32_t(now - autoHourStartedAt_);
  return mode == Mode::AutoHour && elapsed < kAutoHourDurationMs ? kAutoHourDurationMs - elapsed
                                                                 : 0;
}
bool Controller::moving() const {
  for (auto axis : axes) {
    if (axis.wanted || axis.direction) {
      return true;
    }
  }
  return false;
}
void Controller::output(unsigned axisIndex, int direction, uint32_t now) {
  auto &axis = axes[axisIndex];
  if (axis.direction != 0 && axis.direction != direction) {
    axis.previous = axis.direction;
    axis.direction = 0;
    axis.offAt = now;
  }
  if (direction != 0 && axis.direction == 0) {
    if (axis.previous && axis.previous != direction &&
        uint32_t(now - axis.offAt) < kReverseDeadTimeMs) {
      return;
    }
    axis.direction = direction;
    if (!axis.hasRun) {
      axis.runAt = now;
      axis.hasRun = true;
    }
  }
  if (direction == 0 && uint32_t(now - axis.offAt) >= kReverseDeadTimeMs) {
    axis.hasRun = false;
  }
}
void Controller::expire(uint32_t now) {
  if (mode == Mode::AutoHour && !remaining(now)) {
    mode = Mode::Manual;
    sequence_.clear();
  }
}
void Controller::tick(uint32_t now) {
  expire(now);
  const uint32_t elapsed = speedUpdateStarted_ ? uint32_t(now - lastSpeedUpdateAt_) : 0;
  lastSpeedUpdateAt_ = now;
  speedUpdateStarted_ = true;
  if (!sequence_.empty() && uint32_t(now - sequenceAdvancedAt_) >= sequenceIntervalMs_) {
    if (++sequenceIndex_ >= sequence_.size()) {
      sequence_.clear();
    } else {
      auto result = start(sequence_[sequenceIndex_], now);
      if (result != Result::Ok) {
        sequence_.clear();
        fault = "sequence_calibration";
      }
      sequenceAdvancedAt_ = now;
    }
  }
  for (unsigned i = 0; i < 2; ++i) {
    auto &axis = axes[i];
    if (i == 1 && model == Model::G800DXA) {
      axis.wanted = 0;
      axis.tracking = false;
    }
    if (axis.tracking &&
        (!axis.calibrated ||
         (axis.wanted > 0 && axis.current >= axis.target - kArrivalToleranceDegrees) ||
         (axis.wanted < 0 && axis.current <= axis.target + kArrivalToleranceDegrees))) {
      axis.wanted = 0;
      axis.tracking = false;
    }
    if (axis.calibrated &&
        (((axis.wanted < 0 || axis.direction < 0) && axis.current <= 0) ||
         ((axis.wanted > 0 || axis.direction > 0) && axis.current >= (i ? 180 : maximum)))) {
      axis.wanted = 0;
      axis.tracking = false;
    }
    if (model == Model::G800DXA && axis.hasRun &&
        uint32_t(now - axis.runAt) >= kContinuousMotionTimeoutMs) {
      stop();
      fault = "continuous_motion_timeout";
    }
    const bool variableSpeed = i == 0 && model == Model::G800DXA && speedEnabled;
    if (variableSpeed && axis.direction && axis.wanted && axis.direction != axis.wanted) {
      // Brake in the old direction, then let output() enforce the 300 ms interlock.
      axis.speedOutputPercent =
          std::max(0.f, axis.speedOutputPercent -
                            speedPercent * float(elapsed) / std::max(1u, decelerationMs));
      if (axis.speedOutputPercent > 0) {
        continue;
      }
    }
    const int oldDirection = axis.direction;
    output(i, axis.wanted, now);
    if (!variableSpeed || !axis.direction) {
      axis.speedOutputPercent = 0;
      continue;
    }
    float requested = float(speedPercent);
    if (axis.tracking) {
      const float distance = std::abs(axis.target - axis.current);
      const float fraction = std::max(0.f, std::min(1.f, (distance - kArrivalToleranceDegrees) /
                                                             std::max(1u, slowdownDegrees)));
      const float approach = float(std::min(approachSpeedPercent, speedPercent));
      requested = approach + (speedPercent - approach) * fraction;
    }
    if (!oldDirection) {
      axis.speedOutputPercent = std::min(float(startSpeedPercent), requested);
    } else if (requested > axis.speedOutputPercent) {
      axis.speedOutputPercent =
          std::min(requested, axis.speedOutputPercent +
                                  speedPercent * float(elapsed) / std::max(1u, accelerationMs));
    } else {
      axis.speedOutputPercent =
          std::max(requested, axis.speedOutputPercent -
                                  speedPercent * float(elapsed) / std::max(1u, decelerationMs));
    }
  }
  if (presetActive && !moving()) {
    presetActive = false;
    presetSlot = -1;
  }
}

bool number(const std::string &text, float &result) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  errno = 0;
  std::string value = text;
  std::replace(value.begin(), value.end(), ',', '.');
  result = std::strtof(value.c_str(), &end);
  if (end == value.c_str() || errno || !std::isfinite(result)) {
    return false;
  }
  while (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t') {
    ++end;
  }
  return *end == 0;
}
float heading(float angle) {
  angle = std::fmod(angle, 360.f);
  return angle < 0 ? angle + 360 : angle;
}
float nearest(float angle, float current, float maximum, bool bidirectional) {
  float best = -1, distance = 1e9;
  for (int oppositeDirection = 0; oppositeDirection < (bidirectional ? 2 : 1);
       ++oppositeDirection) {
    for (int turn = 0; turn < 2; ++turn) {
      float candidate = heading(angle + oppositeDirection * 180) + turn * 360;
      if (candidate <= maximum && std::abs(candidate - current) < distance) {
        best = candidate;
        distance = std::abs(candidate - current);
      }
    }
  }
  return best;
}
} // namespace forkrot
