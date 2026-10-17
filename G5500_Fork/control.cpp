// G5500 Fork; original by UA1CFM. GPL-3.0.
// G5500 Fork, GPL-3.0; shared by the ESP32 firmware and host simulator.
#include "control.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
namespace forkrot {
namespace {
constexpr uint32_t kAutoHourDurationMs = 3600000;
constexpr uint32_t kReverseDeadTimeMs = 300;
constexpr uint32_t kContinuousMotionTimeoutMs = 180000;
constexpr uint32_t kMinimumSequenceIntervalMs = 1000;
constexpr uint32_t kMaximumSequenceIntervalMs = 999000;

} // namespace

Controller::Controller() {
#if (SELECTED_BOARD == BOARD_CROWPANEL_21 && CROWPANEL_SIMULATION)
  feedback(0, 0, true, true);
#endif
  speeds[1].slowdownDegrees = 2;
#if DRIVE_INTERFACE == DRIVE_YAESU
  speeds[0] = {50, 25, 25, 2000, 1500, 10};
#endif
  for (unsigned i = 0; i < 4; ++i) {
    presets[i] = {std::to_string(i * 90) + "°", {float(i * 90), 0, true, true}};
  }
}
Controller::Controller(const Controller &other) : Controller() { *this = other; }
Controller &Controller::operator=(const Controller &other) {
  remote = other.remote;
  for (unsigned i = 0; i < 2; ++i) {
    motionEpoch[i] = other.motionEpoch[i];
    remoteVariable[i] = other.remoteVariable[i];
    remoteTolerance[i] = other.remoteTolerance[i];
  }
  snapshotAt = other.snapshotAt;
  remoteRemaining = other.remoteRemaining;
  remoteSequenceIndex = other.remoteSequenceIndex;
  remoteSequenceCount = other.remoteSequenceCount;
  model = other.model;
  mode = other.mode;
  maximum = other.maximum;
  axes = other.axes;
  presets = other.presets;
  pending = other.pending;
  lastTarget = other.lastTarget;
  pendingValid = other.pendingValid;
  presetActive = other.presetActive;
  targetKnown = other.targetKnown;
  presetSlot = other.presetSlot;
  speedEnabled = other.speedEnabled;
  speeds = other.speeds;
  driveFault = other.driveFault;
  stopEpoch = other.stopEpoch;
  fault = other.fault;
  autoHourStartedAt_ = other.autoHourStartedAt_;
  sequenceAdvancedAt_ = other.sequenceAdvancedAt_;
  sequenceIntervalMs_ = other.sequenceIntervalMs_;
  lastSpeedUpdateAt_ = other.lastSpeedUpdateAt_;
  speedUpdateStarted_ = other.speedUpdateStarted_;
  sequence_ = other.sequence_;
  pendingSequence_ = other.pendingSequence_;
  sequenceIndex_ = other.sequenceIndex_;
  return *this;
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
  case Result::Deferred:
    return "deferred";
  case Result::Unavailable:
    return "unavailable";
  case Result::Busy:
    return "busy";
  case Result::Timeout:
    return "timeout";
  case Result::Cancelled:
    return "cancelled";
  case Result::NotCalibrated:
    return "not_calibrated";
  default:
    return "invalid";
  }
}
void Controller::feedback(float azimuth, float elevation, bool azCalibrated, bool elCalibrated) {
  if (remote)
    return;
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
Result Controller::start(Target nextTarget, uint32_t, bool external) {
  if (driveFault) return Result::Invalid;
  if (!valid(nextTarget)) {
    return Result::Invalid;
  }
  if ((nextTarget.hasAz && !axes[0].calibrated) ||
      (nextTarget.hasEl && model == Model::G5500 && !axes[1].calibrated)) {
    return Result::NotCalibrated;
  }
  bool accepted = false;
  targetKnown = true;
  for (unsigned i = 0; i < 2; ++i) {
    if ((i == 0 ? nextTarget.hasAz : nextTarget.hasEl) && (i == 0 || model == Model::G5500)) {
      Axis &axis = axes[i];
      const float value = i ? nextTarget.el : nextTarget.az;
      if (external && axis.accepted && std::abs(value - axis.acceptedTarget) <= tolerance(i)) continue;
      accepted = true;
      ++motionEpoch[i];
      axis.acceptedTarget = value;
      axis.accepted = true;
      axis.completed = false;
      axis.target = value;
      float difference = axis.target - axis.current;
      axis.wanted = std::abs(difference) <= tolerance(i) ? 0 : difference > 0 ? 1 : -1;
      axis.tracking = axis.wanted != 0;
      axis.completed = !axis.tracking;
    }
  }
  if (accepted) {
    lastTarget = nextTarget;
    if (model == Model::G800DXA) lastTarget.hasEl = false;
    fault.clear();
  }
  return Result::Ok;
}
Result Controller::target(Target nextTarget, bool external, uint32_t now) {
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Target;
    c.target = nextTarget;
    c.external = external;
    return remote->submit(c);
  }
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
  auto result = start(nextTarget, now, external);
  if (result == Result::Ok) {
    pendingValid = false;
  }
  return result;
}
Result Controller::jog(unsigned axis, int direction, bool external, uint32_t now) {
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Jog;
    c.axis = int(axis);
    c.direction = direction;
    c.external = external;
    return remote->submit(c);
  }
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
  if (driveFault) return Result::Invalid;
  ++motionEpoch[axis];
  axes[axis].completed = false;
  axes[axis].accepted = false;
  axes[axis].tracking = false;
  axes[axis].wanted = direction;
  fault.clear();
  return Result::Ok;
}
void Controller::stop(int axis) {
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Stop;
    c.axis = axis;
    remote->submit(c);
    return;
  }
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
      ++motionEpoch[i];
      axes[i].wanted = 0;
      axes[i].tracking = false;
      axes[i].completed = false;
      axes[i].speedOutputPercent = 0;
    }
  }
}
void Controller::stopNow(int axis, uint32_t now) {
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Stop;
    c.axis = axis;
    remote->submit(c);
    return;
  }
  if (axis < -1 || axis > 1) return;
  stop(axis);
  // Apply STOP without running unrelated tick paths (which can set a fault
  // string). Preserve output()'s reverse dead-time and previous direction.
  for (unsigned i = 0; i < 2; ++i) if (axis < 0 || unsigned(axis) == i) output(i, 0, now);
}
bool Controller::stopOwnedJog(unsigned axis, uint32_t epoch, uint32_t now) {
  if (remote || axis > 1 || motionEpoch[axis] != epoch || axes[axis].tracking)
    return false;
  ++motionEpoch[axis];
  axes[axis].wanted = 0;
  axes[axis].speedOutputPercent = 0;
  output(axis, 0, now);
  return true;
}
Result Controller::execute(uint32_t now) {
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Execute;
    return remote->submit(c);
  }
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
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Mode;
    c.mode = nextMode;
    return remote->submit(c);
  }
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
  if (remote)
    return;
  mode = savedMode == Mode::Auto ? Mode::Auto : Mode::Manual;
}
void Controller::setModel(Model nextModel) {
  if (remote)
    return;
  stop();
  model = nextModel;
  mode = Mode::Manual;
  targetKnown = false;
  lastTarget = {0, 0, false, false};
  if (!driveFault) fault.clear();
  for (auto &axis : axes) {
    axis.calibrated = false;
    axis.hasRun = false;
    axis.accepted = false;
  }
}
void Controller::setMaximum(float nextMaximum) {
  if (remote)
    return;
  if (nextMaximum != 360 && nextMaximum != 450) {
    return;
  }
  if (nextMaximum == maximum) return;
  stop(0);
  maximum = nextMaximum;
  axes[0].accepted = false;
  axes[0].target = std::max(0.f, std::min(maximum, axes[0].current));
  lastTarget.hasAz = false;
}
Result Controller::savePreset(unsigned slot, const std::string &label, Target newTarget) {
  if (remote) {
    if (label.empty() || label.size() > 48)
      return Result::Invalid;
    ControlCommand c;
    c.op = ControlOp::SavePreset;
    c.slot = slot;
    c.target = newTarget;
    std::memcpy(c.label, label.data(), label.size());
    return remote->submit(c);
  }
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
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Preset;
    c.slot = slot;
    return remote->submit(c);
  }
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
Result Controller::sequence(const Target *points, size_t count, uint32_t interval, uint32_t now) {
  if (remote) {
    if (!count || count > 16)
      return Result::Invalid;
    ControlCommand c;
    c.op = ControlOp::Sequence;
    c.count = uint8_t(count);
    c.interval = interval;
    std::copy_n(points, count, c.points.begin());
    return remote->submit(c);
  }
  expire(now);
  if (!count || count > 16 || interval < kMinimumSequenceIntervalMs ||
      interval > kMaximumSequenceIntervalMs) {
    return Result::Invalid;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!valid(points[i])) {
      return Result::Invalid;
    }
  }
  if (presetActive) {
    return Result::Ignored;
  }
  cancelSequence();
  sequenceIntervalMs_ = interval;
  if (mode == Mode::Manual) {
    pendingSequence_.assign(points, count);
    pending = points[0];
    pendingValid = true;
    return Result::Pending;
  }
  auto result = start(points[0], now);
  if (result == Result::Ok) {
    sequence_.assign(points, count);
    sequenceIndex_ = 0;
    sequenceAdvancedAt_ = now;
    pendingValid = false;
  }
  return result;
}
void Controller::cancelSequence() {
  if (remote)
    return;
  sequence_.clear();
  pendingSequence_.clear();
}
uint32_t Controller::remaining(uint32_t now) const {
  if (remote)
    return remoteRemaining > uint32_t(now - snapshotAt) ? remoteRemaining - uint32_t(now - snapshotAt) : 0;
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
bool Controller::variableSpeed(unsigned i) const {
  if (remote)
    return i < 2 && remoteVariable[i];
#if (SELECTED_BOARD == BOARD_CROWPANEL_21 && CROWPANEL_SIMULATION)
  (void)i;
  return false; // Fixed-speed virtual drive, independent of physical PWM settings.
#elif DRIVE_INTERFACE == DRIVE_YAESU
  return i == 0 && model == Model::G800DXA && speedEnabled;
#else
  return i == 0 || model == Model::G5500;
#endif
}
void Controller::tick(uint32_t now) {
  if (remote)
    return;
  expire(now);
  const uint32_t elapsed = speedUpdateStarted_ ? uint32_t(now - lastSpeedUpdateAt_) : 0;
  lastSpeedUpdateAt_ = now;
  speedUpdateStarted_ = true;
#if (SELECTED_BOARD == BOARD_CROWPANEL_21 && CROWPANEL_SIMULATION)
  // Integrate the previous output over real elapsed time, independent of loop rate.
  // Motor GPIO are disabled; all protocols share these simulated positions.
  for (unsigned i = 0; i < 2; ++i) {
    auto &axis = axes[i];
    if (!driveFault && axis.direction && (i == 0 || model == Model::G5500)) {
      float step = 13.4f * (float(elapsed) / 1000.f);
      const float distance = axis.target - axis.current;
      if (axis.tracking && distance * axis.direction >= 0 && step >= std::abs(distance))
        axis.current = axis.target;
      else axis.current = std::clamp(axis.current + axis.direction * step, 0.f, i ? 180.f : maximum);
    }
  }
#endif
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
    if (driveFault) { axis.wanted = 0; axis.tracking = false; }
    if (i == 1 && model == Model::G800DXA) {
      axis.wanted = 0;
      axis.tracking = false;
    }
    if (axis.tracking &&
        (!axis.calibrated ||
         (axis.wanted > 0 && axis.current >= axis.target - tolerance(i)) ||
         (axis.wanted < 0 && axis.current <= axis.target + tolerance(i)))) {
      axis.completed = axis.calibrated;
      axis.wanted = 0;
      axis.tracking = false;
    }
    if (axis.calibrated) {
      const auto outward = [&](int dir) {
        return (dir < 0 && axis.current <= 0) || (dir > 0 && axis.current >= (i ? 180 : maximum));
      };
      if (outward(axis.wanted)) {
        axis.wanted = 0;
        axis.tracking = false;
      }
      if (outward(axis.direction)) {
        // The old output must stop at a limit, while a new inward target remains armed.
        output(i, 0, now);
        axis.speedOutputPercent = 0;
      }
    }
    if (model == Model::G800DXA && axis.hasRun &&
        uint32_t(now - axis.runAt) >= kContinuousMotionTimeoutMs) {
      stop();
      fault = "continuous_motion_timeout";
    }
    const bool variableSpeed = this->variableSpeed(i);
    const auto &speed = speeds[i];
    if (variableSpeed && axis.direction && axis.wanted && axis.direction != axis.wanted) {
      // Brake in the old direction, then let output() enforce the 300 ms interlock.
      axis.speedOutputPercent =
          std::max(0.f, axis.speedOutputPercent -
                            speed.speedPercent * float(elapsed) / std::max(1u, speed.decelerationMs));
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
    float requested = float(speed.speedPercent);
    if (axis.tracking) {
      const float distance = std::abs(axis.target - axis.current);
      const float fraction = std::max(0.f, std::min(1.f, (distance - tolerance(i)) /
                                                             std::max(1u, speed.slowdownDegrees)));
      const float approach = float(std::min(speed.approachSpeedPercent, speed.speedPercent));
      requested = approach + (speed.speedPercent - approach) * fraction;
    }
    if (!oldDirection) {
      axis.speedOutputPercent = std::min(float(speed.startSpeedPercent), requested);
    } else if (requested > axis.speedOutputPercent) {
      axis.speedOutputPercent =
          std::min(requested, axis.speedOutputPercent +
                                  speed.speedPercent * float(elapsed) / std::max(1u, speed.accelerationMs));
    } else {
      axis.speedOutputPercent =
          std::max(requested, axis.speedOutputPercent -
                                  speed.speedPercent * float(elapsed) / std::max(1u, speed.decelerationMs));
    }
  }
  if (presetActive && !moving()) {
    presetActive = false;
    presetSlot = -1;
  }
}

bool number(std::string_view text, float &result) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) text.remove_suffix(1);
  if (!text.empty() && text.front() == '+') {
    text.remove_prefix(1);
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) return false;
  }
  // Bounded decimal input, including the decimal comma used by R4UAB.
  if (text.empty() || text.size() > 63) return false;
  char value[64];
  for (size_t i = 0; i < text.size(); ++i) value[i] = text[i] == ',' ? '.' : text[i];
  const auto parsed = std::from_chars(value, value + text.size(), result);
  return parsed.ec == std::errc() && parsed.ptr == value + text.size() && std::isfinite(result);
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
