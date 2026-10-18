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
      (kickEnabled && kickPercent > outputLimitPercent) || feedbackTimeoutMs < velocityWindowMs) return false;
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
  MotionRecord b{}; b[0] = 'M'; b[1] = 'O'; b[2] = 1; b[3] = uint8_t(s.mode);
  b[4] = uint8_t(s.ready) | uint8_t(s.kickEnabled) << 1 | uint8_t(s.holdEnabled) << 2;
  size_t at = 8;
  auto put = [&](uint32_t n) { for (unsigned i=0; i<4; ++i) b[at++] = uint8_t(n >> (8*i)); };
  for (const auto &f : motionRealFields) { uint32_t n; const float value = s.*f.member; std::memcpy(&n, &value, 4); put(n); }
  for (const auto &f : motionIntFields) put(s.*f.member);
  put(checksum(b.data(), b.size()-4));
  return b;
}
bool decodeMotionSettings(const MotionRecord &b, MotionSettings &s) {
  if (b[0] != 'M' || b[1] != 'O' || b[2] != 1 || b[3] > 1 || b[4] > 7 || b[5] || b[6] || b[7]) return false;
  MotionSettings next; next.mode = RegulatorMode(b[3]); next.ready = b[4]&1; next.kickEnabled = b[4]&2; next.holdEnabled = b[4]&4;
  size_t at = 8;
  auto get = [&]() { uint32_t n=0; for (unsigned i=0; i<4; ++i) n |= uint32_t(b[at++]) << (8*i); return n; };
  for (const auto &f : motionRealFields) { const uint32_t n = get(); std::memcpy(&(next.*f.member), &n, 4); }
  for (const auto &f : motionIntFields) next.*f.member = get();
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
  return std::clamp(ff + kp*error + integral - kd*derivative, lo, hi);
}
void MotionFeedback::sample(float position, bool calibrated, uint32_t stamp, unsigned window) {
  const bool wasValid=valid;
  valid = calibrated && std::isfinite(position);
  if(!valid) {
    // A fresh ADC conversion is still fresh before calibration, but cannot
    // contribute a velocity. This keeps simple calibration jog available.
    if(std::isfinite(position)) at=stamp;
    count=next=0;velocity=0;return;
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
void Controller::cancelMotion(unsigned i) {
  motion_[i].cancel();
  axes[i].holdArmed = false;
  axes[i].requestedVelocity = axes[i].profileAcceleration = 0;
  axes[i].motionPhase = axes[i].motionError == MotionError::None ? MotionPhase::Idle : MotionPhase::Fault;
}
void Controller::resetFeedback(int axis,uint32_t now) {
  if(remote || axis < -1 || axis > 1) return;
  stopNow(axis,now);
  for(unsigned i=0;i<2;++i) if(axis<0 || unsigned(axis)==i) {
    motion_[i].feedback={};axes[i].measuredVelocity=0;
  }
}
void Controller::feedbackAt(unsigned i, float position, bool calibrated, uint32_t stamp) {
  if (remote || i > 1 || (i == 1 && model == Model::G800DXA)) return;
  auto &f = motion_[i].feedback;
  const auto &cfg = motionSettings[i];
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
  const bool active=axes[i].wanted || axes[i].direction || axes[i].tracking || axes[i].holdArmed;
  if (!axes[i].calibrated && active && variableSpeed(i) &&
      (wasCalibrated || axes[i].tracking || axes[i].holdArmed || !std::isfinite(position)))
    motionFault(i, MotionError::FeedbackInvalid, stamp);
}
bool Controller::configureMotion(unsigned i, const MotionSettings &settings, uint32_t now) {
  if (remote || i > 1 || !settings.valid() || (settings.ready && !prepareMotionPlanner())) return false;
  if (motionSettings[i] != settings) { stopNow(int(i), now); motionSettings[i] = settings; }
  return true;
}
Result Controller::resetMotion(int axis, uint32_t now) {
  if (remote || configurationLocked || axis < -1 || axis > 1) return Result::Unavailable;
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
void Controller::motionFault(unsigned i, MotionError error, uint32_t now) {
  stopNow(int(i), now); axes[i].motionError = error; axes[i].motionPhase = MotionPhase::Fault;
  fault = std::string(i ? "el_" : "az_") + motionErrorName(error);
}
void Controller::tickMotion(unsigned i, uint32_t now) {
  auto &axis = axes[i]; auto &r = motion_[i]; const auto &m = motionSettings[i]; const auto &s = speeds[i];
  const bool cascade = m.mode == RegulatorMode::Cascade;
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
  }
  if (cascade && axis.tracking && std::abs(distance) <= m.positionTolerance && std::abs(axis.measuredVelocity) <= m.stoppedVelocity) {
    if (!r.settling) { r.settling=true; r.settledAt=now; }
    if (uint32_t(now-r.settledAt) >= m.settleMs) {
      const bool hold = axis.holdArmed && m.holdEnabled;
      axis.tracking = false; axis.wanted = 0; axis.completed = true; axis.speedOutputPercent=0;
      output(i,0,now); cancelMotion(i); axis.holdArmed=hold;
      axis.motionPhase=hold ? MotionPhase::Holding : MotionPhase::Idle;
      return;
    }
    // Do not kick a stationary mechanism that is already in the arrival band.
    axis.speedOutputPercent=0; axis.wanted=0; output(i,0,now); return;
  } else r.settling = false;
  const bool moving = std::abs(axis.measuredVelocity) > m.stoppedVelocity;
  int desired = cascade && axis.tracking ? sign(distance) : axis.wanted;
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
      axis.motionPhase=MotionPhase::Run; r.releasing=true; r.releaseAt=now; r.kickDone=true;
      r.profileValid=false; r.positionPid.reset(); r.velocityPid.reset();
      if(!cascade) {
        float running=float(s.speedPercent)*manualScale_[i];
        if(axis.tracking) {
          const float fraction=std::clamp((std::abs(distance)-tolerance(i))/std::max(1u,s.slowdownDegrees),0.f,1.f);
          const float approach=float(std::min(s.approachSpeedPercent,s.speedPercent));
          running=approach+(s.speedPercent-approach)*fraction;
        }
        r.ramp=std::min(running,m.kickPercent); r.rampRate=0;
      }
    } else if (uint32_t(now-r.kickAt) >= m.kickMs) { motionFault(i,MotionError::KickTimeout,now); return; }
    else { axis.speedOutputPercent=std::min(m.kickPercent,m.outputLimitPercent); return; }
  }
  // Simple controller ramp uses percent as a virtual velocity. The two boundary
  // derivatives remain continuous when a target or the work output changes.
  const float up = std::max(.1f, float(s.accelerationMs)/1000);
  const float down = std::max(.1f, float(s.decelerationMs)/1000/std::max(.1f,s.decelerationFactor));
  const float work = float(s.speedPercent);
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
    if(cascade) {
      if(!r.profileValid) { r.commandVelocity=axis.measuredVelocity;r.commandAcceleration=0;r.profileValid=true; }
      float virtualPosition=0;
      if(!profileStep(false,virtualPosition,r.commandVelocity,r.commandAcceleration,0,
                     m.velocity,m.acceleration,m.deceleration,m.jerk,dt)) { motionFault(i,MotionError::Planner,now); return; }
      axis.requestedVelocity=r.commandVelocity;axis.profileAcceleration=r.commandAcceleration;
      const float lo=axis.direction<0?-m.outputLimitPercent:0,hi=axis.direction<0?0:m.outputLimitPercent;
      axis.speedOutputPercent=std::abs(r.velocityPid.step(r.commandVelocity-axis.measuredVelocity,axis.measuredVelocity,
          m.feedforward*r.commandVelocity,m.velocityKp,m.velocityKi,m.velocityKd,dt,float(m.derivativeFilterMs)/1000,lo,hi));
      if(std::abs(r.commandVelocity)>.001f || (r.feedback.timed && moving)) return;
    } else {
      if (!rampTo(0)) { motionFault(i,MotionError::Planner,now); return; }
      axis.speedOutputPercent=std::clamp(r.ramp,0.f,m.outputLimitPercent);
      if (r.ramp > .001f) return;
    }
    axis.speedOutputPercent=0; output(i,0,now); r.profileValid=false; r.kickDone=false; r.releasing=false;
    r.positionPid.reset(); r.velocityPid.reset(); return;
  }
  if (!axis.direction && axis.previous && axis.previous != desired &&
      (uint32_t(now-axis.offAt) < 300 || (r.feedback.timed && moving))) {
    axis.motionPhase=MotionPhase::Braking; axis.speedOutputPercent=0; return;
  }
  if (desired && !axis.direction) {
    output(i,desired,now); if (!axis.direction) return;
    if (m.kickEnabled && !moving && axis.calibrated && !r.kickDone) {
      axis.motionPhase=MotionPhase::Kick; r.kickAt=now; r.kickPosition=axis.current; r.kickDirection=desired;
      r.progressAt=now; r.progressPosition=axis.current; r.progressValid=true;
      axis.speedOutputPercent=std::min(m.kickPercent,m.outputLimitPercent); return;
    }
  }
  float requested=0;
  if (cascade) {
    if (!r.profileValid) {
      r.referencePosition=axis.current; r.referenceVelocity=axis.measuredVelocity; r.referenceAcceleration=0;
      r.commandVelocity=axis.measuredVelocity; r.commandAcceleration=0; r.profileValid=true;
    }
    const bool ok=profileStep(axis.tracking,r.referencePosition,r.referenceVelocity,r.referenceAcceleration,
                             axis.tracking ? axis.target : m.velocity*manualScale_[i]*desired,
                             m.velocity,m.acceleration,m.deceleration,m.jerk,dt);
    if (!ok) { motionFault(i,MotionError::Planner,now); return; }
    const float filter=float(m.derivativeFilterMs)/1000;
    float velocity=r.referenceVelocity;
    if (axis.tracking) velocity=r.positionPid.step(r.referencePosition-axis.current,axis.current,velocity,
       m.positionKp,m.positionKi,m.positionKd,dt,filter,-m.velocity,m.velocity,!r.releasing);
    float virtualPosition=0;
    if (!profileStep(false,virtualPosition,r.commandVelocity,r.commandAcceleration,velocity,
                     m.velocity,m.acceleration,m.deceleration,m.jerk,dt)) { motionFault(i,MotionError::Planner,now); return; }
    axis.requestedVelocity=r.commandVelocity; axis.profileAcceleration=r.commandAcceleration;
    desired=axis.direction ? axis.direction : sign(r.commandVelocity);
    if(blocked(desired)) {
      axis.wanted=0;axis.tracking=false;axis.speedOutputPercent=0;output(i,0,now);cancelMotion(i);return;
    }
    const float lo=desired < 0 ? -m.outputLimitPercent : 0, hi=desired < 0 ? 0 : m.outputLimitPercent;
    const float ff=m.feedforward*r.commandVelocity;
    const float signedOutput=r.velocityPid.step(r.commandVelocity-axis.measuredVelocity,axis.measuredVelocity,ff,
       m.velocityKp,m.velocityKi,m.velocityKd,dt,filter,lo,hi,!r.releasing);
    requested=std::abs(signedOutput);
    if (!axis.direction && desired) { output(i,desired,now); if (!axis.direction) requested=0; }
  } else {
    float target=work*manualScale_[i];
    if (axis.tracking) {
      const float fraction=std::clamp((std::abs(distance)-tolerance(i))/std::max(1u,s.slowdownDegrees),0.f,1.f);
      const float approach=float(std::min(s.approachSpeedPercent,s.speedPercent));
      target=approach+(work-approach)*fraction;
    }
    if (!rampTo(std::min(target,m.outputLimitPercent))) { motionFault(i,MotionError::Planner,now); return; }
    requested=r.ramp;
  }
  if (r.releasing) {
    const float t=std::clamp(float(uint32_t(now-r.releaseAt))/std::max(1u,m.kickReleaseMs),0.f,1.f);
    const float blend=t*t*t*(10+t*(-15+6*t));
    requested=m.kickPercent+(requested-m.kickPercent)*blend;
    if (t >= 1) r.releasing=false;
  }
  axis.speedOutputPercent=axis.direction ? std::clamp(requested,0.f,m.outputLimitPercent) : 0;
  axis.motionPhase=MotionPhase::Run;
  if (axis.speedOutputPercent > 0 && axis.calibrated && r.feedback.timed) {
    if (!r.progressValid || std::abs(axis.current-r.progressPosition) >= std::min(.1f,m.movementDegrees)) {
      r.progressAt=now; r.progressPosition=axis.current; r.progressValid=true;
    } else if (uint32_t(now-r.progressAt) >= m.stallMs) { motionFault(i,MotionError::Stalled,now); }
  }
}
} // namespace forkrot
