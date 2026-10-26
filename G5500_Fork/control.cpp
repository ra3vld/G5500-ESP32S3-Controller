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

Controller::Controller() { configureHardware(hardware); }
void Controller::configureHardware(const HardwareConfig &config) {
  if (autoTune.active()) finishAutoTune(AutoTunePhase::Cancelled,"hardware_changed",lastSpeedUpdateAt_);
  autoTune.phase = AutoTunePhase::Idle;
  coast={};
  prepareMotionPlanner();
  hardware = config;
  hardware.releaseSimulationPins();
  if (!hardware.simulation && !remote && hardware.singleAxis() && (!FORK_OPTIONAL_DRIVE || hardware.localAxisConfigured(0))) { if(model==Model::G5500)model = Model::G800DXA; axes[1] = {}; }
  speeds = {};
  motionSettings = {};
  motion_ = {};
  speeds[1].slowdownDegrees = 2;
  speedEnabled = hardware.drive != DRIVE_YAESU;
  if (hardware.drive == DRIVE_YAESU) speeds[0] = {50, 25, 25, 2000, 1500, 10};
  if (!hardware.simulation) {
    speeds[0].coastDegrees = AZ_STOP_TOLERANCE_DEG;
    speeds[1].coastDegrees = EL_STOP_TOLERANCE_DEG;
  } else feedback(0, 0, true, true);
  for (unsigned i=0; i<4; ++i) presets[i] = {std::to_string(i*90)+"°", {float(i*90),0,true,true}};
}
Controller::Controller(const Controller &other) : Controller() { *this = other; }
Controller &Controller::operator=(const Controller &other) {
  hardware = other.hardware;
  configurationLocked = other.configurationLocked;
  setupRequired = other.setupRequired; setupAccess = false; setupMotionConfirmed = other.setupMotionConfirmed;
  coast=other.coast;
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
  azRange = other.azRange;
  azProfile = other.azProfile;
  elRange = other.elRange;
  elProfile = other.elProfile;
  rangeRevision = other.rangeRevision;
  axes = other.axes;
  for (unsigned i = 0; i < 4; ++i) {
    manualSessions_[i] = other.manualSessions_[i];
  }
  for (unsigned i = 0; i < 2; ++i) {
    manualScale_[i] = other.manualScale_[i];
    manualAbsolute_[i] = other.manualAbsolute_[i];
    remoteSpeedAvailable[i] = other.remoteSpeedAvailable[i];
    remoteSpeed[i] = other.remoteSpeed[i];
    remoteSpeedLimit[i] = other.remoteSpeedLimit[i];
  }
  runtimeSpeeds = other.runtimeSpeeds;
  webManualGesture_ = other.webManualGesture_;
  webManualOwner_ = other.webManualOwner_;
  presets = other.presets;
  pending = other.pending;
  lastTarget = other.lastTarget;
  pendingValid = other.pendingValid;
  presetActive = other.presetActive;
  targetKnown = other.targetKnown;
  presetSlot = other.presetSlot;
  speedEnabled = other.speedEnabled;
  speeds = other.speeds;
  motionSettings = other.motionSettings;
  motion_ = other.motion_;
  autoTune = other.autoTune;
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
  axes[1].current = model != Model::G5500 ? 0 : elevation;
  axes[1].calibrated = model == Model::G5500 && elCalibrated && std::isfinite(elevation);
}
bool Controller::valid(Target candidate) const {
  return (candidate.hasAz || candidate.hasEl) &&
         (!candidate.hasAz || azRange.contains(candidate.az)) &&
         (!candidate.hasEl ||
          (elRange.contains(candidate.el) &&
           (model == Model::G5500 || candidate.el == 0)));
}
Result Controller::start(Target nextTarget, uint32_t now, bool retryStall) {
  if (movementBlocked() && !autoTune.internal) return Result::Unavailable;
  if (autoTune.active() && !autoTune.internal) return Result::Busy;
  if (driveFault) return Result::Invalid;
  if (!valid(nextTarget)) {
    return Result::Invalid;
  }
  if ((nextTarget.hasAz && !axes[0].calibrated) ||
      (nextTarget.hasEl && model == Model::G5500 && !axes[1].calibrated)) {
    return Result::NotCalibrated;
  }
  if(!hardware.simulation && ((nextTarget.hasAz && !hardware.hasDirections(0)) ||
      (nextTarget.hasEl && model==Model::G5500 && !hardware.hasDirections(1)))) return Result::Unavailable;
  for (unsigned i=0; i<2; ++i) if ((i ? nextTarget.hasEl && model == Model::G5500 : nextTarget.hasAz) &&
      ( (axes[i].motionError != MotionError::None && !(retryStall && axes[i].motionError == MotionError::Stalled)) ||
        (variableSpeed(i) && !motionSettings[i].ready))) return Result::Unavailable;
  bool accepted = false;
  targetKnown = true;
  for (unsigned i = 0; i < 2; ++i) {
    if ((i == 0 ? nextTarget.hasAz : nextTarget.hasEl) && (i == 0 || model == Model::G5500)) {
      if (retryStall) recoverStall(i, now);
      Axis &axis = axes[i];
      const float value = i ? nextTarget.el : nextTarget.az;
      const bool unchanged = axis.accepted && value == axis.target &&
          (axis.tracking || axis.holdArmed || (axis.completed && std::abs(value-axis.current)<=tolerance(i)));
      if(!unchanged) {
        auto &r=motion_[i];
        r.arriving=r.settling=false;
        const unsigned interval=uint32_t(now-r.targetChangedAt);
        const bool streaming=r.targetTimed && (axis.tracking || axis.completed || axis.holdArmed) &&
            value!=axis.target && (!axis.direction || axis.direction*(value-axis.target)>0) && interval<=1800;
        if(!streaming) {r.targetIntervalMs=0;r.targetVelocity=0;}
        else if(!r.streamingTarget) {
          r.targetIntervalMs=std::clamp(interval,5u,1500u);
          r.targetVelocity=interval ? std::clamp((value-axis.target)*1000/interval,-runtimeSpeed(i),runtimeSpeed(i)) : 0;
          r.targetVelocityAt=now;r.targetVelocityPosition=value;
        } else {
          // Wi-Fi may deliver queued UDP packets in one burst. Their arrival
          // spacing is not the target's physical velocity, nor a new stream.
          const unsigned span=uint32_t(now-r.targetVelocityAt);
          if(span>=40) {
            const float velocity=std::clamp((value-r.targetVelocityPosition)*1000/span,-runtimeSpeed(i),runtimeSpeed(i));
            r.targetVelocity=.75f*r.targetVelocity+.25f*velocity;
            r.targetVelocityAt=now;r.targetVelocityPosition=value;
          }
          if(interval>=40) {
            const unsigned cadence=std::clamp(interval,100u,1500u);
            r.targetIntervalMs=cadence>=r.targetIntervalMs ? cadence : (3*r.targetIntervalMs+cadence)/4;
          }
        }
        r.streamingTarget=streaming;
        r.targetChangedAt=now;
        r.targetTimed=true;
        motion_[i].targetDirection=(value>axis.current)-(value<axis.current);
        motion_[i].crossedTarget=false;
      }
      accepted = true;
      axis.acceptedTarget = value;
      axis.accepted = true;
      axis.target = value;
      axis.targetKnown = true;
      manualScale_[i] = 1;
      manualAbsolute_[i] = false;
      if (unchanged) continue;
      ++motionEpoch[i];
      axis.completed = false;
      const bool cascade = variableSpeed(i) && motionSettings[i].mode == RegulatorMode::Cascade;
      axis.holdArmed = cascade && motionSettings[i].holdEnabled;
      const float difference = axis.target - axis.current;
      const int requested = std::abs(difference) <= tolerance(i) ? 0 : difference > 0 ? 1 : -1;
      if (!cascade) axis.wanted = requested;
      else if (!axis.direction) axis.wanted = requested;
      axis.tracking = cascade || requested != 0;
      axis.completed = !axis.tracking;
    }
  }
  if (accepted) {
    lastTarget = nextTarget;
    if (model != Model::G5500) lastTarget.hasEl = false;
    if(axes[0].motionError==MotionError::None && axes[1].motionError==MotionError::None) fault.clear();
  }
  return Result::Ok;
}
Result Controller::target(Target nextTarget, bool external, uint32_t now, bool followInManual) {
  if (autoTune.active() && !autoTune.internal) return Result::Busy;
  if (movementBlocked()) return Result::Unavailable;
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Target;
    c.target = nextTarget;
    c.external = external;
    c.followInManual = followInManual;
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
  if (external && mode == Mode::Manual && !followInManual) {
    if (!pendingValid) pending = {0,0,false,false};
    if (nextTarget.hasAz) { pending.az=nextTarget.az; pending.hasAz=true; }
    if (nextTarget.hasEl) { pending.el=nextTarget.el; pending.hasEl=true; }
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
  if (autoTune.active() && !autoTune.internal && direction) return Result::Busy;
  if (movementBlocked()) return Result::Unavailable;
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::Jog;
    c.axis = int(axis);
    c.direction = direction;
    c.external = external;
    return remote->submit(c);
  }
  expire(now);
  if (axis > 1 || (axis == 1 && model != Model::G5500) || direction < -1 || direction > 1) {
    return Result::Invalid;
  }
  if (direction == 0) {
    stop(axis);
    return Result::Ok;
  }
  if(!hardware.simulation && !hardware.hasDirections(axis)) return Result::Unavailable;
  if (external && presetActive) {
    return Result::Ignored;
  }
  cancelSequence();
  if (!external) {
    presetActive = false;
    presetSlot = -1;
  }
  if (driveFault) return Result::Invalid;
  if ((axes[axis].motionError != MotionError::None && axes[axis].motionError != MotionError::Stalled) ||
      (variableSpeed(axis) && !motionSettings[axis].ready)) return Result::Unavailable;
  if (variableSpeed(axis) && motionSettings[axis].mode == RegulatorMode::Cascade && !axes[axis].calibrated) return Result::NotCalibrated;
  recoverStall(axis, now);
  if (axes[axis].tracking || axes[axis].holdArmed) {
    // Taking over a running position command changes intent, not the current
    // velocity/acceleration boundary state or the load compensation integral.
    axes[axis].holdArmed=false;motion_[axis].positionPid.reset();motion_[axis].settling=false;
  }
  ++motionEpoch[axis];
  manualScale_[axis] = 1;
  manualAbsolute_[axis] = false;
  axes[axis].completed = false;
  axes[axis].accepted = false;
  axes[axis].tracking = false;
  axes[axis].wanted = direction;
  if(axes[0].motionError==MotionError::None && axes[1].motionError==MotionError::None) fault.clear();
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
  if (autoTune.active() && !autoTune.internal)
    finishAutoTune(AutoTunePhase::Cancelled,"stopped",lastSpeedUpdateAt_);
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
      cancelMotion(unsigned(i));
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
  cancelMotion(axis);
  output(axis, 0, now);
  return true;
}
Result Controller::execute(uint32_t now) {
  if (autoTune.active() && !autoTune.internal) return Result::Busy;
  if (movementBlocked()) return Result::Unavailable;
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
Target Controller::displayedTarget() const {
  Target t{axes[0].target, axes[1].target, axes[0].targetKnown,
           model == Model::G5500 && axes[1].targetKnown};
  if (pendingValid) {
    if (pending.hasAz) {
      t.az = pending.az;
      t.hasAz = true;
    }
    if (pending.hasEl && model == Model::G5500) {
      t.el = pending.el;
      t.hasEl = true;
    }
  }
  return t;
}
Result Controller::executeDisplayed(uint32_t now) {
  if (movementBlocked()) return Result::Unavailable;
  // An acknowledgement consumes this gesture; it must never also start a goal.
  if (hasDriveError()) return resetMotion(-1, now);
  if (remote) {
    if (!remote->manualAvailable()) {
      return pendingValid ? execute(now) : target(displayedTarget(), false, now);
    }
    ControlCommand c;
    c.op = ControlOp::ExecuteDisplayed;
    return remote->submit(c);
  }
  if (presetActive) {
    return Result::Ignored;
  }
  const auto t = displayedTarget();
  if (!valid(t)) {
    return Result::Invalid;
  }
  if (pendingValid) {
    pending = t;
    return execute(now); // Preserve a pending sequence and its progression.
  }
  return target(t, false, now);
}
void Controller::closeManual(ManualSession &s, uint32_t now) {
  for (unsigned i = 0; i < 2; ++i) {
    if (s.claimed & (1u << i)) {
      stopOwnedJog(i, s.epoch[i], now);
    }
  }
  s.closed = true;
}
Result Controller::manualDrive(uint64_t source, uint64_t sessionBoot, uint32_t gesture,
                               uint32_t serial, uint8_t mask, float az, float el, bool release,
                               uint32_t now, uint16_t inputLeaseMs, bool absoluteSpeed) {
  if (autoTune.active() && !autoTune.internal && !release) return Result::Busy;
  if (movementBlocked()) return Result::Unavailable;
  if (!gesture || !serial || mask > 3 || (!release && model != Model::G5500 && (mask & 2)) ||
      !std::isfinite(az) || !std::isfinite(el) || std::abs(az) > 1 || std::abs(el) > 1) {
    return Result::Invalid;
  }
  if (remote && inputLeaseMs <= 100) {
    if (!release) webManualOwner_=0;
    ControlCommand c;
    c.op = ControlOp::ManualDrive;
    c.gesture = gesture;
    c.axesMask = mask;
    c.manual[0] = az;
    c.manual[1] = el;
    c.release = release;
    c.inputLeaseMs = inputLeaseMs;
    c.absoluteSpeed = absoluteSpeed;
    return remote->submitAt(c, now);
  }
  ManualSession *s = nullptr, *free = nullptr;
  for (auto &candidate : manualSessions_) {
    if (candidate.used && candidate.source == source && candidate.boot == sessionBoot) {
      s = &candidate;
      break;
    }
    // Tombstones outlive the 2-second HUD command ticket.
    if (!candidate.used || (candidate.closed && uint32_t(now - candidate.renewed) >= 3000)) {
      free = &candidate;
    }
  }
  if (!s) {
    if (!free) {
      return Result::Busy;
    }
    s = free;
    *s = {};
    s->used = true;
    s->source = source;
    s->boot = sessionBoot;
  }
  if (s->gesture != gesture) {
    if (s->gesture && int32_t(gesture - s->gesture) <= 0) {
      return Result::Cancelled;
    }
    closeManual(*s, now);
    s->gesture = gesture;
    s->serial = 0;
    s->claimed = s->blocked = 0;
    s->closed = false;
    for (unsigned i = 0; i < 2; ++i) {
      s->epoch[i] = remote ? 0 : motionEpoch[i];
    }
  }
  if (s->serial && int32_t(serial - s->serial) <= 0) {
    return Result::Cancelled;
  }
  s->serial = serial;
  if (s->closed) {
    return Result::Cancelled;
  }
  if (s->claimed && uint32_t(now - s->renewed) >= 1000) {
    closeManual(*s, now);
    return Result::Cancelled;
  }
  s->renewed = now;
  if (remote) {
    // Web sources have independent serials/tombstones even when a HUD relays
    // them through its single manual-input stream. A reloaded page cannot
    // reuse an older page's logical gesture or reorder its release.
    if (!s->epoch[0] && (release || (az==0 && el==0))) {
      if(release)s->closed=true;
      return Result::Ok;
    }
    if (s->epoch[0] && s->epoch[0]!=webManualOwner_) {
      s->closed=true;return Result::Cancelled;
    }
    if (!s->epoch[0]) {
      if (!++webManualGesture_) ++webManualGesture_;
      s->epoch[0] = webManualOwner_ = webManualGesture_;
    }
    ControlCommand c;
    c.op=ControlOp::ManualDrive;c.gesture=s->epoch[0];c.axesMask=mask;
    c.manual[0]=az;c.manual[1]=el;c.release=release;c.inputLeaseMs=inputLeaseMs;c.absoluteSpeed=absoluteSpeed;
    s->claimed |= mask;
    if(release)s->closed=true;
    return remote->submitAt(c,now);
  }
  if (release) {
    closeManual(*s, now);
    return Result::Ok;
  }
  if (driveFault) {
    closeManual(*s, now);
    return Result::Invalid;
  }
  const float values[] = {az, el};
  for(unsigned i=0;i<2;++i) if((mask&(1u<<i)) && values[i]!=0 && !(s->blocked&(1u<<i))) {
    if(!hardware.simulation && !hardware.hasDirections(i)) { closeManual(*s,now);return Result::Unavailable; }
    const bool retryStall = !(s->claimed & (1u<<i)) && s->epoch[i] == motionEpoch[i];
    if((axes[i].motionError!=MotionError::None && !(retryStall && axes[i].motionError==MotionError::Stalled)) ||
       (variableSpeed(i) && !motionSettings[i].ready)) { closeManual(*s,now);return Result::Unavailable; }
    if(variableSpeed(i) && motionSettings[i].mode==RegulatorMode::Cascade && !axes[i].calibrated) { closeManual(*s,now);return Result::NotCalibrated; }
  }
  for (unsigned i = 0; i < 2; ++i) {
    const uint8_t bit = 1u << i;
    if (motionEpoch[i] != s->epoch[i]) {
      s->blocked |= bit;
    }
    if (!(mask & bit) || (s->blocked & bit)) {
      continue;
    }
    if (!(s->claimed & bit)) {
      if (!values[i]) {
        continue;
      }
      s->claimed |= bit;
      jog(i, values[i] > 0 ? 1 : -1, false, now);
      s->epoch[i] = motionEpoch[i];
    }
    const int direction = values[i] > 0 ? 1 : values[i] < 0 ? -1 : 0;
    if (!direction) {
      stopOwnedJog(i, s->epoch[i], now);
      s->epoch[i] = motionEpoch[i];
    } else {
      axes[i].wanted = direction;
      manualScale_[i] = std::abs(values[i]);
      manualAbsolute_[i] = absoluteSpeed;
    }
  }
  return Result::Ok;
}
Result Controller::setMode(Mode nextMode, uint32_t now) {
  if(movementBlocked() && (nextMode!=Mode::Manual || remote))return Result::Unavailable;
  if (autoTune.active()) finishAutoTune(AutoTunePhase::Cancelled,"mode_changed",now);
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
  if (!hardware.simulation && hardware.singleAxis() && (!FORK_OPTIONAL_DRIVE || hardware.localAxisConfigured(0)) && nextModel == Model::G5500) return;
  stop();
  model = nextModel;
  if (model != Model::G5500) {
    if (elProfile != EL_STANDARD || !(elRange == RotationRange{0,180})) {
      elRange = {0,180};elProfile = EL_STANDARD;
      if (!++rangeRevision) ++rangeRevision;
    }
    axes[1] = {};
  }
  mode = Mode::Manual;
  targetKnown = false;
  lastTarget = {0, 0, false, false};
  if (!driveFault) fault.clear();
  for (auto &axis : axes) {
    axis.calibrated = false;
    axis.hasRun = false;
    axis.accepted = false;
    axis.targetKnown = false;
    axis.measuredVelocity = axis.requestedVelocity = axis.profileAcceleration = 0;
  }
  motion_ = {};
}
void Controller::setMaximum(float value) {
  if (value == 360 || value == 450) {
    setRange({0, value}, value == 360 ? AZ_P36 : AZ_P45, lastSpeedUpdateAt_);
  }
}
bool Controller::setRange(AzRange range, uint8_t profile, uint32_t now) {
  if (remote || !validAzProfile(profile, range)) {
    return false;
  }
  if (azRange == range && azProfile == profile) {
    return true;
  }
  resetFeedback(0, now);
  azRange = range;
  azProfile = profile;
  if (!++rangeRevision) {
    ++rangeRevision;
  }
  axes[0].accepted = axes[0].targetKnown = false;
  axes[0].target = azRange.clamp(axes[0].current);
  if (hardware.simulation) axes[0].current = azRange.clamp(axes[0].current);
  lastTarget.hasAz = false;
  targetKnown = axes[1].targetKnown;
  return true;
}
bool Controller::setElRange(RotationRange next, uint8_t profile, uint32_t now) {
  if (remote || model != Model::G5500 || !validElProfile(profile, next)) return false;
  if (elRange == next && elProfile == profile) return true;
  resetFeedback(1, now);
  elRange = next;
  elProfile = profile;
  if (!++rangeRevision) ++rangeRevision;
  axes[1].accepted = axes[1].targetKnown = false;
  axes[1].target = elRange.clamp(axes[1].current);
  if (hardware.simulation) axes[1].current = elRange.clamp(axes[1].current);
  lastTarget.hasEl = false;
  targetKnown = axes[0].targetKnown;
  return true;
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
  if (autoTune.active() && !autoTune.internal) return Result::Busy;
  if (movementBlocked()) return Result::Unavailable;
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
Result Controller::sequence(const Target *points, size_t count, uint32_t interval, uint32_t now,
                            bool followInManual) {
  if (autoTune.active() && !autoTune.internal) return Result::Busy;
  if (movementBlocked()) return Result::Unavailable;
  if (remote) {
    if (!count || count > 16)
      return Result::Invalid;
    ControlCommand c;
    c.op = ControlOp::Sequence;
    c.followInManual = followInManual;
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
  if (mode == Mode::Manual && !followInManual) {
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
unsigned Controller::reverseDeadTimeMs(unsigned i) const {
  const auto &m=motionSettings[i];
  return m.mode==RegulatorMode::Cascade && m.holdEnabled && axes[i].holdArmed
      ? m.holdReverseMs : kReverseDeadTimeMs;
}
void Controller::output(unsigned axisIndex, int direction, uint32_t now) {
  auto &axis = axes[axisIndex];
  const unsigned deadTime=reverseDeadTimeMs(axisIndex);
  if (axis.direction != 0 && axis.direction != direction) {
    axis.previous = axis.direction;
    axis.direction = 0;
    axis.offAt = now;
  }
  if (direction != 0 && axis.direction == 0) {
    if (axis.previous && axis.previous != direction &&
        uint32_t(now - axis.offAt) < deadTime) {
      return;
    }
    axis.direction = direction;
    if (!axis.hasRun) {
      axis.runAt = now;
      axis.hasRun = true;
    }
  }
  if (direction == 0 && uint32_t(now - axis.offAt) >= deadTime) {
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
  if (hardware.simulation) return i == 0 || (i == 1 && model == Model::G5500);
  if (hardware.drive == DRIVE_YAESU) return i == 0 && model != Model::G5500 && speedEnabled && hardware.pins.azSpeedEnable>=0;
  return i == 0 || model == Model::G5500;
}
bool Controller::runtimeSpeedAvailable(unsigned i) const {
  if (i > 1 || (i == 1 && model != Model::G5500)) return false;
  if (remote) return remote->available() && remoteSpeedAvailable[i];
  if (!variableSpeed(i)) return false;
  if (hardware.simulation) return true;
  const auto &p = hardware.pins;
  const int enable = i ? p.elSpeedEnable : p.azSpeedEnable;
  if (enable < 0) return false;
  return hardware.drive != DRIVE_BTS7960 ||
      ((i ? p.elNegative : p.azNegative) >= 0 && (i ? p.elPositive : p.azPositive) >= 0);
}
float Controller::runtimeSpeedLimit(unsigned i) const {
  if (i > 1) return 0;
  if (remote) return remoteSpeedLimit[i];
  return motionSettings[i].mode == RegulatorMode::Cascade ? motionSettings[i].velocity :
      std::min(100.f, motionSettings[i].outputLimitPercent);
}
unsigned Controller::runtimeSpeedIndex(unsigned i) const {
  return ((((unsigned(hardware.simulation) * 3 + unsigned(hardware.drive)) * 2 +
      unsigned(model)) * 2 + i) * 2 + unsigned(motionSettings[i].mode));
}
void Controller::resetRuntimeSpeed(unsigned i, uint32_t now) {
  if (i > 1) return;
  const unsigned base = runtimeSpeedIndex(i) & ~1u;
  runtimeSpeeds.set(base, 0, now);
  runtimeSpeeds.set(base + 1, 0, now);
}
float Controller::runtimeSpeed(unsigned i) const {
  if (i > 1) return 0;
  if (remote) return remoteSpeed[i];
  const float saved = motionSettings[i].mode == RegulatorMode::Cascade ? motionSettings[i].velocity : float(speeds[i].speedPercent);
  const bool tuning = autoTune.active() && autoTune.axis == int(i);
  const float value = runtimeSpeeds.values[runtimeSpeedIndex(i)];
  const float limit = runtimeSpeedLimit(i);
  if (tuning) return std::min(saved,limit);
  return std::clamp(!tuning && value > 0 ? value : saved, limit / 20.f, limit);
}
Result Controller::setRuntimeSpeed(unsigned i, float value, uint32_t now) {
  if (i > 1 || !std::isfinite(value)) return Result::Invalid;
  if (movementBlocked() || autoTune.active() || !runtimeSpeedAvailable(i)) return Result::Unavailable;
  const float limit = runtimeSpeedLimit(i), minimum = limit / 20.f;
  // Status JSON and browser arithmetic can round the lower endpoint by one ULP.
  if (value < minimum - limit * 1e-6f || value > limit) return Result::Invalid;
  if (remote) {
    ControlCommand c; c.op = ControlOp::RuntimeSpeed; c.axis = int(i); c.speed = std::max(minimum,value);
    return remote->submitAt(c, now);
  }
  runtimeSpeeds.set(runtimeSpeedIndex(i), std::max(minimum,value), now);
  return Result::Ok;
}
Result Controller::startCoast(unsigned axis,int direction,float travel,uint32_t now) {
  if(movementBlocked() || remote || axis>1 || (axis==1&&model!=Model::G5500) || variableSpeed(axis) || hardware.simulation)return Result::Unavailable;
  if(coast.active() || autoTune.active() || moving() || axes[0].tracking || axes[1].tracking)return Result::Busy;
  const auto &a=axes[axis];const auto &f=motionFeedback(axis);
  if(!a.calibrated)return Result::NotCalibrated;
  if(hasDriveError() || !f.timed || !f.valid || uint32_t(now-f.at)>motionSettings[axis].feedbackTimeoutMs)return Result::Unavailable;
  const float reserve=std::max(5.f,speeds[axis].coastDegrees*2);
  if((direction!=1&&direction!=-1) || !std::isfinite(travel) || travel<5 || travel>90 ||
      a.current<range(axis).start+reserve || a.current>range(axis).end-reserve ||
      !range(axis).contains(a.current+direction*(travel+reserve)))return Result::Invalid;
  stopNow(-1,now);
  const auto result=jog(axis,direction,false,now);if(result!=Result::Ok)return result;
  if(coast.revision!=rangeRevision){coast.measured[0]=coast.measured[1]=0;}
  coast.phase=CoastMeasurement::Running;coast.axis=axis;coast.direction=direction;coast.origin=a.current;coast.travel=travel;
  coast.started=now;coast.revision=rangeRevision;coast.stopEpoch=stopEpoch;coast.settledAt=0;coast.reason="";
  if(!++coast.id)++coast.id;
  return Result::Ok;
}
void Controller::cancelCoast(uint32_t now) {
  if(coast.active()){coast.phase=CoastMeasurement::Cancelled;coast.reason="cancelled";stopNow(-1,now);}
}
Result Controller::applyCoast(unsigned axis,uint32_t now) {
  if(axis>1 || coast.phase!=CoastMeasurement::Complete || coast.measured[axis]!=3 || coast.revision!=rangeRevision || movementBlocked())return Result::Unavailable;
  const float proposal=std::max(coast.degrees[axis][0],coast.degrees[axis][1]);
  if(!std::isfinite(proposal) || proposal>std::min(90.f,range(axis).span()/10))return Result::Invalid;
  stopNow(-1,now);speeds[axis].coastDegrees=proposal;return Result::Ok;
}
void Controller::tickCoast(uint32_t now) {
  if(!coast.active())return;
  auto fail=[&](const char *reason){coast.phase=CoastMeasurement::Failed;coast.reason=reason;stopNow(-1,now);};
  const auto &a=axes[coast.axis];const auto &f=motionFeedback(coast.axis);const auto &m=motionSettings[coast.axis];
  if(configurationLocked || driveFault || !fault.empty() || coast.revision!=rangeRevision || stopEpoch!=coast.stopEpoch || !a.calibrated ||
      !f.valid || uint32_t(now-f.at)>m.feedbackTimeoutMs){fail("measurement_interrupted");return;}
  if(uint32_t(now-coast.started)>30000){fail("measurement_timeout");return;}
  if(a.current<=range(coast.axis).start+1 || a.current>=range(coast.axis).end-1){fail("range_margin");return;}
  if(coast.phase==CoastMeasurement::Running) {
    const float moved=(a.current-coast.origin)*coast.direction;
    if(uint32_t(now-coast.started)>m.stallMs && moved<.2f){fail("no_movement");return;}
    if(moved < -.5f){fail("wrong_direction");return;}
    if(moved>=coast.travel) {
      coast.cutPosition=a.current;coast.cutAt=now;stopNow(int(coast.axis),now);coast.stopEpoch=stopEpoch;
      coast.phase=CoastMeasurement::Coasting;
    }
  } else {
    if(std::abs(f.velocity)<=m.stoppedVelocity) {
      if(!coast.settledAt)coast.settledAt=now;
      if(uint32_t(now-coast.settledAt)>=m.settleMs) {
        const unsigned d=coast.direction>0?1:0;
        const float distance=(a.current-coast.cutPosition)*coast.direction;
        if(distance<-.5f){fail("wrong_direction");return;}
        coast.degrees[coast.axis][d]=std::max(0.f,distance);
        coast.seconds[coast.axis][d]=float(coast.settledAt-coast.cutAt)/1000;
        coast.measured[coast.axis]|=uint8_t(1u<<d);coast.phase=CoastMeasurement::Complete;
      }
    } else coast.settledAt=0;
  }
}
void Controller::tick(uint32_t now) {
  tickCoast(now);
  // HUD outputs are remote: the operation that locks configuration sends STOP once.
  if (configurationLocked) { if (!remote) stopNow(-1, now); return; }
  if (remote)
    return;
  for (auto &s : manualSessions_) {
    if (s.used && !s.closed && uint32_t(now - s.renewed) >= 1000) {
      closeManual(s, now);
    }
  }
  expire(now);
  const uint32_t elapsed = speedUpdateStarted_ ? uint32_t(now - lastSpeedUpdateAt_) : 0;
  lastSpeedUpdateAt_ = now;
  speedUpdateStarted_ = true;
  if (hardware.simulation) {
  // Integrate the previous output over real elapsed time, independent of loop rate.
  // Motor GPIO are disabled; all protocols share these simulated positions.
  for (unsigned i = 0; i < 2; ++i) {
    auto &axis = axes[i];
    if (!driveFault && axis.direction && (i == 0 || model == Model::G5500)) {
      float step = 13.4f * (axis.speedOutputPercent / 100.f) * (float(elapsed) / 1000.f);
      const float distance = axis.target - axis.current;
      if (axis.tracking && distance * axis.direction >= 0 && step >= std::abs(distance))
        axis.current = axis.target;
      else {
        axis.current = std::clamp(axis.current + axis.direction * step, range(i).start, range(i).end);
      }
    }
  }
  }
  if (hardware.simulation) for (unsigned i=0; i<2; ++i)
    if (i==0 || model==Model::G5500) feedbackAt(i,axes[i].current,true,now);
  if (!sequence_.empty() && uint32_t(now - sequenceAdvancedAt_) >= sequenceIntervalMs_) {
    if (++sequenceIndex_ >= sequence_.size()) {
      sequence_.clear();
    } else {
      auto result = start(sequence_[sequenceIndex_], now, false);
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
    if (i == 1 && model != Model::G5500) {
      axis.wanted = 0;
      axis.tracking = false;
    }
    if (axis.tracking &&
        (!axis.calibrated || ((motionSettings[i].mode != RegulatorMode::Cascade || !variableSpeed(i)) && (
         (axis.wanted > 0 && axis.current >= axis.target - tolerance(i)) ||
         (axis.wanted < 0 && axis.current <= axis.target + tolerance(i)))))) {
      axis.completed = axis.calibrated;
      axis.wanted = 0;
      axis.tracking = false;
    }
    if (axis.calibrated) {
      const auto outward = [&](int dir) {
        return (dir < 0 && axis.current <= range(i).start) ||
               (dir > 0 && axis.current >= range(i).end);
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
    if (model != Model::G5500 && axis.hasRun &&
        uint32_t(now - axis.runAt) >= kContinuousMotionTimeoutMs) {
      stop();
      fault = "continuous_motion_timeout";
    }
    if (!axis.calibrated && axis.holdArmed) { motionFault(i,MotionError::FeedbackInvalid,now); continue; }
    if (this->variableSpeed(i)) tickMotion(i,now);
    else {
      cancelMotion(i);
      output(i,axis.wanted,now);
      axis.speedOutputPercent=0;
    }
  }
  if (presetActive && !moving() && !axes[0].tracking && !axes[1].tracking) {
    presetActive = false;
    presetSlot = -1;
  }
  tickAutoTune(now);
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
float nearest(float angle, float current, AzRange range, bool bidirectional) {
  if (!std::isfinite(angle) || !std::isfinite(current) || !range.valid()) {
    return NAN;
  }
  float best = NAN, distance = INFINITY;
  for (int opposite = 0; opposite < (bidirectional ? 2 : 1); ++opposite) {
    const double base = heading(angle + opposite * 180);
    const int first = int(std::ceil((double(range.start) - base - .001) / 360));
    const int last = int(std::floor((double(range.end) - base + .001) / 360));
    if (first > last) {
      continue;
    }
    const int near = std::clamp(int(std::clamp(std::floor((double(current) - base) / 360), double(first), double(last))), first, last);
    for (int k : {near, std::min(near + 1, last)}) {
      const float candidate = range.clamp(float(base + k * 360));
      const float d = std::abs(candidate - current);
      if (d < distance || (d == distance && candidate < best)) {
        best = candidate;
        distance = d;
      }
    }
  }
  return best;
}
float nearest(float angle, float current, float maximum, bool bidirectional) {
  return nearest(angle, current, AzRange{0, maximum}, bidirectional);
}
} // namespace forkrot
