// GPL-3.0. Ruckig's separately licensed local core is under src/ruckig/.
#include "control.hpp"
#include "src/ruckig/ruckig.hpp"
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
namespace forkrot {
const char *regulatorName(RegulatorMode m) { return m == RegulatorMode::Cascade ? "cascade" : "simple"; }
const char *motionPhaseName(MotionPhase p) {
  static constexpr const char *names[] = {"idle", "kick", "run", "braking", "holding", "fault"};
  return unsigned(p) < std::size(names) ? names[unsigned(p)] : "fault";
}
const char *motionErrorName(MotionError e) {
  static constexpr const char *names[] = {"", "kick_start_timeout", "feedback_stale", "feedback_invalid", "stalled", "planner_error"};
  return unsigned(e) < std::size(names) ? names[unsigned(e)] : "planner_error";
}
bool MotionSettings::valid() const {
  if (mode > RegulatorMode::Cascade || holdResumeDegrees < positionTolerance ||
      (kickEnabled && kickPercent > outputLimitPercent) ||
      (mode==RegulatorMode::Cascade && sustainingPercent > outputLimitPercent) ||
      feedbackTimeoutMs < velocityWindowMs) return false;
  for (const auto &f : motionRealFields) if (!std::isfinite(this->*f.member) || this->*f.member < f.lo || this->*f.member > f.hi) return false;
  for (const auto &f : motionIntFields) if (this->*f.member < f.lo || this->*f.member > f.hi) return false;
  return true;
}
bool MotionSettings::operator==(const MotionSettings &o) const {
  if (mode != o.mode || ready != o.ready || kickEnabled != o.kickEnabled || holdEnabled != o.holdEnabled) return false;
  for (const auto &f : motionRealFields) if (this->*f.member != o.*f.member) return false;
  for (const auto &f : motionIntFields) if (this->*f.member != o.*f.member) return false;
  return true;
}
namespace {
struct PlannerScratch {
  ruckig::Ruckig<1> solver;
  ruckig::InputParameter<1> input;
  ruckig::Trajectory<1> trajectory;
};
std::unique_ptr<PlannerScratch> &sharedPlanner() {
  static std::unique_ptr<PlannerScratch> value;
  return value;
}
uint32_t checksum(const uint8_t *b, size_t n) {
  uint32_t c = 2166136261u;
  for (size_t i = 0; i < n; ++i) c = (c ^ b[i]) * 16777619u;
  return c;
}
// Scratch belongs to the single controller thread. Each axis owns only its
// numerical boundary state, so Controller/HUD copies do not copy a solver or
// allocate. Recalculate from that state, never from zero, on every control step.
bool profileStep(bool positionControl, float &p, float &v, float &a, float target,
                 float velocity, float acceleration, float deceleration, float jerk, float dt) {
  auto &scratch=sharedPlanner();
  if(!scratch) return false;
  auto &solver=scratch->solver;auto &input=scratch->input;auto &trajectory=scratch->trajectory;
  input.control_interface = positionControl ? ruckig::ControlInterface::Position : ruckig::ControlInterface::Velocity;
  input.current_position = {p}; input.current_velocity = {v}; input.current_acceleration = {a};
  input.target_position = {positionControl ? target : 0};
  input.target_velocity = {positionControl ? 0 : target}; input.target_acceleration = {0};
  input.max_velocity = {velocity};
  // Acceleration/deceleration are mechanical magnitudes in either direction.
  const float positive = v < 0 ? deceleration : acceleration;
  const float negative = v < 0 ? acceleration : deceleration;
  input.max_acceleration = {positive}; input.min_acceleration = std::array<double, 1>{-negative};
  input.max_jerk = {jerk};
  if (solver.calculate(input, trajectory) < 0) return false;
  std::array<double, 1> np{}, nv{}, na{};
  trajectory.at_time(std::min(double(dt), trajectory.get_duration()), np, nv, na);
  p = float(np[0]); v = float(nv[0]); a = float(na[0]);
  return std::isfinite(p) && std::isfinite(v) && std::isfinite(a);
}
int sign(float n) { return (n > 0) - (n < 0); }
}
bool prepareMotionPlanner() {
  auto &scratch=sharedPlanner();
  if(!scratch) scratch.reset(new(std::nothrow) PlannerScratch{});
  return bool(scratch);
}
MotionRecord encodeMotionSettings(const MotionSettings &s) {
  MotionRecord b{}; b[0] = 'M'; b[1] = 'O'; b[2] = s.sustainingPercent ? 2 : 1; b[3] = uint8_t(s.mode);
  b[4] = uint8_t(s.ready) | uint8_t(s.kickEnabled) << 1 | uint8_t(s.holdEnabled) << 2;
  b[5] = uint8_t(s.sustainingPercent);
  size_t at = 8;
  auto put = [&](uint32_t n) { for (unsigned i=0; i<4; ++i) b[at++] = uint8_t(n >> (8*i)); };
  for (const auto &f : motionRealFields) { uint32_t n; const float value = s.*f.member; std::memcpy(&n, &value, 4); put(n); }
  for (const auto &f : motionIntFields) if(f.member!=&MotionSettings::sustainingPercent) put(s.*f.member);
  put(checksum(b.data(), b.size()-4));
  return b;
}
bool decodeMotionSettings(const MotionRecord &b, MotionSettings &s) {
  if (b[0] != 'M' || b[1] != 'O' || (b[2] != 1 && b[2] != 2) || b[3] > 1 || b[4] > 7 ||
      (b[2]==1 && b[5]) || b[6] || b[7]) return false;
  MotionSettings next; next.mode = RegulatorMode(b[3]); next.ready = b[4]&1; next.kickEnabled = b[4]&2; next.holdEnabled = b[4]&4;
  next.sustainingPercent=b[2]==2 ? b[5] : 0;
  size_t at = 8;
  auto get = [&]() { uint32_t n=0; for (unsigned i=0; i<4; ++i) n |= uint32_t(b[at++]) << (8*i); return n; };
  for (const auto &f : motionRealFields) { const uint32_t n = get(); std::memcpy(&(next.*f.member), &n, 4); }
  for (const auto &f : motionIntFields) if(f.member!=&MotionSettings::sustainingPercent) next.*f.member = get();
  if (get() != checksum(b.data(), b.size()-4) || !next.valid()) return false;
  // Old records may contain the removed user commissioning checkbox.
  next.ready = true; s = next; return true;
}
float MotionPid::step(float error, float measurement, float ff, float kp, float ki, float kd,
                      float dt, float filter, float lo, float hi, bool integrate) {
  if (initialized) derivative += dt / (filter + dt) * ((measurement-lastMeasurement)/dt - derivative);
  else initialized = true;
  lastMeasurement = measurement;
  const float limit = std::max(std::abs(lo), std::abs(hi));
  const float candidate = std::clamp(integral + (integrate ? ki*error*dt : 0), -limit, limit);
  const float raw = ff + kp*error + candidate - kd*derivative;
  if ((raw >= lo && raw <= hi) || (raw > hi && error < 0) || (raw < lo && error > 0)) integral = candidate;
  // Conditional integration freezes the state, not the commanded output.
  // Returning the old integral here can strand PWM just below the limit.
  return std::clamp(raw, lo, hi);
}
void MotionFeedback::sample(float position, bool calibrated, uint32_t stamp, unsigned window) {
  const bool wasValid=valid;
  valid = calibrated && std::isfinite(position);
  if(!valid) {
    // A fresh ADC conversion is still fresh before calibration, but cannot
    // contribute a velocity. This keeps simple calibration jog available.
    if(std::isfinite(position)) at=stamp;
    count=next=0;velocity=0;coastSeconds=0;powered=coastTracking=false;return;
  }
  if(!wasValid) { count=next=0;velocity=0; }
  if (count && uint32_t(stamp-at) < 20) return;
  if (count && uint32_t(stamp-at) > 5000) { count = 0; next = 0; velocity = 0; }
  at = stamp;
  samples[next] = {position, stamp}; next = (next+1)%samples.size(); count = std::min(count+1, unsigned(samples.size()));
  double sx=0, sy=0, sxx=0, sxy=0; unsigned n=0;
  for (unsigned i=0; i<count; ++i) {
    const uint32_t age = uint32_t(stamp-samples[i].at);
    if (age > window) continue;
    const double x = -double(age)/1000, y = double(samples[i].position)-position;
    sx+=x; sy+=y; sxx+=x*x; sxy+=x*y; ++n;
  }
  const double denominator = n*sxx-sx*sx;
  if (n >= 2 && denominator > 1e-10) velocity = float((n*sxy-sx*sy)/denominator);
  else velocity=0;
}
void MotionRuntime::cancel() {
  const auto saved = feedback;
  *this = {}; feedback = saved;
}
bool MotionFeedback::progressed(int direction, float degrees, unsigned window) const {
  if(!valid || count<2 || !direction) return false;
  const auto &latest=samples[(next+samples.size()-1)%samples.size()];
  const Sample *oldest=&latest;
  uint32_t oldestAge=0;
  for(unsigned i=0;i<count;++i) {
    const uint32_t age=uint32_t(at-samples[i].at);
    if(age<=window && age>oldestAge) {oldest=&samples[i];oldestAge=age;}
  }
  return oldestAge>0 && direction*(latest.position-oldest->position)>=degrees;
}
void Controller::cancelMotion(unsigned i) {
  motion_[i].cancel();
  axes[i].holdArmed = false;
  axes[i].requestedVelocity = axes[i].profileAcceleration = 0;
  axes[i].motionPhase = axes[i].motionError == MotionError::None ? MotionPhase::Idle : MotionPhase::Fault;
}
void Controller::resetFeedback(int axis,uint32_t now) {
  if(remote || axis < -1 || axis > 1) return;
  if (autoTune.active()) finishAutoTune(AutoTunePhase::Cancelled,"calibration_changed",now);
  if (autoTune.phase == AutoTunePhase::Complete) { autoTune.phase=AutoTunePhase::Cancelled; autoTune.reason="calibration_changed"; }
  stopNow(axis,now);
  for(unsigned i=0;i<2;++i) if(axis<0 || unsigned(axis)==i) {
    motion_[i].feedback={};axes[i].measuredVelocity=0;
  }
}
void Controller::feedbackAt(unsigned i, float position, bool calibrated, uint32_t stamp) {
  if (remote || i > 1 || (i == 1 && model == Model::G800DXA)) return;
  auto &f = motion_[i].feedback;
  const auto &cfg = motionSettings[i];
  const float previousPosition=axes[i].current,previousVelocity=f.velocity;
  const uint32_t previousStamp=f.at;
  const bool wasCalibrated=axes[i].calibrated;
  if (f.count && stamp != f.at && calibrated && axes[i].calibrated) {
    const float dt = float(uint32_t(stamp-f.at))/1000;
    // Feedback plausibility is independent of the requested profile speed:
    // simple PWM drives and disturbances can exceed a cascade's set velocity.
    // Allow the supported 360 deg/s physical velocity range plus quantization.
    if (dt < .5f && std::abs(position-axes[i].current) > std::max(2.f, 360.f*dt)) {
      // A finite outlier is not a loss of mechanical calibration. Ignore it
      // without refreshing feedback age. Only distinct ADC conversions count;
      // repeated status reads must not turn one spike into a latched fault.
      if (!f.rejectedSamples || stamp != f.rejectedAt) { ++f.rejectedSamples; f.rejectedAt=stamp; }
      if (f.rejectedSamples < 3) return;
      calibrated=false;
    } else f.rejectedSamples=0;
  }
  axes[i].current = std::isfinite(position) ? position : axes[i].current;
  axes[i].calibrated = calibrated && std::isfinite(position);
  f.timed = true; f.sample(position, axes[i].calibrated, stamp, cfg.velocityWindowMs);
  axes[i].measuredVelocity = f.velocity;
  if(f.valid && f.at==stamp && stamp!=previousStamp) {
    const bool powered=axes[i].direction && axes[i].speedOutputPercent>0;
    if(f.powered && !powered && std::abs(previousVelocity)>cfg.stoppedVelocity) {
      f.coastPosition=previousPosition;f.coastVelocity=std::abs(previousVelocity);
      f.coastDirection=sign(previousVelocity);f.coastAt=stamp;f.coastTracking=true;
    }
    if(powered)f.coastTracking=false;
    else if(f.coastTracking && uint32_t(stamp-f.coastAt)>=80 && std::abs(f.velocity)<=cfg.stoppedVelocity) {
      const float travel=f.coastDirection*(position-f.coastPosition);
      if(travel>cfg.movementDegrees/2 && f.coastVelocity>.001f) {
        const float seconds=std::clamp(travel/f.coastVelocity,.02f,2.f);
        f.coastSeconds=f.coastSeconds>0 ? .5f*(f.coastSeconds+seconds) : seconds;
      }
      f.coastTracking=false;
    }
    f.powered=powered;
  }
  const bool active=axes[i].wanted || axes[i].direction || axes[i].tracking || axes[i].holdArmed;
  if (!axes[i].calibrated && active && variableSpeed(i) &&
      (wasCalibrated || axes[i].tracking || axes[i].holdArmed || !std::isfinite(position)))
    motionFault(i, MotionError::FeedbackInvalid, stamp);
}
bool Controller::configureMotion(unsigned i, const MotionSettings &settings, uint32_t now) {
  if (remote || i > 1 || !settings.valid() || (settings.ready && !prepareMotionPlanner())) return false;
  if (motionSettings[i] != settings) { stopNow(int(i), now); motionSettings[i] = settings; resetRuntimeSpeed(i,now); }
  return true;
}
Result Controller::resetMotion(int axis, uint32_t now) {
  if (configurationLocked || axis < -1 || axis > 1) return Result::Unavailable;
  if (remote) {
    ControlCommand c;
    c.op = ControlOp::ResetMotion;
    c.axis = axis;
    return remote->submitAt(c, now);
  }
  for (unsigned i=0; i<2; ++i) if (axis < 0 || unsigned(axis) == i) {
    stopNow(int(i), now); axes[i].motionError = MotionError::None; cancelMotion(i);
  }
  if(!driveFault) {
    if(axes[0].motionError!=MotionError::None) fault=std::string("az_")+motionErrorName(axes[0].motionError);
    else if(axes[1].motionError!=MotionError::None) fault=std::string("el_")+motionErrorName(axes[1].motionError);
    else if(fault.rfind("az_",0)==0 || fault.rfind("el_",0)==0) fault.clear();
  }
  return Result::Ok;
}
// Only a newly accepted movement request may retry a progress timeout.
// Kick timeout and sensing/planner faults still require an explicit reset.
void Controller::recoverStall(unsigned i, uint32_t now) {
  if (axes[i].motionError != MotionError::Stalled) return;
  // motionFault already stopped this axis. Do not call STOP again here: it
  // would erase a newly queued manual target/sequence and disturb other intent.
  (void)now;
  axes[i].motionError = MotionError::None;
  cancelMotion(i);
  if (!driveFault && (fault.rfind("az_",0)==0 || fault.rfind("el_",0)==0)) {
    if (axes[0].motionError!=MotionError::None) fault=std::string("az_")+motionErrorName(axes[0].motionError);
    else if (axes[1].motionError!=MotionError::None) fault=std::string("el_")+motionErrorName(axes[1].motionError);
    else fault.clear();
  }
}
void Controller::motionFault(unsigned i, MotionError error, uint32_t now) {
  if (autoTune.active()) finishAutoTune(AutoTunePhase::Failed,motionErrorName(error),now);
  stopNow(int(i), now); axes[i].motionError = error; axes[i].motionPhase = MotionPhase::Fault;
  fault = std::string(i ? "el_" : "az_") + motionErrorName(error);
}
void Controller::tickMotion(unsigned i, uint32_t now) {
  auto &axis = axes[i]; auto &r = motion_[i]; const auto &m = motionSettings[i]; const auto &s = speeds[i];
  const bool cascade = m.mode == RegulatorMode::Cascade;
  const auto manualSpeed = [&]() {
    const float work = runtimeSpeed(i), fraction = manualScale_[i];
    if (axis.tracking || (autoTune.active() && autoTune.axis == int(i))) return work * fraction;
    if (fraction <= 0) return 0.f;
    const float minimum = cascade ? .05f * work :
        std::max(.05f * work, std::min(float(m.sustainingPercent), m.outputLimitPercent));
    return minimum + (std::max(work, minimum) - minimum) * fraction;
  };
  if (!m.ready || axis.motionError != MotionError::None || driveFault) {
    axis.wanted = 0; axis.tracking = false; axis.speedOutputPercent = 0; output(i,0,now); cancelMotion(i); return;
  }
  if ((axis.wanted || axis.tracking || axis.holdArmed) && r.feedback.timed &&
      uint32_t(now-r.feedback.at) > m.feedbackTimeoutMs) { motionFault(i,MotionError::FeedbackStale,now); return; }
  if (!axis.wanted && !axis.tracking && !axis.holdArmed) {
    axis.speedOutputPercent = 0; output(i,0,now); cancelMotion(i); return;
  }
  if (r.started && uint32_t(now-r.at) < 20) return;
  const float dt = r.started ? std::min(.1f, float(uint32_t(now-r.at))/1000) : .02f;
  r.at = now; r.started = true;
  axis.measuredVelocity = r.feedback.velocity;
  const float distance = axis.target-axis.current;
  if (axis.motionPhase == MotionPhase::Holding && axis.holdArmed) {
    if (std::abs(distance) <= m.holdResumeDegrees) { output(i,0,now); axis.speedOutputPercent=0; return; }
    axis.tracking = true; axis.completed = false; r.profileValid = false; r.settling = false;
    r.targetDirection=sign(distance); r.crossedTarget=false;
  }
  // A moving stream of small absolute goals is not a sequence of arrivals.
  // Coalesce the stop decision for a short input gap, never for duplicate goals.
  const bool passingTarget=r.streamingTarget && axis.direction && uint32_t(now-r.targetChangedAt)<100;
  const bool moving = std::abs(axis.measuredVelocity) > m.stoppedVelocity;
  const float predictedCoast=std::abs(axis.measuredVelocity)*r.feedback.coastSeconds;
  const bool coastToTarget=distance*axis.measuredVelocity>0 && std::abs(distance)<=predictedCoast;
  // A one-shot positioning move may coast beyond its goal, but only active
  // holding is allowed to reverse and correct that overshoot. Latch the actual
  // crossing, then wait for measured rest before reporting completion.
  if (cascade && axis.tracking && !axis.holdArmed && !passingTarget &&
      r.targetDirection && distance*r.targetDirection<=0) r.crossedTarget=true;
  if (cascade && axis.tracking && (std::abs(distance)<=m.positionTolerance || coastToTarget || r.arriving || r.crossedTarget) && !passingTarget) {
    if (!r.arriving) {
      r.arriving=true; r.profileValid=false; r.kickDone=false; r.releasing=false;
      r.positionPid.reset(); r.velocityPid.reset();
    }
    const bool stopped=!moving;
    if (!stopped) r.settling=false;
    else if (!r.settling) { r.settling=true; r.settledAt=now; }
    if (r.settling && uint32_t(now-r.settledAt) >= m.settleMs && std::abs(distance)>m.positionTolerance && !r.crossedTarget) {
      // Wait for the real coast to finish before judging the remaining error.
      r.arriving=r.settling=false;
    } else if (r.settling && uint32_t(now-r.settledAt) >= m.settleMs) {
      const bool hold = axis.holdArmed && m.holdEnabled;
      axis.tracking = false; axis.wanted = 0; axis.completed = true; axis.speedOutputPercent=0;
      output(i,0,now); cancelMotion(i); axis.holdArmed=hold;
      axis.motionPhase=hold ? MotionPhase::Holding : MotionPhase::Idle;
      return;
    }
    // Coast inside the accepted position band. A noisy/lagging velocity can
    // postpone confirmation, but must never restart the motor within the band.
    if(r.arriving) {
      axis.speedOutputPercent=0; axis.wanted=0; axis.requestedVelocity=axis.profileAcceleration=0;
      output(i,0,now); axis.motionPhase=MotionPhase::Braking; return;
    }
  } else { r.settling = false; r.arriving=false; }
  int desired = cascade && axis.tracking ? sign(distance) : axis.wanted;
  if(passingTarget && std::abs(distance)<=m.positionTolerance) desired=axis.direction;
  if (!desired) desired = axis.wanted;
  const auto blocked=[&](int direction) {
    return axis.calibrated && ((direction<0 && axis.current<=range(i).start) || (direction>0 && axis.current>=range(i).end));
  };
  if(blocked(desired)) {
    axis.wanted=0;axis.tracking=false;axis.speedOutputPercent=0;output(i,0,now);cancelMotion(i);return;
  }
  if (axis.motionPhase == MotionPhase::Kick) {
    if (desired && desired != r.kickDirection) { axis.motionPhase=MotionPhase::Braking; r.releasing=false; r.ramp=axis.speedOutputPercent; r.rampRate=0; }
    else if ((axis.current-r.kickPosition)*r.kickDirection >= m.movementDegrees) {
      r.releaseOutput=axis.speedOutputPercent;
      axis.motionPhase=MotionPhase::Run; r.releasing=true; r.releaseAt=now; r.kickDone=true;
      r.profileValid=false; r.positionPid.reset(); r.velocityPid.reset();
      if(!cascade) {
        float running=manualSpeed();
        if(axis.tracking) {
          const float fraction=std::clamp((std::abs(distance)-tolerance(i))/std::max(1u,s.slowdownDegrees),0.f,1.f);
          const float approach=std::min(float(s.approachSpeedPercent),runtimeSpeed(i));
          running=approach+(runtimeSpeed(i)-approach)*fraction;
        }
        r.ramp=std::min(running,m.kickPercent); r.rampRate=0;
      }
    } else if (uint32_t(now-r.kickAt) >=
               ((autoTune.active() && autoTune.axis==int(i)) ? m.kickMs : std::max(m.kickMs,m.stallMs))) {
      auto &t=autoTune;
      if(t.active() && t.axis==int(i) &&
          (t.phase==AutoTunePhase::Low || t.phase==AutoTunePhase::High || t.phase==AutoTunePhase::Reposition) &&
          t.searchLevel<t.limit) {
        // Retry a sticky section at the next bounded level after stopping.
        // Raising a live pulse rapidly to the cap confuses feedback delay
        // with breakaway torque and consumes the measurement stroke.
        t.searchLevel=std::min(t.limit,t.searchLevel+t.options.searchStepPercent);
        const bool transfer=t.phase==AutoTunePhase::Reposition;
        autoTuneCoast(transfer?t.nextPhase:t.phase,transfer?t.nextDirection:t.direction,
                      transfer?t.nextLevel:t.level,transfer,now);
      } else motionFault(i,MotionError::KickTimeout,now);
      return;
    }
    else {
      // Give each level time to appear in the position feedback. Applying the
      // whole configured ceiling before the first fresh displacement injects
      // excessive energy into a short position/holding correction.
      const unsigned steps=std::clamp(m.kickMs/60,1u,8u);
      const unsigned interval=std::max(20u,(m.kickMs*2/3)/steps);
      const float step=float(std::min<uint32_t>(steps,uint32_t(now-r.kickAt)/interval))/steps;
      axis.speedOutputPercent=std::clamp(r.kickBase+(m.kickPercent-r.kickBase)*step,0.f,m.outputLimitPercent);
      r.kickPeak=std::max(r.kickPeak,axis.speedOutputPercent);
      return;
    }
  }
  // Simple controller ramp uses percent as a virtual velocity. The two boundary
  // derivatives remain continuous when a target or the work output changes.
  const float up = std::max(.1f, float(s.accelerationMs)/1000);
  const float down = std::max(.1f, float(s.decelerationMs)/1000/std::max(.1f,s.decelerationFactor));
  const float work = runtimeSpeed(i);
  const float velocityLimit = cascade ? runtimeSpeed(i) : m.velocity;
  auto rampTo = [&](float target) {
    float virtualPosition=0;
    const float duration=target>=r.ramp ? up : down;
    return profileStep(false, virtualPosition, r.ramp, r.rampRate, target, 100,
                       2*work/up, 2*work/down, 4*work/(duration*duration), dt);
  };
  if (axis.direction && desired && axis.direction != desired) {
    if (axis.motionPhase != MotionPhase::Braking && std::abs(r.ramp-axis.speedOutputPercent)>.001f) {
      r.ramp=axis.speedOutputPercent;r.rampRate=0;
    }
    axis.motionPhase=MotionPhase::Braking;
    if(!cascade) {
      if (!rampTo(0)) { motionFault(i,MotionError::Planner,now); return; }
      axis.speedOutputPercent=std::clamp(r.ramp,0.f,m.outputLimitPercent);
      if (r.ramp > .001f) return;
    }
    // Never power away from a position target while waiting for a virtual
    // S-profile to decelerate. That can drive an overshoot for seconds and
    // turn holding into a large limit cycle. Coast first; the interlock below
    // still requires measured rest and 300 ms before the opposite direction.
    axis.speedOutputPercent=0; output(i,0,now); r.profileValid=false; r.kickDone=false; r.releasing=false;
    axis.requestedVelocity=axis.profileAcceleration=0;
    r.positionPid.reset(); r.velocityPid.reset(); return;
  }
  if (!axis.direction && axis.previous && axis.previous != desired &&
      (uint32_t(now-axis.offAt) < 300 || (r.feedback.timed && moving))) {
    axis.motionPhase=MotionPhase::Braking; axis.speedOutputPercent=0; return;
  }
  if (desired && !axis.direction) {
    output(i,desired,now); if (!axis.direction) return;
    // A velocity estimate alone can be ADC noise on a stationary shaft. Only
    // confirmed directed displacement permits skipping breakaway on a start.
    const bool confirmedMoving=moving && r.feedback.progressed(desired,m.movementDegrees,m.velocityWindowMs);
    if (m.kickEnabled && !confirmedMoving && axis.calibrated && !r.kickDone) {
      axis.motionPhase=MotionPhase::Kick; r.kickAt=now; r.kickPosition=axis.current; r.kickDirection=desired;
      r.progressAt=now; r.progressPosition=axis.current; r.progressValid=true;
      r.kickBase=std::min(m.kickPercent,std::max(5.f,cascade?float(m.sustainingPercent):manualSpeed()));
      if(m.kickMs<=40)r.kickBase=m.kickPercent; // One control tick cannot fit a staircase.
      r.kickPeak=axis.speedOutputPercent=std::min(r.kickBase,m.outputLimitPercent); return;
    }
  }
  float requested=0;
  if (cascade) {
    // The regression velocity deliberately averages a sensor window. At
    // breakaway it can still report the beginning of acceleration. Use the
    // newest fresh displacement only to initialize the profile and detect
    // kick overspeed; the PID keeps its filtered measurement afterwards.
    float handoffVelocity=axis.measuredVelocity;
    if(r.releasing && now==r.releaseAt && r.feedback.count>=2) {
      const auto &samples=r.feedback.samples;
      const auto &latest=samples[(r.feedback.next+samples.size()-1)%samples.size()];
      const auto &previous=samples[(r.feedback.next+samples.size()-2)%samples.size()];
      const unsigned age=uint32_t(latest.at-previous.at);
      if(age>=20) {
        const float recent=(latest.position-previous.position)*1000/age;
        if(desired*recent>desired*handoffVelocity)handoffVelocity=recent;
      }
    }
    if (!r.profileValid) {
      // Kick can exceed the working speed. Do not promote that overspeed to
      // the desired trajectory: it could keep driving fast for an entire slow
      // deceleration. The velocity PID still sees the actual speed and brakes.
      r.referencePosition=axis.current;
      const bool confirmed=r.kickDone || r.feedback.progressed(desired,m.movementDegrees,m.velocityWindowMs);
      r.referenceVelocity=confirmed ? std::clamp(handoffVelocity,-velocityLimit,velocityLimit) : 0;
      r.referenceAcceleration=0;
      r.commandVelocity=r.referenceVelocity;r.commandAcceleration=0;r.profileValid=true;

    }
    const float filter=float(m.derivativeFilterMs)/1000;
    if(axis.tracking) {
      if(!r.profileTracking || velocityLimit!=r.plannedSpeedLimit) {
        // A live speed selection changes the remaining move, not its goal.
        // Start at the shaft position and the command's continuous boundary
        // state: a lagging virtual path must not keep opposing the selection.
        // Keep both PID states, breakaway and progress deadlines intact.
        r.referencePosition=axis.current;
        r.referenceVelocity=r.commandVelocity;
        r.referenceAcceleration=r.commandAcceleration;
      }
      // Lowering a hard position-profile limit below its current velocity
      // invokes the planner's constraint recovery brake. That brake can go
      // well below the selected speed. Instead, keep the current state
      // feasible and approach the new limit with a velocity S-curve. The
      // position trajectory still wins whenever the goal needs more braking.
      const float outwardAcceleration=std::max(0.f,r.referenceAcceleration*sign(r.referenceVelocity));
      const float feasibleLimit=std::max(velocityLimit,std::abs(r.referenceVelocity)+
          outwardAcceleration*outwardAcceleration/(2*m.jerk));
      float speedPosition=r.referencePosition,speedVelocity=r.referenceVelocity,speedAcceleration=r.referenceAcceleration;
      if (!profileStep(true,r.referencePosition,r.referenceVelocity,r.referenceAcceleration,axis.target,
                       feasibleLimit,m.acceleration,m.deceleration,m.jerk,dt)) { motionFault(i,MotionError::Planner,now); return; }
      if(feasibleLimit>velocityLimit+.0001f) {
        if(!profileStep(false,speedPosition,speedVelocity,speedAcceleration,velocityLimit*desired,
                        m.velocity,m.acceleration,m.deceleration,m.jerk,dt)) { motionFault(i,MotionError::Planner,now); return; }
        if(speedVelocity*desired<r.referenceVelocity*desired) {
          r.referencePosition=speedPosition;r.referenceVelocity=speedVelocity;r.referenceAcceleration=speedAcceleration;
        }
      }
      // A reduced limit cannot clip the feedforward while the position
      // profile is still braking from a higher speed. Its acceleration lead
      // would then be applied to the already clipped speed and brake twice,
      // taking the command below the new selection before recovering.
      const float correctionLimit=std::max(velocityLimit,std::abs(r.referenceVelocity));
      const float velocity=r.positionPid.step(r.referencePosition-axis.current,axis.current,r.referenceVelocity,
         m.positionKp,m.positionKi,m.positionKd,dt,filter,-correctionLimit,correctionLimit,!r.releasing);
      float virtualPosition=0;
      // Pass the nominal position profile through without a second S-curve;
      // only its PID correction needs the remaining velocity/jerk limiter.
      const float accelerationLead=std::copysign(r.referenceAcceleration*r.referenceAcceleration/(2*m.jerk),r.referenceAcceleration);
      const float velocityBoundary=std::clamp(velocity+accelerationLead,-m.velocity,m.velocity);
      if (!profileStep(false,virtualPosition,r.commandVelocity,r.commandAcceleration,velocityBoundary,
                       m.velocity,m.acceleration,m.deceleration,m.jerk,dt)) { motionFault(i,MotionError::Planner,now); return; }
    } else {
      // Jog selects a velocity directly. One S-curve preserves acceleration
      // and jerk when the slider or proportional manual input changes.
      if (!profileStep(false,r.referencePosition,r.commandVelocity,r.commandAcceleration,
                       manualSpeed()*desired,m.velocity,m.acceleration,m.deceleration,m.jerk,dt)) {
        motionFault(i,MotionError::Planner,now); return;
      }
      r.referenceVelocity=r.commandVelocity;r.referenceAcceleration=r.commandAcceleration;
    }
    r.profileTracking=axis.tracking;r.plannedSpeedLimit=velocityLimit;
    axis.requestedVelocity=r.commandVelocity; axis.profileAcceleration=r.commandAcceleration;
    desired=axis.direction ? axis.direction : sign(r.commandVelocity);
    if(blocked(desired)) {
      axis.wanted=0;axis.tracking=false;axis.speedOutputPercent=0;output(i,0,now);cancelMotion(i);return;
    }
    // Static breakaway and dynamic friction are different. Once moving, do
    // not fall below an identified sustaining output while requesting motion
    // in this direction. Braking, arrival and safety paths above retain zero.
    const float floor=desired*r.commandVelocity>.001f ? float(m.sustainingPercent) : 0.f;
    const float lo=desired < 0 ? -m.outputLimitPercent : floor, hi=desired < 0 ? -floor : m.outputLimitPercent;
    const float ff=m.feedforward*r.commandVelocity;
    if (r.releasing && now==r.releaseAt && m.velocityKi>0 && desired*handoffVelocity<velocityLimit) {
      // Transfer the actual breakaway output into the PI state instead of
      // dropping to feedforward/floor for several seconds while I rebuilds.
      // The freshly reset derivative is zero. This is an initial condition,
      // not integration during Kick: normal error integration resumes after
      // release and removes excess torque. Never retain a pulse when its
      // measured speed already exceeds the working speed.
      const float seed=desired*r.releaseOutput-ff-m.velocityKp*(r.commandVelocity-axis.measuredVelocity);
      r.velocityPid.integral=std::clamp(seed,-m.outputLimitPercent,m.outputLimitPercent);
    }
    const float signedOutput=r.velocityPid.step(r.commandVelocity-axis.measuredVelocity,axis.measuredVelocity,ff,
       m.velocityKp,m.velocityKi,m.velocityKd,dt,filter,lo,hi,!r.releasing);
    requested=std::abs(signedOutput);
    if (!axis.direction && desired) { output(i,desired,now); if (!axis.direction) requested=0; }
  } else {
    float target=manualSpeed();
    if (axis.tracking) {
      const float fraction=std::clamp((std::abs(distance)-tolerance(i))/std::max(1u,s.slowdownDegrees),0.f,1.f);
      const float approach=std::min(float(s.approachSpeedPercent),work);
      target=approach+(work-approach)*fraction;
    }
    if (!rampTo(std::min(target,m.outputLimitPercent))) { motionFault(i,MotionError::Planner,now); return; }
    requested=r.ramp;
  }
  if (r.releasing) {
    const float t=std::clamp(float(uint32_t(now-r.releaseAt))/std::max(1u,m.kickReleaseMs),0.f,1.f);
    const float blend=t*t*t*(10+t*(-15+6*t));
    requested=r.releaseOutput+(requested-r.releaseOutput)*blend;
    if (t >= 1) r.releasing=false;
  }
  axis.speedOutputPercent=axis.direction ? std::clamp(requested,0.f,m.outputLimitPercent) : 0;
  axis.motionPhase=MotionPhase::Run;
  const bool tuning = autoTune.active() && autoTune.axis == int(i);
  // A small correction can legitimately remain below the drive's dead band.
  // Start a full progress interval only when meaningful output is restored.
  // Autotune still observes low output: no motion there is a search result.
  if (!tuning && axis.speedOutputPercent < 7.f-1e-4f) r.progressValid=false;
  else if ((tuning || axis.speedOutputPercent > 0) && axis.calibrated && r.feedback.timed) {
    // Only a new furthest position in the commanded direction is progress.
    // Alternating ADC counts at a stationary shaft must not keep it powered.
    if (!r.progressValid || axis.direction*(axis.current-r.progressPosition) >= std::min(.1f,m.movementDegrees)) {
      r.progressAt=now; r.progressPosition=axis.current; r.progressValid=true;
    } else if (uint32_t(now-r.progressAt) >= m.stallMs) {
      if(tuning) raiseAutoTuneMinimum(now);
      else motionFault(i,MotionError::Stalled,now);
    }
  }
}
const char *Controller::autoTunePhaseName(AutoTunePhase phase) {
  static constexpr const char *names[]={"idle","quiet","start","low","high","coast","reposition","validate","complete","cancelled","failed"};
  return names[unsigned(phase)];
}
Result Controller::centerForAutoTune(unsigned i, uint32_t now) {
  if(remote || configurationLocked || i>1 || (i==1 && model!=Model::G5500) || !variableSpeed(i)) return Result::Unavailable;
  if(autoTune.active() || mode!=Mode::Manual || moving() || pendingValid || presetActive || sequenceCount() ||
      axes[0].tracking || axes[1].tracking || axes[0].holdArmed || axes[1].holdArmed) return Result::Busy;
  if(!axes[i].calibrated) return Result::NotCalibrated;
  if(driveFault || !fault.empty() || axes[0].motionError!=MotionError::None || axes[1].motionError!=MotionError::None)
    return Result::Unavailable;
  const auto &feedback=motion_[i].feedback;
  if(!feedback.timed || !feedback.valid || uint32_t(now-feedback.at)>motionSettings[i].feedbackTimeoutMs)
    return Result::Unavailable;
  const auto bounds=range(i);const float middle=bounds.start+bounds.span()/2;
  const auto result=start({middle,middle,i==0,i==1},now,false);
  // Preparation is a one-shot movement, even if normal positioning uses hold.
  // Keep the saved hold preference intact for subsequent ordinary targets.
  if(result==Result::Ok)axes[i].holdArmed=false;
  return result;
}
bool AutoTuneOptions::valid(float limit) const {
  for(const auto &f:autoTuneRealFields) {
    const float value=this->*f.member;
    if(!std::isfinite(value) || value<f.lo || value>f.hi)return false;
  }
  for(const auto &f:autoTuneIntFields)if(this->*f.member<f.lo || this->*f.member>f.hi)return false;
  return searchStartPercent<=limit && plateauTimeoutMs>=2*plateauMinMs+200;
}
Result Controller::startAutoTune(unsigned i, RegulatorMode mode, float travel, float limit, uint32_t now, const AutoTuneOptions &options) {
  if (remote || configurationLocked || i>1 || (i==1 && model!=Model::G5500) || !variableSpeed(i)) return Result::Unavailable;
  if (autoTune.active() || moving() || axes[0].tracking || axes[1].tracking || axes[0].holdArmed || axes[1].holdArmed ||
      mode != motionSettings[i].mode || this->mode!=Mode::Manual || pendingValid || presetActive || sequenceCount()) return Result::Busy;
  const auto &m=motionSettings[i];const auto &f=motion_[i].feedback;
  if (!axes[i].calibrated) return Result::NotCalibrated;
  if (driveFault || !fault.empty() || axes[0].motionError!=MotionError::None || axes[1].motionError!=MotionError::None ||
      !m.ready || !m.valid() || !f.timed || !f.valid || uint32_t(now-f.at)>m.feedbackTimeoutMs) return Result::Unavailable;
  if (!options.valid(limit) || !std::isfinite(travel) || !std::isfinite(limit) || travel<10 || travel>90 ||
      limit<10 || limit>m.outputLimitPercent || limit>100 || travel<6*m.movementDegrees) return Result::Invalid;
  const float margin=std::max(2.f,speeds[i].coastDegrees);
  if (axes[i].current-travel < range(i).start+margin || axes[i].current+travel > range(i).end-margin) return Result::Invalid;
  const uint32_t id=autoTune.id+1;
  stopNow(-1,now);
  autoTune={};auto &t=autoTune;t.options=options;
  t.id=id?id:1;t.axis=int(i);t.mode=mode;t.origin=axes[i].current;t.otherPosition=axes[1-i].current;
  t.lower=t.origin-travel;t.upper=t.origin+travel;t.limit=limit;t.revision=rangeRevision;t.startedAt=now;
  t.originalSpeed=t.proposedSpeed=speeds[i];t.originalMotion=t.proposedMotion=m;
  // Identification uses the regular simple controller, not raw GPIO writes.
  // All sensor, progress, range and reversal protections stay in tickMotion().
  auto probe=m;probe.mode=RegulatorMode::Simple;probe.kickEnabled=false;probe.holdEnabled=false;probe.outputLimitPercent=limit;
  motionSettings[i]=probe;speeds[i].accelerationMs=100;speeds[i].decelerationMs=100;speeds[i].decelerationFactor=1;
  t.noiseLow=t.noiseHigh=axes[i].current;t.sampledAt=f.at;
  t.lastPosition=axes[i].current;t.positionAt=f.at;
  autoTunePhase(AutoTunePhase::Quiet,now);
  return Result::Ok;
}
void Controller::autoTunePhase(AutoTunePhase phase,uint32_t now) {
  auto &t=autoTune;t.phase=phase;t.phaseAt=now;t.phasePosition=axes[unsigned(t.axis)].current;
  t.sampleCount=t.sampleNext=0;t.settled=false;t.plateauReady=false;
  t.plateauAt=0;t.plateauVelocity=0;
}
void Controller::autoTuneLevel(float level) {
  auto &t=autoTune;t.level=std::clamp(std::ceil(level),1.f,std::floor(t.limit));
  speeds[unsigned(t.axis)].speedPercent=unsigned(t.level);
  speeds[unsigned(t.axis)].approachSpeedPercent=speeds[unsigned(t.axis)].speedPercent;
}
void Controller::raiseAutoTuneMinimum(uint32_t now) {
  auto &t=autoTune;
  if(t.phase!=AutoTunePhase::Low && t.phase!=AutoTunePhase::High && t.phase!=AutoTunePhase::Validate) {
    finishAutoTune(AutoTunePhase::Failed,"no_sustained_motion",now);return;
  }
  // Re-identify at a higher *measured* working output. A temporary PWM floor
  // during validation would hide a bad candidate that fails again after Apply.
  const bool validating=t.phase==AutoTunePhase::Validate;
  const float next=validating ? std::max(t.measurements[0].highOutput,t.measurements[1].highOutput)+1 : t.level+1;
  t.minimumLevel=std::max(t.minimumLevel,next);
  if(t.minimumLevel>std::floor(t.limit)-2) {
    finishAutoTune(AutoTunePhase::Failed,"no_sustained_motion",now);return;
  }
  const int direction=validating?1:t.direction;
  for(unsigned d=0;d<2;++d) if(validating || (direction>0?0u:1u)==d) {
    auto &m=t.measurements[d];
    m.lowOutput=t.minimumLevel;m.lowVelocity=m.highOutput=m.highVelocity=0;
    t.confirmedHigh[d]={};
  }
  t.relocations=0;
  // Stop and confirm coast before the next bounded startup. The global time,
  // stroke, output cap and saved kick/stall deadlines are never extended.
  autoTuneCoast(AutoTunePhase::Low,direction,t.minimumLevel,false,now);
}
bool Controller::autoTuneProbe(AutoTunePhase phase,int direction,float level,uint32_t now) {
  auto &t=autoTune;const unsigned i=unsigned(t.axis);
  t.internal=true;stopNow(int(i),now);t.direction=direction;
  auto probe=t.originalMotion;probe.mode=RegulatorMode::Simple;probe.holdEnabled=false;probe.outputLimitPercent=t.limit;
  probe.kickEnabled=phase!=AutoTunePhase::Start;
  probe.kickPercent=std::min(t.limit,t.searchLevel);
  // A probe never extends the saved startup or stall protection deadline.
  probe.kickMs=std::min({2800u,t.originalMotion.stallMs,t.originalMotion.kickEnabled?t.originalMotion.kickMs:2800u});
  motionSettings[i]=probe;speeds[i].accelerationMs=100;speeds[i].decelerationMs=100;speeds[i].decelerationFactor=1;
  autoTuneLevel(level);autoTunePhase(phase,now);
  const auto result=jog(i,direction,false,now);t.internal=false;
  if(result!=Result::Ok)finishAutoTune(AutoTunePhase::Failed,"start_rejected",now);
  return result==Result::Ok;
}
void Controller::autoTuneCoast(AutoTunePhase next,int direction,float level,bool relocate,uint32_t now) {
  auto &t=autoTune;t.nextPhase=next;t.nextDirection=direction;t.nextLevel=level;t.relocate=relocate;
  t.internal=true;stopNow(t.axis,now);t.internal=false;autoTunePhase(AutoTunePhase::Coast,now);
}
void Controller::finishAutoTune(AutoTunePhase phase,const char *reason,uint32_t now) {
  auto &t=autoTune;if(!t.active())return;
  t.phase=phase;t.reason=reason;t.internal=true;
  stopNow(-1,now);
  const unsigned i=unsigned(t.axis);speeds[i]=t.originalSpeed;motionSettings[i]=t.originalMotion;
  axes[i].targetKnown=false;axes[i].accepted=false;
  targetKnown=false;lastTarget={0,0,false,false};
  t.internal=false;
}
Result Controller::cancelAutoTune(uint32_t now) {
  if(remote)return Result::Unavailable;
  if(autoTune.active())finishAutoTune(AutoTunePhase::Cancelled,"cancelled",now);
  return Result::Ok;
}
Result Controller::applyAutoTune(uint32_t id,uint32_t now) {
  auto &t=autoTune;
  if(remote || configurationLocked || t.phase!=AutoTunePhase::Complete || !id || id!=t.id) return Result::Unavailable;
  const unsigned i=unsigned(t.axis);
  if(moving() || axes[0].tracking || axes[1].tracking || axes[0].holdArmed || axes[1].holdArmed || driveFault ||
      !fault.empty() || axes[0].motionError!=MotionError::None || axes[1].motionError!=MotionError::None) return Result::Busy;
  if(rangeRevision!=t.revision || !(speeds[i]==t.originalSpeed) || motionSettings[i]!=t.originalMotion || !axes[i].calibrated)
    return Result::Invalid;
  stopNow(-1,now);
  if(t.mode==RegulatorMode::Simple) speeds[i]=t.proposedSpeed;
  motionSettings[i]=t.proposedMotion;
  t.phase=AutoTunePhase::Idle;t.reason="applied";
  return Result::Ok;
}
bool Controller::proposeAutoTune(uint32_t now) {
  auto &t=autoTune;auto &m=t.proposedMotion;auto &s=t.proposedSpeed;
  const auto &a=t.measurements[0],&b=t.measurements[1];
  const float speed=std::min(a.lowVelocity,b.lowVelocity);
  if(speed<=t.threshold || a.highVelocity<=a.lowVelocity*1.05f || b.highVelocity<=b.lowVelocity*1.05f) return false;
  const float gain=std::max((a.highVelocity-a.lowVelocity)/(a.highOutput-a.lowOutput),
                            (b.highVelocity-b.lowVelocity)/(b.highOutput-b.lowOutput));
  if(!std::isfinite(gain) || gain<=0)return false;
  // Fast drives need several fresh samples without another 200 ms of delay
  // after the position filter. Validate the resulting feedback with the
  // candidate, rather than compensating estimator lag with aggressive I.
  if(t.mode==RegulatorMode::Cascade) m.velocityWindowMs=std::min(m.velocityWindowMs,80u);
  const float delay=std::max(.02f,float(m.velocityWindowMs)/2000);
  const float tau=std::clamp(std::max(a.responseSeconds,b.responseSeconds),.05f,5.f);
  const float lambda=std::max(.5f,2*(tau+delay))*t.options.lambdaFactor;
  m.mode=t.mode;m.ready=true;m.holdEnabled=false;m.kickEnabled=true;m.outputLimitPercent=t.limit;
  // These are settled, constant-level start trials, already rounded upward.
  // Do not multiply an observation delayed by the sensor into a harder kick.
  m.kickPercent=std::min(t.limit,5*std::ceil(std::max(a.startOutput,b.startOutput)/5));
  m.kickMs=std::clamp(unsigned(1.5f*std::max(a.startMs,b.startMs)+200),200u,10000u);
  // Keep the handoff used during the probes. Estimator delay does not identify
  // a longer kick release; extending it would add unmeasured startup travel.
  m.stoppedVelocity=std::clamp(t.threshold,.001f,10.f);
  m.movementDegrees=std::max(m.movementDegrees,6*(t.noiseHigh-t.noiseLow));
  // The lowest sustainable plateau can be marginal elsewhere in the stroke.
  // Use the tested upper plateau as working output, including on approach.
  s.speedPercent=unsigned(std::ceil(std::max(a.highOutput,b.highOutput)));
  s.approachSpeedPercent=s.speedPercent;
  s.accelerationMs=std::clamp(unsigned(2000*(tau+delay)),100u,10000u);s.decelerationMs=s.accelerationMs;s.decelerationFactor=1;
  // One simple-mode coast setting serves both directions. The midpoint
  // minimizes the worst directional stopping error; the largest observed
  // coast remains the safety reserve for subsequent probes.
  s.coastDegrees=std::min(range(unsigned(t.axis)).span()/10,(a.coastDegrees+b.coastDegrees)/2);
  s.slowdownDegrees=unsigned(std::clamp(std::ceil(speed*s.decelerationMs/1000+s.coastDegrees),1.f,90.f));
  if(t.mode==RegulatorMode::Cascade) {
    // Conservative SIMC PI for the measured first-order speed response.
    // https://skoge.folk.ntnu.no/publications/2003/tuningPID/README.html
    // Kp=tau/(gain*(lambda+delay)); Ti=min(tau,4*(lambda+delay)).
    m.velocityKp=tau/(gain*(lambda+delay));m.velocityKi=m.velocityKp/std::min(tau,4*(lambda+delay));m.velocityKd=0;
    m.positionKp=t.options.positionGainFactor/(4*lambda);m.positionKi=m.positionKd=0;
    // Do not deliberately command less than the measured sustaining speeds.
    // The faster of the two low plateaus keeps both directions out of their
    // observed low-speed friction region.
    // The old saved speed may itself be below the drive's sustainable region.
    // It is a parameter being tuned, not the operator's trial-output limit.
    m.velocity=std::clamp(std::max({a.lowVelocity,b.lowVelocity,std::min(a.highVelocity,b.highVelocity)}),.001f,360.f);
    m.sustainingPercent=unsigned(std::ceil(std::max(a.lowOutput,b.lowOutput)));
    // Friction makes PWM/speed at a low plateau unsuitable for a faster cruise.
    // Interpolate each measured response at the chosen operating velocity.
    const auto workingOutput=[&](const auto &v) {
      return v.lowOutput+(m.velocity-v.lowVelocity)*(v.highOutput-v.lowOutput)/(v.highVelocity-v.lowVelocity);
    };
    const float working=std::max(workingOutput(a),workingOutput(b));
    if(!std::isfinite(working) || working>m.outputLimitPercent || working<=0) return false;
    m.feedforward=working/m.velocity;
    // A faster working point must not silently loosen the requested accuracy.
    // The feedback controller must slow on approach and validate that accuracy.
    m.positionTolerance=std::max(m.positionTolerance,m.movementDegrees);
    m.holdResumeDegrees=std::max(m.holdResumeDegrees,1.5f*m.positionTolerance);
    m.acceleration=std::min(m.acceleration,m.velocity/lambda);
    m.deceleration=std::min(m.deceleration,m.velocity/lambda);
    m.jerk=std::min(m.jerk,m.acceleration/lambda);
    // Keep the model-based PI. Breakaway is handled by its bounded kick;
    // inflating I to recover static friction creates a stop/start limit cycle.
  }
  if(!m.valid() || m.movementDegrees>10)return false;
  const unsigned i=unsigned(t.axis);
  // Reserve the observed kick/transfer peak too. A destination planned only
  // from plateau speed can immediately trip the unchanged dynamic stroke guard.
  // Cascade stops by feedback; only Simple additionally uses a manual coast cutoff.
  const float reserve=std::max({2.f,t.originalSpeed.coastDegrees,1.2f*t.stoppingDistance})+
      t.originalMotion.movementDegrees+std::max({a.highVelocity,b.highVelocity,t.peakVelocity})*
      (.15f+float(t.originalMotion.velocityWindowMs)/2000)+(t.mode==RegulatorMode::Simple?2*s.coastDegrees:0);
  const float lower=t.lower+reserve,upper=t.upper-reserve;
  if(lower>=upper) {finishAutoTune(AutoTunePhase::Failed,"insufficient_travel",now);return false;}
  const float current=axes[i].current;
  t.validationOrigin=std::clamp(current,lower,upper);
  t.validationError=0;
  t.validationPass=t.validationReversals=0;t.validationLastDirection=0;
  const int direction=upper-current>=current-lower?1:-1;
  const float available=direction>0?upper-current:current-lower;
  if(available<std::max(5.f,3*m.movementDegrees)) {
    finishAutoTune(AutoTunePhase::Failed,"insufficient_travel",now);return false;
  }
  // Returning to the origin can be just a few degrees after the last probe.
  // A strong startup pulse then dominates the whole move, so the measured
  // working-output coast is never exercised. Allow a few seconds of work-speed
  // travel, within the same guarded stroke; retain the original accuracy test.
  const float minimumMove=t.mode==RegulatorMode::Cascade?2*m.positionTolerance+m.movementDegrees:0;
  const float distance=std::min(available,std::max({5.f,4*speed,6*s.coastDegrees,4*m.movementDegrees,minimumMove}));
  t.validationTarget=std::clamp(current+direction*distance,lower,upper);
  if(t.mode==RegulatorMode::Cascade && std::abs(t.validationTarget-t.validationOrigin)<minimumMove) {
    t.validationTarget=std::clamp(t.validationOrigin+direction*minimumMove,lower,upper);
    if(std::abs(t.validationTarget-t.validationOrigin)+.001f<minimumMove) {
      finishAutoTune(AutoTunePhase::Failed,"insufficient_travel",now);return false;
    }
  }
  // Validate the candidate through normal position control before offering Apply.
  t.internal=true;stopNow(int(i),now);
  speeds[i]=s;motionSettings[i]=m;
  t.direction=t.validationTarget>=axes[i].current?1:-1;
  autoTunePhase(AutoTunePhase::Validate,now);
  Target target{t.validationTarget,t.validationTarget,i==0,i==1};
  const auto result=start(target,now,false);t.internal=false;
  return result==Result::Ok;
}
void Controller::tickAutoTune(uint32_t now) {
  auto &t=autoTune;if(!t.active())return;
  if(t.ticked && uint32_t(now-t.tickAt)<20)return;
  const float dt=t.ticked?float(uint32_t(now-t.tickAt))/1000:.02f;t.ticked=true;t.tickAt=now;
  if(dt>.25f) {finishAutoTune(AutoTunePhase::Failed,"control_timing",now);return;}
  const unsigned i=unsigned(t.axis);const auto &f=motion_[i].feedback;auto &a=axes[i];
  if(t.phase==AutoTunePhase::Validate && a.direction && a.direction!=t.validationLastDirection) {
    if(t.validationLastDirection && ++t.validationReversals>1) {
      finishAutoTune(AutoTunePhase::Failed,"validation_error",now);return;
    }
    t.validationLastDirection=a.direction;
  }
  if(configurationLocked || driveFault || rangeRevision!=t.revision || !a.calibrated || !f.valid ||
      !f.timed || uint32_t(now-f.at)>t.originalMotion.feedbackTimeoutMs) {
    finishAutoTune(AutoTunePhase::Failed,"feedback_or_configuration",now);return;
  }
  if(uint32_t(now-t.startedAt)>120000) {finishAutoTune(AutoTunePhase::Failed,"timeout",now);return;}
  if(std::abs(axes[1-i].current-t.otherPosition)>std::max(2.f,t.originalMotion.movementDegrees) || axes[1-i].direction) {
    finishAutoTune(AutoTunePhase::Failed,"other_axis_moved",now);return;
  }
  const float velocity=t.direction*a.measuredVelocity;
  if(f.at!=t.positionAt) {
    t.sampleVelocity=(a.current-t.lastPosition)*1000/uint32_t(f.at-t.positionAt);
    t.lastPosition=a.current;t.positionAt=f.at;
  }
  const float coast=std::max({t.stoppingDistance,t.measurements[0].coastDegrees,t.measurements[1].coastDegrees});
  // The filtered velocity lags during kick. Include fresh position differences
  // and estimator latency in the stopping reserve, never just steady speed.
  const float speed=std::max(std::abs(a.measuredVelocity),std::abs(t.sampleVelocity));
  if(t.phase!=AutoTunePhase::Validate)t.peakVelocity=std::max(t.peakVelocity,speed);
  const float guard=std::max(2.f,std::max(t.originalSpeed.coastDegrees,1.2f*coast)+
      t.originalMotion.movementDegrees+speed*(.15f+float(t.originalMotion.velocityWindowMs)/2000));
  if(a.current<t.lower || a.current>t.upper) {finishAutoTune(AutoTunePhase::Failed,"travel_limit",now);return;}
  const bool nearEdge=a.direction && ((a.direction>0 && a.current>t.upper-guard)||
                                      (a.direction<0 && a.current<t.lower+guard));
  const auto keepMeasuredHigh=[&]() {
    const auto &point=t.confirmedHigh[t.direction>0?0:1];
    if(point.output<=0)return false;
    auto &measured=t.measurements[t.direction>0?0:1];
    measured.highOutput=point.output;measured.highVelocity=point.velocity;measured.responseSeconds=point.responseSeconds;
    t.relocations=0;
    autoTuneCoast(t.direction>0?AutoTunePhase::Start:AutoTunePhase::Validate,-1,t.searchLevel,false,now);
    return true;
  };
  if(nearEdge) {
    if(t.phase==AutoTunePhase::Start || t.phase==AutoTunePhase::Low || t.phase==AutoTunePhase::High) {
      ++t.relocations;
      if(t.phase==AutoTunePhase::High && t.relocations>1 && keepMeasuredHigh())return;
      if(t.relocations>3) {finishAutoTune(AutoTunePhase::Failed,"insufficient_travel",now);return;}
      autoTuneCoast(t.phase,t.direction,t.level,true,now);return;
    }
    if(t.phase==AutoTunePhase::Reposition) {
      t.internal=true;stopNow(int(i),now);t.internal=false;return;
    }
    if(t.phase!=AutoTunePhase::Coast) {finishAutoTune(AutoTunePhase::Failed,"travel_limit",now);return;}
  }
  if(t.phase==AutoTunePhase::Quiet) {
    if(f.at!=t.sampledAt){t.sampledAt=f.at;t.noiseLow=std::min(t.noiseLow,a.current);t.noiseHigh=std::max(t.noiseHigh,a.current);t.noiseVelocity=std::max(t.noiseVelocity,std::abs(a.measuredVelocity));}
    if(uint32_t(now-t.phaseAt)<600)return;
    t.threshold=std::max(.2f,3*t.noiseVelocity);
    if(t.noiseHigh-t.noiseLow>t.originalMotion.movementDegrees/2 || t.threshold>10) {finishAutoTune(AutoTunePhase::Failed,"feedback_noise",now);return;}
    // A saved kick is not an identified minimum, especially after changing
    // PWM frequency or the mechanics. Search from below it on every new tune.
    t.searchLevel=std::min(t.options.searchStartPercent,t.limit);
    autoTuneProbe(AutoTunePhase::Start,1,t.searchLevel,now);return;
  }
  auto &measurement=t.measurements[t.direction>0?0:1];
  if((t.phase==AutoTunePhase::Low || t.phase==AutoTunePhase::High || t.phase==AutoTunePhase::Reposition) &&
      a.motionPhase==MotionPhase::Run && motion_[i].kickDone && a.direction) {
    // A later restart can encounter greater stiction than the first search.
    // Record only feedback-confirmed pulses, in the actual drive direction
    // (repositioning runs opposite to the pending measurement direction).
    auto &started=t.measurements[a.direction>0?0:1];
    started.startOutput=std::max(started.startOutput,motion_[i].kickPeak);
    started.startMs=std::max(started.startMs,uint32_t(motion_[i].releaseAt-motion_[i].kickAt));
  }
  if((t.phase==AutoTunePhase::Low || t.phase==AutoTunePhase::High || t.phase==AutoTunePhase::Reposition) &&
      a.motionPhase==MotionPhase::Kick) {
    // The normal kick keeps the identified ceiling for this whole attempt.
    // A failed attempt stops before trying the next 5% level in tickMotion.
    return;
  }
  if(t.phase==AutoTunePhase::Start) {
    const uint32_t elapsed=uint32_t(now-t.phaseAt);
    if((a.current-t.phasePosition)*t.direction>=t.originalMotion.movementDegrees && velocity>t.threshold) {
      measurement.startOutput=std::max(measurement.startOutput,std::max(1.f,a.speedOutputPercent));
      measurement.startMs=std::max(measurement.startMs,elapsed);t.relocations=0;
      // Reuse the lower of the saved and newly confirmed breakaway levels for
      // later pulses. Starting every probe from 5% would waste its bounded
      // startup deadline and then rush to the cap at a known sticky point.
      if(t.originalMotion.kickEnabled)
        t.searchLevel=std::min(t.originalMotion.kickPercent,measurement.startOutput);
      // Begin below the old work level, rather than assuming it is slow enough
      // for identification. Raise it only if this drive cannot sustain motion.
      const float low=std::max(t.minimumLevel,std::min({t.limit-2,float(t.originalSpeed.speedPercent),std::max(5.f,std::min(15.f,measurement.startOutput/2))}));
      autoTuneLevel(low);measurement.lowOutput=t.level;autoTunePhase(AutoTunePhase::Low,now);return;
    }
    const uint32_t maximum=std::min({2800u,t.originalMotion.stallMs,
        t.originalMotion.kickEnabled?t.originalMotion.kickMs:2800u});
    // Identify breakaway at a constant PWM, rather than attributing delayed
    // motion to the top of a fast ramp. Stop/settle between unsuccessful
    // levels; each pulse and the complete procedure retain their deadlines.
    const uint32_t observation=std::min<uint32_t>(maximum,std::max(t.options.startupObservationMs,2*t.originalMotion.velocityWindowMs+300));
    const bool partial=(a.current-t.phasePosition)*t.direction>=t.originalMotion.movementDegrees/4 || velocity>t.threshold;
    if(elapsed>=maximum || (elapsed>=observation && !partial)) {
      if(t.searchLevel<t.limit) {
        t.searchLevel=std::min(t.limit,t.searchLevel+t.options.searchStepPercent);
        autoTuneCoast(AutoTunePhase::Start,t.direction,t.searchLevel,false,now);
      } else finishAutoTune(AutoTunePhase::Failed,"no_start",now);
      return;
    }
    autoTuneLevel(t.searchLevel);return;
  }
  if(t.phase==AutoTunePhase::Reposition && a.motionPhase==MotionPhase::Run && a.direction &&
      motion_[i].progressValid) {
    // Gear resistance can exceed the old work/approach output between probes.
    // Raise this transfer's output through the normal S-ramp before the saved
    // stall deadline. Keep the highest request until it ends, without resetting
    // progress, extending that deadline, or starting another kick.
    const uint32_t rampMs=std::max(20u,std::min(500u,t.originalMotion.stallMs/4));
    const uint32_t age=uint32_t(now-motion_[i].progressAt);
    const float fraction=age>rampMs?std::min(1.f,float(age-rampMs)/rampMs):0;
    const float base=std::min(float(t.originalSpeed.speedPercent),t.limit);
    const unsigned level=unsigned(std::min(std::floor(t.limit),std::ceil(base+(t.limit-base)*fraction)));
    speeds[i].speedPercent=std::max(speeds[i].speedPercent,level);
    if(fraction>0)speeds[i].approachSpeedPercent=speeds[i].speedPercent;
  }
  if(t.phase==AutoTunePhase::Low || t.phase==AutoTunePhase::High) {
    // Kick, reversal dead-time and output ramp are not samples of the plateau.
    if(a.motionPhase!=MotionPhase::Run || motion_[i].releasing || std::abs(a.speedOutputPercent-t.level)>.1f) return;
    if(!t.plateauReady) {
      autoTunePhase(t.phase,now);t.plateauReady=true;t.sampledAt=f.at;
    }
    const uint32_t elapsed=uint32_t(now-t.phaseAt);
    const uint32_t window=std::max(t.options.plateauMinMs,t.originalMotion.velocityWindowMs);
    // Keep a long experimental window bounded without growing the MCU's ring.
    // Round sampling to the controller tick; each recorded point is still fresh.
    const uint32_t sampleEvery=20*((window+20*(t.positions.size()-1)-1)/(20*(t.positions.size()-1)));
    if(f.at!=t.sampledAt && uint32_t(f.at-t.sampledAt)>=sampleEvery) {t.sampledAt=f.at;t.positions[t.sampleNext]=a.current;t.sampleTimes[t.sampleNext]=f.at;t.sampleNext=(t.sampleNext+1)%t.positions.size();t.sampleCount=std::min(unsigned(t.positions.size()),t.sampleCount+1);}
    if(elapsed>t.options.plateauTimeoutMs) {raiseAutoTuneMinimum(now);return;}
    if(elapsed>std::max(400u,2*t.originalMotion.velocityWindowMs) && velocity<t.threshold) {
      raiseAutoTuneMinimum(now);return;
    }
    if(elapsed<std::max(t.options.plateauMinMs,t.originalMotion.velocityWindowMs) || t.sampleCount<8)return;
    // Fit the fresh position samples directly. The control velocity estimator
    // adds a second delay, which previously consumed most of a short stroke.
    const unsigned oldest=t.sampleCount==t.positions.size()?t.sampleNext:0;
    unsigned skip=0;
    while(t.sampleCount-skip>8 && uint32_t(f.at-t.sampleTimes[(oldest+skip)%t.positions.size()])>window)++skip;
    const unsigned first=(oldest+skip)%t.positions.size(),count=t.sampleCount-skip;
    const auto index=[&](unsigned n){return (first+n)%t.positions.size();};
    const auto seconds=[&](unsigned n){return float(uint32_t(t.sampleTimes[index(n)]-t.sampleTimes[first]))/1000;};
    float sx=0,sy=0,sxx=0,sxy=0;
    for(unsigned n=0;n<count;++n) {
      const float x=seconds(n),y=t.direction*(t.positions[index(n)]-t.positions[first]);
      sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
    }
    const float denominator=count*sxx-sx*sx;
    if(denominator<=0)return;
    const float mean=(count*sxy-sx*sy)/denominator;
    const unsigned half=count/2;
    const auto slope=[&](unsigned from,unsigned to) {
      float xsum=0,ysum=0,xxsum=0,xysum=0;
      for(unsigned n=from;n<=to;++n) {
        const float x=seconds(n),y=t.direction*(t.positions[index(n)]-t.positions[first]);
        xsum+=x;ysum+=y;xxsum+=x*x;xysum+=x*y;
      }
      const unsigned n=to-from+1;
      return (n*xysum-xsum*ysum)/(n*xxsum-xsum*xsum);
    };
    const float trend=std::abs(slope(0,half-1)-slope(count-half,count-1));
    const float span=seconds(count-1);
    const float noise=std::max(t.noiseHigh-t.noiseLow,.1f*t.originalMotion.movementDegrees);
    if(mean<=t.threshold || trend>std::max({.08f*mean,.25f*t.threshold,4*noise/span})) {
      return;
    }
    // Confirm with a second, non-overlapping window at the same output. One
    // short, locally straight section of the kick's decay is not steady motion.
    // An ADC step in an intermediate overlapping window does not erase the
    // first fit; expire it instead of matching a much later oscillation cycle.
    const float agreement=std::max({.1f*mean,.25f*t.threshold,2*noise/span});
    const uint32_t confirmation=std::max(uint32_t(t.options.plateauMinMs),uint32_t(span*1000));
    if(!t.plateauAt || uint32_t(f.at-t.plateauAt)>2*confirmation || std::abs(mean-t.plateauVelocity)>agreement) {
      t.plateauAt=f.at;t.plateauVelocity=mean;return;
    }
    if(uint32_t(f.at-t.plateauAt)<confirmation)return;
    t.relocations=0;
    if(t.phase==AutoTunePhase::Low) {
      measurement.lowVelocity=mean;autoTuneLevel(measurement.lowOutput+1);measurement.highOutput=t.level;
      autoTunePhase(AutoTunePhase::High,now);return;
    }
    measurement.highVelocity=mean;
    const float delta=mean-measurement.lowVelocity;
    if(delta<=.05f*measurement.lowVelocity) {
      if(t.level+1<=t.limit) {autoTuneLevel(t.level+1);measurement.highOutput=t.level;autoTunePhase(AutoTunePhase::High,now);return;}
      finishAutoTune(AutoTunePhase::Failed,"insufficient_response",now);return;
    }
    measurement.responseSeconds=std::clamp((mean*(float(elapsed)/1000)-t.direction*(a.current-t.phasePosition))/delta,.02f,5.f);
    t.confirmedHigh[t.direction>0?0:1]={measurement.highOutput,mean,measurement.responseSeconds};
    // The low plateau identifies friction, not the requested working speed.
    // Seek a useful work point (at least half the allowed output), in measured
    // steps. Stop increasing if the guarded stroke cannot fit another fit.
    const float wanted=std::min(t.limit,std::max(float(t.originalSpeed.speedPercent),t.limit*t.options.workingFraction));
    const float next=std::min(wanted,t.level+t.options.searchStepPercent);
    const float predicted=mean+(next-t.level)*delta/(measurement.highOutput-measurement.lowOutput);
    const float usable=t.upper-t.lower-2*guard;
    const float needed=predicted*(.6f+measurement.responseSeconds)+guard;
    if(next>t.level && predicted<300 && needed<usable) {
      measurement.highOutput=next;
      const float room=t.direction>0?t.upper-a.current:a.current-t.lower;
      if(needed<room) {autoTuneLevel(next);autoTunePhase(AutoTunePhase::High,now);}
      else autoTuneCoast(AutoTunePhase::High,t.direction,next,true,now);
      return;
    }
    autoTuneCoast(t.direction>0?AutoTunePhase::Start:AutoTunePhase::Validate,-1,t.searchLevel,false,now);return;
  }
  if(t.phase==AutoTunePhase::Coast || t.phase==AutoTunePhase::Reposition || t.phase==AutoTunePhase::Validate) {
    if(std::abs(a.measuredVelocity)<=t.threshold && !a.direction && !a.tracking) {
      if(!t.settled){t.settled=true;t.settledAt=now;}
    } else t.settled=false;
    if(uint32_t(now-t.phaseAt)>(t.phase==AutoTunePhase::Coast?5000u:60000u)) {finishAutoTune(AutoTunePhase::Failed,"settle_timeout",now);return;}
    if(!t.settled || uint32_t(now-t.settledAt)<std::max(300u,t.originalMotion.settleMs))return;
    if(t.phase==AutoTunePhase::Validate) {
      const float error=a.current-t.validationTarget;
      if(std::abs(error)>std::abs(t.validationError))t.validationError=error;
      if(std::abs(t.validationError)>std::max(2*t.originalMotion.movementDegrees,t.proposedMotion.mode==RegulatorMode::Cascade?
          t.proposedMotion.positionTolerance+t.originalMotion.movementDegrees:2*t.originalMotion.positionTolerance)) {
        finishAutoTune(AutoTunePhase::Failed,"validation_error",now);return;
      }
      if(!t.validationPass) {
        // One successful approach does not validate the opposite direction or
        // a return after kick. Exercise both before offering the candidate.
        t.validationPass=1;t.validationReversals=0;t.validationLastDirection=0;
        t.validationTarget=t.validationOrigin;
        t.direction=t.validationTarget>=a.current?1:-1;
        autoTunePhase(AutoTunePhase::Validate,now);
        t.internal=true;
        const auto result=start({t.validationTarget,t.validationTarget,i==0,i==1},now,false);
        t.internal=false;
        if(result!=Result::Ok)finishAutoTune(AutoTunePhase::Failed,"validation_error",now);
        return;
      }
      finishAutoTune(AutoTunePhase::Complete,"",now);return;
    }
    if(t.phase==AutoTunePhase::Reposition) {
      autoTuneProbe(t.nextPhase,t.nextDirection,t.nextLevel,now);return;
    }
    const float stoppedDistance=std::max(0.f,t.direction*(a.current-t.phasePosition));
    t.stoppingDistance=std::max(t.stoppingDistance,stoppedDistance);
    // A cutoff during a startup search is a safety reserve, not the coast of
    // the working output that will be proposed to the user.
    if(!t.relocate && (t.nextPhase==AutoTunePhase::Start || t.nextPhase==AutoTunePhase::Validate))
      measurement.coastDegrees=stoppedDistance;
    if(t.relocate) {
      // A transfer must stop inside the trial too. The old near-edge goal
      // allowed only the saved coast, even after a much faster startup or
      // adaptive transfer. Leave room for that observed speed and feedback lag.
      const float reserve=std::max({2.f,1.5f*t.originalSpeed.coastDegrees,1.2f*std::max({t.stoppingDistance,t.measurements[0].coastDegrees,t.measurements[1].coastDegrees})})+
          t.originalMotion.movementDegrees+t.peakVelocity*(.15f+float(t.originalMotion.velocityWindowMs)/2000)+
          2*t.originalSpeed.coastDegrees;
      if(2*reserve>=(t.upper-t.lower)*.75f) {
        if(t.nextPhase==AutoTunePhase::High && keepMeasuredHigh())return;
        finishAutoTune(AutoTunePhase::Failed,"insufficient_travel",now);return;
      }
      t.internal=true;stopNow(int(i),now);
      auto m=t.originalMotion;m.mode=RegulatorMode::Simple;m.holdEnabled=false;m.outputLimitPercent=t.limit;
      m.kickEnabled=true;m.kickPercent=std::min(t.limit,t.searchLevel);
      m.kickMs=std::min({2800u,t.originalMotion.stallMs,t.originalMotion.kickEnabled?t.originalMotion.kickMs:2800u});
      motionSettings[i]=m;speeds[i]=t.originalSpeed;speeds[i].speedPercent=std::min(unsigned(t.limit),speeds[i].speedPercent);
      speeds[i].approachSpeedPercent=std::min(speeds[i].approachSpeedPercent,speeds[i].speedPercent);
      const float goal=t.nextDirection>0?t.lower+reserve:t.upper-reserve;
      autoTunePhase(AutoTunePhase::Reposition,now);
      const auto result=start({goal,goal,i==0,i==1},now,false);t.internal=false;
      if(result!=Result::Ok)finishAutoTune(AutoTunePhase::Failed,"start_rejected",now);
    } else if(t.nextPhase==AutoTunePhase::Validate) {
      if(!proposeAutoTune(now))finishAutoTune(AutoTunePhase::Failed,"model_invalid",now);
    } else autoTuneProbe(t.nextPhase,t.nextDirection,t.nextLevel,now);
  }
}
} // namespace forkrot
