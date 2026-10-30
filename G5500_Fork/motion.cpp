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
  if (mode > RegulatorMode::Cascade ||
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
#ifdef MOTION_PLANNER_TEST
unsigned plannerCalculations=0;
#endif
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
// allocate. Recalculate changing profiles from that state, never from zero.
bool profileStep(bool positionControl, float &p, float &v, float &a, float target,
                 float velocity, float acceleration, float deceleration, float jerk, float dt) {
  auto &scratch=sharedPlanner();
  if(!scratch) return false;
  // A velocity profile already at its target has zero duration: at_time(0)
  // returns the same boundary. Avoid validating/solving it again each tick,
  // including steady simple PWM. Keep invalid/degenerate inputs on the solver
  // path and require exact equality so acceleration/jerk tails are preserved.
  if(!positionControl && v==target && a==0 && std::isfinite(p) && std::isfinite(v) &&
      std::isfinite(velocity) && velocity>=std::abs(v) &&
      std::isfinite(acceleration) && acceleration>0 && std::isfinite(deceleration) && deceleration>0 &&
      std::isfinite(jerk) && jerk>0 && std::isfinite(dt) && dt>=0)return true;
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
#ifdef MOTION_PLANNER_TEST
  ++plannerCalculations;
#endif
  if (solver.calculate(input, trajectory) < 0) return false;
  std::array<double, 1> np{}, nv{}, na{};
  trajectory.at_time(std::min(double(dt), trajectory.get_duration()), np, nv, na);
  p = float(np[0]); v = float(nv[0]); a = float(na[0]);
  return std::isfinite(p) && std::isfinite(v) && std::isfinite(a);
}
int sign(float n) { return (n > 0) - (n < 0); }
bool feedbackQuiet(const MotionFeedback &f,unsigned window,float degrees) {
  if(!f.valid || f.count<2)return false;
  float low=f.samples[(f.next+f.samples.size()-1)%f.samples.size()].position,high=low;
  unsigned oldest=0;
  for(unsigned n=0;n<f.count;++n) {
    const auto &s=f.samples[n];const unsigned age=uint32_t(f.at-s.at);
    if(age<=window) {low=std::min(low,s.position);high=std::max(high,s.position);oldest=std::max(oldest,age);}
  }
  return oldest>=window/2 && high-low<=degrees;
}
bool autoTuneStartupExpired(const AutoTuneState &t, uint32_t elapsed, float displacement,
                            float velocity, uint32_t riseMs=0) {
  const auto &m=t.originalMotion;
  // Saved kickMs may be much shorter than the sensor's confirmation delay.
  // A moving probe still has to cover movementDegrees at one output level;
  // allow that confirmation within the unchanged stall guard and trial cap.
  const uint32_t maximum=std::min(2800u,m.stallMs);
  const uint32_t observation=std::min(maximum,std::max<uint32_t>(t.options.startupObservationMs,
      riseMs+2*m.velocityWindowMs+300));
  const bool partial=displacement>=m.movementDegrees/4 || velocity>t.threshold;
  return elapsed>=maximum || (elapsed>=observation && !partial);
}
}
#ifdef MOTION_PLANNER_TEST
unsigned motionPlannerCalculations() {return plannerCalculations;}
bool testMotionProfileStep(bool positionControl,float &p,float &v,float &a,float target,
                           float velocity,float acceleration,float deceleration,float jerk,float dt) {
  return profileStep(positionControl,p,v,a,target,velocity,acceleration,deceleration,jerk,dt);
}
#endif
bool prepareMotionPlanner() {
  auto &scratch=sharedPlanner();
  if(!scratch) scratch.reset(new(std::nothrow) PlannerScratch{});
  return bool(scratch);
}
bool Controller::prepareMotion() {
  return remote || (!variableSpeed(0) && !variableSpeed(1)) || prepareMotionPlanner();
}
MotionRecord encodeMotionSettings(const MotionSettings &s) {
  MotionRecord b{}; b[0] = 'M'; b[1] = 'O'; b[2] = 3; b[3] = uint8_t(s.mode);
  b[4] = uint8_t(s.ready) | uint8_t(s.kickEnabled) << 1 | uint8_t(s.holdEnabled) << 2;
  b[5] = uint8_t(s.sustainingPercent);
  b[6] = uint8_t(s.holdReverseMs); b[7] = uint8_t(s.holdReverseMs>>8);
  size_t at = 8;
  auto put = [&](uint32_t n) { for (unsigned i=0; i<4; ++i) b[at++] = uint8_t(n >> (8*i)); };
  for (const auto &f : motionRealFields) { uint32_t n; const float value = s.*f.member; std::memcpy(&n, &value, 4); put(n); }
  for (const auto &f : motionIntFields) if(f.member!=&MotionSettings::sustainingPercent && f.member!=&MotionSettings::holdReverseMs) put(s.*f.member);
  put(checksum(b.data(), b.size()-4));
  return b;
}
bool decodeMotionSettings(const MotionRecord &b, MotionSettings &s) {
  if (b[0] != 'M' || b[1] != 'O' || (b[2] < 1 || b[2] > 3) || b[3] > 1 || b[4] > 7 ||
      (b[2]==1 && b[5]) || (b[2]<3 && (b[6] || b[7]))) return false;
  MotionSettings next; next.mode = RegulatorMode(b[3]); next.ready = b[4]&1; next.kickEnabled = b[4]&2; next.holdEnabled = b[4]&4;
  next.sustainingPercent=b[2]>=2 ? b[5] : 0;
  if(b[2]>=3)next.holdReverseMs=unsigned(b[6])|(unsigned(b[7])<<8);
  size_t at = 8;
  auto get = [&]() { uint32_t n=0; for (unsigned i=0; i<4; ++i) n |= uint32_t(b[at++]) << (8*i); return n; };
  for (const auto &f : motionRealFields) { const uint32_t n = get(); std::memcpy(&(next.*f.member), &n, 4); }
  for (const auto &f : motionIntFields) if(f.member!=&MotionSettings::sustainingPercent && f.member!=&MotionSettings::holdReverseMs) next.*f.member = get();
  if (get() != checksum(b.data(), b.size()-4) || !next.valid()) return false;
  // Older cascade records tied holding to the much wider arrival tolerance.
  // Migrate once; version 3 preserves an explicitly chosen holding threshold.
  if (b[2]<3 && next.mode==RegulatorMode::Cascade)
    next.holdResumeDegrees=std::min(next.holdResumeDegrees,1.f);
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
    count=next=0;velocity=0;coastSeconds=0;staticCoastFactor={};powered=coastTracking=false;return;
  }
  if(!wasValid) { count=next=0;velocity=0; }
  // Fast deadlines can produce a short interval after a late conversion.
  // Every distinct real conversion is useful; cached timestamps are not.
  if (count && stamp==at) return;
  if (count && uint32_t(stamp-at) > 5000) { count = 0; next = 0; velocity = 0; }
  at = stamp;
  samples[next] = {position, stamp}; next = (next+1)%samples.size(); count = std::min(count+1, unsigned(samples.size()));
  velocity=velocityInWindow(window);
}
float MotionFeedback::velocityInWindow(unsigned window) const {
  if(!valid || !count)return 0;
  const float position=samples[(next+samples.size()-1)%samples.size()].position;
  // Both coordinates are relative and bounded by the short sensor window.
  // Single precision is sufficient here and uses ESP32's hardware FPU; double
  // regression over 122 samples on both axes consumes the 200 Hz time budget.
  float sx=0, sy=0, sxx=0, sxy=0; unsigned n=0;
  for (unsigned i=0; i<count; ++i) {
    const uint32_t age = uint32_t(at-samples[i].at);
    if (age > window) continue;
    const float x = -float(age)/1000, y = samples[i].position-position;
    sx+=x; sy+=y; sxx+=x*x; sxy+=x*y; ++n;
  }
  const float denominator = n*sxx-sx*sx;
  return n>=2 && denominator>1e-10f ? float((n*sxy-sx*sy)/denominator) : 0;
}
void MotionRuntime::cancel(bool retainTarget) {
  const auto targetAt=targetChangedAt;
  const bool timed=targetTimed;
  static_cast<MotionState &>(*this) = {};
  if(retainTarget) {targetChangedAt=targetAt;targetTimed=timed;}
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
float MotionFeedback::positionMean(unsigned window) const {
  if(!count)return 0;
  const float latest=samples[(next+samples.size()-1)%samples.size()].position;
  float sum=0;unsigned n=0;
  for(unsigned i=0;i<count;++i)if(uint32_t(at-samples[i].at)<=window) {sum+=samples[i].position-latest;++n;}
  return n ? latest+sum/n : latest;
}
float MotionFeedback::staticCoastDistance(float velocity) const {
  return velocity*velocity*staticCoastFactor[velocity>0 ? 1 : 0];
}
void Controller::cancelMotion(unsigned i,bool retainTarget) {
  motion_[i].cancel(retainTarget);
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
  if (remote || i > 1 || (i == 1 && model != Model::G5500)) return;
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
      if (!f.rejectedSamples || stamp != f.rejectedAt) {
        ++f.rejectedSamples; ++f.rejectedTotal; f.rejectedAt=stamp;
        f.rejectedPosition=position;f.lastGoodPosition=axes[i].current;
        f.rejectedIntervalMs=uint32_t(stamp-f.at);
      }
      // Three conversions used to cover 60 ms at 50 Hz. At 200 Hz the same
      // count covers only 15 ms and latches ordinary switching transients.
      // Keep the physical observation time independent of the selected rate;
      // rejected samples still never refresh the last accepted feedback age.
      if (f.rejectedSamples < 3 || uint32_t(stamp-f.at)<60) return;
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
    else if(f.coastTracking && axes[i].holdArmed && cfg.holdEnabled &&
            uint32_t(stamp-f.coastAt)>=std::max(80u,cfg.velocityWindowMs)) {
      const float velocity=f.coastDirection*f.velocity;
      const float travel=f.coastDirection*(position-f.coastPosition);
      // Holding can reverse before rest. Fit the observed exponential coast
      // from distance / velocity lost instead of waiting for its endpoint.
      if(velocity>cfg.stoppedVelocity && velocity<.5f*f.coastVelocity && travel>cfg.movementDegrees/2)
        f.coastSeconds=std::clamp(travel/(f.coastVelocity-velocity),.02f,2.f);
    }
    if(!powered && f.coastTracking && uint32_t(stamp-f.coastAt)>=80 && std::abs(f.velocity)<=cfg.stoppedVelocity) {
      const float travel=f.coastDirection*(position-f.coastPosition);
      if(travel>cfg.movementDegrees/2 && f.coastVelocity>.001f) {
        const float seconds=std::clamp(travel/f.coastVelocity,.02f,2.f);
        f.coastSeconds=f.coastSeconds>0 ? .5f*(f.coastSeconds+seconds) : seconds;
        auto &factor=f.staticCoastFactor[f.coastDirection>0 ? 1 : 0];
        const float measured=seconds/f.coastVelocity;
        factor=factor>0 ? .5f*(factor+measured) : measured;
      }
      f.coastTracking=false;
    }
    f.powered=powered;
  }
  const bool active=axes[i].wanted || axes[i].direction || axes[i].tracking || axes[i].holdArmed;
  if (!axes[i].calibrated && active && (variableSpeed(i) || hardware.sensorType(i)) &&
      (wasCalibrated || axes[i].tracking || axes[i].holdArmed || !std::isfinite(position))) {
    ++f.invalidEvents;
    motionFault(i, MotionError::FeedbackInvalid, stamp);
  }
}
bool Controller::configureMotion(unsigned i, const MotionSettings &settings, uint32_t now) {
  if (remote || i > 1 || !settings.valid() || (variableSpeed(i) && !prepareMotionPlanner())) return false;
  if (motionSettings[i] != settings) {
    const auto &old=motionSettings[i];
    const bool speedProfileChanged=old.mode!=settings.mode || old.velocity!=settings.velocity ||
        old.outputLimitPercent!=settings.outputLimitPercent;
    stopNow(int(i), now);motionSettings[i]=settings;
    if(speedProfileChanged)resetRuntimeSpeed(i,now);
  }
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
Result Controller::setControlFrequency(unsigned hz, uint32_t now) {
  // Saving the sampling rate stops outputs and needs no setup motion permit.
  if(remote || configurationLocked)return Result::Unavailable;
  if(!validMotionHz(hz))return Result::Invalid;
  if(autoTune.active())return Result::Busy;
  if(controlPeriodMs_==1000/hz)return Result::Ok;
  stopNow(-1,now);
  controlPeriodMs_=1000/hz;
  for(unsigned i=0;i<2;++i) {
    auto &r=motion_[i];
    const auto at=r.feedback.at;const bool timed=r.feedback.timed, valid=r.feedback.valid;
    r={};r.feedback.at=at;r.feedback.timed=timed;r.feedback.valid=valid;
    axes[i].measuredVelocity=0;
  }
  // A completed identification belongs to the frequency at which it ran.
  if(autoTune.phase==AutoTunePhase::Complete) {
    autoTune.phase=AutoTunePhase::Cancelled;autoTune.reason="control_frequency_changed";
  }
  return Result::Ok;
}
void Controller::tickMotion(unsigned i, uint32_t now) {
  const unsigned period=controlPeriodMs();
  auto &axis = axes[i]; auto &r = motion_[i]; const auto &m = motionSettings[i]; const auto &s = speeds[i];
  const bool cascade = m.mode == RegulatorMode::Cascade;
  const float workingSpeed = manualAbsolute_[i] && !axis.tracking ? runtimeSpeedLimit(i) : runtimeSpeed(i);
  const auto manualSpeed = [&]() {
    const float work = workingSpeed, fraction = manualScale_[i];
    if (axis.tracking || (autoTune.active() && autoTune.axis == int(i))) return work * fraction;
    if (fraction <= 0) return 0.f;
    // Touch spans the full configured drive range, independent of working speed.
    if (manualAbsolute_[i]) return runtimeSpeedLimit(i) * fraction;
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
    axis.speedOutputPercent = 0; output(i,0,now); cancelMotion(i,axis.completed); return;
  }
  if (r.started && int32_t(now-r.nextAt)<0) return;
  const uint32_t interval = r.started ? uint32_t(now-r.at) : 0;
  // Keep the nominal phase after jitter. Skip missed deadlines in one step;
  // never replay PID/planner updates against the same measurement.
  r.nextAt = r.started
      ? r.nextAt + (uint32_t(now-r.nextAt)/period+1)*period
      : now+period;
  r.timing.record(interval,period);
  const float dt = r.started ? std::min(.1f, float(interval)/1000) : float(period)/1000;
  r.at = now; r.started = true;
  axis.measuredVelocity = r.feedback.velocity;
  const float distance = axis.target-axis.current;
  if (axis.motionPhase == MotionPhase::Holding && axis.holdArmed) {
    if (std::abs(distance) <= m.holdResumeDegrees) { output(i,0,now); axis.speedOutputPercent=0; return; }
    axis.tracking = true; axis.completed = false; r.profileValid = false; r.settling = false;
    r.targetDirection=sign(distance); r.crossedTarget=false;
  }
  // A moving stream of small absolute goals is not a sequence of arrivals.
  // Allow normal packet spacing and jitter, but expire a stopped stream.
  // Duplicates never refresh this deadline. STOP bypasses it entirely.
  const unsigned streamTimeout=std::clamp(6*r.targetIntervalMs,1800u,4500u);
  const unsigned streamElapsed=uint32_t(now-r.targetChangedAt);
  const bool passingTarget=axis.tracking && r.streamingTarget && (axis.direction || r.targetVelocity!=0) && streamElapsed<streamTimeout;
  const bool activeHold=cascade && axis.holdArmed && m.holdEnabled && !passingTarget;
  const float arrivalTolerance=activeHold ? std::min(m.positionTolerance,m.holdResumeDegrees/2) : m.positionTolerance;
  const float streamFade=streamElapsed<=r.targetIntervalMs ? 1.f :
      (streamElapsed<streamTimeout ? float(streamTimeout-streamElapsed)/std::max(1u,streamTimeout-r.targetIntervalMs) : 0.f);
  const float streamAdvance=std::clamp(r.targetVelocity*std::min(streamElapsed,r.targetIntervalMs)/1000,
      -m.positionTolerance/4,m.positionTolerance/4)*streamFade;
  const float streamPosition=std::clamp(axis.target+streamAdvance,range(i).start,range(i).end);
  // Wait out the existing packet-cadence window before changing one-shot
  // behavior. In-flight streams (including their first point) keep the same
  // coast, kick and progress rules. Duplicate goals do not renew this window.
  const bool staticTarget=cascade && axis.tracking && !axis.holdArmed && !autoTune.active() &&
      r.targetTimed && streamElapsed>=streamTimeout;
  const bool moving = std::abs(axis.measuredVelocity) > m.stoppedVelocity;
  const float predictedCoast=staticTarget ? r.feedback.staticCoastDistance(axis.measuredVelocity) :
      std::abs(axis.measuredVelocity)*r.feedback.coastSeconds;
  // Switching feedback can resemble velocity before the shaft breaks away.
  // A stationary goal must not coast out of an unconfirmed start. Arrival
  // inside tolerance and crossing the actual goal still cut power immediately.
  const bool coastConfirmed=!staticTarget || (moving &&
      (!m.kickEnabled || r.kickDone ||
       r.feedback.progressed(sign(axis.measuredVelocity),m.movementDegrees,m.velocityWindowMs)) &&
      r.feedback.progressed(sign(axis.measuredVelocity),m.movementDegrees/4,m.velocityWindowMs));
  const bool coastToTarget=coastConfirmed && distance*axis.measuredVelocity>0 && std::abs(distance)<=predictedCoast;
  // A one-shot positioning move may coast beyond its goal, but only active
  // holding is allowed to reverse and correct that overshoot. Latch the actual
  // crossing, then wait for measured rest before reporting completion.
  if (cascade && axis.tracking && !axis.holdArmed && !passingTarget &&
      r.targetDirection && distance*r.targetDirection<=0) r.crossedTarget=true;
  if (!staticTarget) {
    r.staticProgressValid=false;r.staticKickInterrupted=false;r.staticKickFloor=0;
  }
  else if(axis.calibrated && r.feedback.timed) {
    // Observe the entire stationary-goal attempt, including zero-output
    // braking and renewed kicks. Neither can grant a fresh stall interval.
    const float remaining=std::abs(axis.target-r.feedback.positionMean(m.velocityWindowMs));
    if(!r.staticProgressValid || r.staticProgressRemaining-remaining>=std::min(.1f,m.movementDegrees)) {
      r.staticProgressValid=true;r.staticProgressAt=now;r.staticProgressRemaining=remaining;
    } else if(uint32_t(now-r.staticProgressAt)>=m.stallMs &&
              std::abs(distance)>m.positionTolerance && !r.crossedTarget) {
      motionFault(i,MotionError::Stalled,now);return;
    }
  }
  // Holding must react to a renewed error even while the previous coast is
  // still moving. Settlement confirms completion; it never gates correction.
  if (activeHold && r.arriving && std::abs(distance)>m.holdResumeDegrees)
    r.arriving=r.settling=false;
  if (cascade && axis.tracking && (std::abs(distance)<=arrivalTolerance || coastToTarget || r.arriving || r.crossedTarget) && !passingTarget) {
    if (!r.arriving) {
      r.staticKickInterrupted=staticTarget && axis.motionPhase==MotionPhase::Kick && !r.kickDone;
      r.arriving=true; r.profileValid=false; r.kickDone=false; r.releasing=false;
      r.positionPid.reset(); r.velocityPid.reset();
    }
    const bool stopped=!moving;
    if (!stopped) r.settling=false;
    else if (!r.settling) { r.settling=true; r.settledAt=now; }
    const float resumeTolerance=activeHold ? m.holdResumeDegrees : m.positionTolerance;
    if (r.settling && uint32_t(now-r.settledAt) >= m.settleMs && std::abs(distance)>resumeTolerance && !r.crossedTarget) {
      // Wait for the real coast to finish before judging the remaining error.
      if(staticTarget && r.staticKickInterrupted) {
        // A switching excursion can briefly enter the arrival band before
        // breakaway. After an observed failed arrival, resume at a bounded
        // next start level instead of repeating the same sub-threshold pulse.
        const float step=std::max(1.f,(m.kickPercent-float(m.sustainingPercent))/std::clamp(m.kickMs/60,1u,8u));
        r.staticKickFloor=std::min(m.kickPercent,std::max(r.staticKickFloor,r.kickPeak)+step);
      }
      r.staticKickInterrupted=false;
      r.arriving=r.settling=false;
    } else if (r.settling && uint32_t(now-r.settledAt) >= m.settleMs) {
      const bool hold = axis.holdArmed && m.holdEnabled;
      axis.tracking = false; axis.wanted = 0; axis.completed = true; axis.speedOutputPercent=0;
      output(i,0,now); cancelMotion(i,true); axis.holdArmed=hold;
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
  if(passingTarget) desired=axis.direction ? axis.direction : sign(r.targetVelocity);
  if (!desired) desired = axis.wanted;
  const auto blocked=[&](int direction) {
    return axis.calibrated && ((direction<0 && axis.current<=range(i).start) || (direction>0 && axis.current>=range(i).end));
  };
  if(blocked(desired)) {
    axis.wanted=0;axis.tracking=false;axis.speedOutputPercent=0;output(i,0,now);cancelMotion(i);return;
  }
  const float coastLead=std::max(0.f,desired*(axis.measuredVelocity-r.targetVelocity*streamFade))*
      std::max(r.feedback.coastSeconds,m.velocityWindowMs/2000.f);
  const float streamLead=-desired*(streamPosition-axis.current)+coastLead;
  const float streamBand=std::min(m.positionTolerance,std::max(m.movementDegrees,
      std::min(m.positionTolerance/2,std::abs(r.targetVelocity)*r.targetIntervalMs/1000)));
  if(!passingTarget || streamLead<=0)r.streamCoasting=false;
  else if(streamLead>=streamBand)r.streamCoasting=true;
  const bool streamBrake=cascade && passingTarget && r.streamCoasting;
  if(streamBrake && axis.motionPhase==MotionPhase::Kick) {
    // The overrun guard must also cut a starting/recovery pulse. Waiting for
    // movement confirmation here can keep driving beyond the moving goal.
    axis.motionPhase=MotionPhase::Run;axis.speedOutputPercent=0;
    r.kickDone=true;r.kickConfirming=r.kickRecovery=r.releasing=false;
    r.velocityPid.reset();
  }
  if (axis.motionPhase == MotionPhase::Kick) {
    const bool observedKick=cascade && axis.tracking && m.sustainingPercent && (!axis.holdArmed || r.streamingTarget) && (!autoTune.active() || autoTune.phase==AutoTunePhase::Tracking);
    const float kickPosition=observedKick ? r.feedback.positionMean(std::clamp(m.velocityWindowMs/2,40u,80u)) : axis.current;
    bool confirmedMove=(kickPosition-r.kickPosition)*r.kickDirection >= m.movementDegrees;
    if(cascade && axis.tracking && (!autoTune.active() || autoTune.phase==AutoTunePhase::Tracking)) {
      // Switching noise can cross movementDegrees on a stationary shaft.
      // Require directed progress to persist across an observation window;
      // a single favourable ADC conversion must not end breakaway.
      confirmedMove=confirmedMove && r.feedback.progressed(r.kickDirection,m.movementDegrees/4,m.velocityWindowMs);
      if(!confirmedMove)r.kickConfirming=false;
      else if(!r.kickConfirming) {r.kickConfirming=true;r.kickConfirmAt=now;}
      // Stop adding starting torque as soon as movement is plausible. Keep
      // the running bias while validating it, rather than accelerating for
      // an extra sensor window at the full breakaway output.
      if(observedKick && r.kickConfirming)axis.speedOutputPercent=std::min(axis.speedOutputPercent,
          std::max(float(m.sustainingPercent),m.feedforward*workingSpeed));
      const bool fast=!m.sustainingPercent && (axis.current-r.kickPosition)*r.kickDirection>=2*m.movementDegrees &&
          r.kickDirection*axis.measuredVelocity>workingSpeed;
      confirmedMove=confirmedMove && (fast || uint32_t(now-r.kickConfirmAt)>=std::clamp(m.velocityWindowMs/2,40u,80u));
    }
    if (desired && desired != r.kickDirection) { axis.motionPhase=MotionPhase::Braking; r.releasing=false; r.ramp=axis.speedOutputPercent; r.rampRate=0; }
    else if (confirmedMove) {
      r.staticKickFloor=0;
      r.releaseOutput=axis.speedOutputPercent;
      axis.motionPhase=MotionPhase::Run; r.releasing=true; r.releaseAt=now; r.kickDone=true;
      if(!r.kickRecovery || !passingTarget) {r.profileValid=false;r.positionPid.reset();}
      r.kickRecovery=false;r.velocityPid.reset();
      if(!cascade) {
        float running=manualSpeed();
        if(axis.tracking) {
          const float fraction=std::clamp((std::abs(distance)-tolerance(i))/std::max(1u,s.slowdownDegrees),0.f,1.f);
          const float approach=std::min(float(s.approachSpeedPercent),workingSpeed);
          running=approach+(workingSpeed-approach)*fraction;
        }
        r.ramp=std::min(running,m.kickPercent); r.rampRate=0;
      }
    } else if ((autoTune.active() && autoTune.axis==int(i) && autoTune.phase!=AutoTunePhase::Validate && autoTune.phase!=AutoTunePhase::Tracking) ?
               autoTuneStartupExpired(autoTune,uint32_t(now-r.kickAt),
                   (axis.current-r.kickPosition)*r.kickDirection,axis.measuredVelocity*r.kickDirection,m.kickMs*2/3) :
               uint32_t(now-r.kickAt)>=std::max(m.kickMs,m.stallMs)) {
      auto &t=autoTune;
      if(t.active() && t.axis==int(i) && t.phase==AutoTunePhase::Minimum) {
        // An experimental low-speed start is allowed to fail. Retry the next
        // candidate instead of latching a normal-drive kick fault.
        if(t.options.has(TuneKick) && !t.trackingVerified && t.proposedMotion.kickPercent<std::floor(t.limit)) {
          t.proposedMotion.kickPercent=std::min(std::floor(t.limit),
              t.proposedMotion.kickPercent+t.options.searchStepPercent);
          t.searchLevel=t.proposedMotion.kickPercent;
          t.proposalValidated=false;t.minimumPass=0;
          autoTuneCoast(AutoTunePhase::Minimum,t.direction,t.minimumCandidate,false,now);return;
        }
        finishAutoTuneMinimumTrial(false,now);return;
      }
      if(t.active() && t.axis==int(i) && t.phase==AutoTunePhase::Tracking) {
        finishAutoTuneTrackingTrial(false,now,"no_progress");return;
      }
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
      float level=r.kickBase+(m.kickPercent-r.kickBase)*step;
      if(observedKick) {
        const float rise=(m.kickPercent-r.kickBase)*1000/std::max(20u,m.kickMs*2/3);
        level=r.kickConfirming ? axis.speedOutputPercent : std::min(level,axis.speedOutputPercent+rise*dt);
      }
      axis.speedOutputPercent=std::clamp(level,0.f,m.outputLimitPercent);
      r.kickPeak=std::max(r.kickPeak,axis.speedOutputPercent);
      return;
    }
  }
  // Simple controller ramp uses percent as a virtual velocity. The two boundary
  // derivatives remain continuous when a target or the work output changes.
  const float up = std::max(.1f, float(s.accelerationMs)/1000);
  const float down = std::max(.1f, float(s.decelerationMs)/1000/std::max(.1f,s.decelerationFactor));
  const float work = workingSpeed;
  const float velocityLimit = cascade ? workingSpeed : m.velocity;
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
    // retains the selected direction dead time. Only one-shot/jog waits for
    // measured rest too; holding may correct a moving shaft after that pause.
    axis.speedOutputPercent=0; output(i,0,now); r.profileValid=false; r.kickDone=false; r.releasing=false;
    axis.requestedVelocity=axis.profileAcceleration=0;
    r.positionPid.reset(); r.velocityPid.reset(); return;
  }
  if (!axis.direction && axis.previous && axis.previous != desired &&
      (uint32_t(now-axis.offAt) < reverseDeadTimeMs(i) || (!activeHold && r.feedback.timed && moving))) {
    axis.motionPhase=MotionPhase::Braking; axis.speedOutputPercent=0; return;
  }
  if (desired && !axis.direction) {
    output(i,desired,now); if (!axis.direction) return;
    // A velocity estimate alone can be ADC noise on a stationary shaft. Only
    // confirmed directed displacement permits skipping breakaway on a start.
    const bool confirmedMoving=moving && r.feedback.progressed(desired,m.movementDegrees,m.velocityWindowMs);
    if (m.kickEnabled && !streamBrake && !confirmedMoving && !(activeHold && moving) && axis.calibrated && !r.kickDone) {
      axis.motionPhase=MotionPhase::Kick; r.kickAt=now;
      r.kickPosition=cascade && axis.tracking && m.sustainingPercent && (!axis.holdArmed || r.streamingTarget) && (!autoTune.active() || autoTune.phase==AutoTunePhase::Tracking)
          ? r.feedback.positionMean(std::clamp(m.velocityWindowMs/2,40u,80u)) : axis.current;
      r.kickDirection=desired;
      r.kickConfirming=false;
      r.kickRecovery=false;
      r.progressAt=now; r.progressPosition=axis.current; r.progressValid=true;
      r.kickBase=std::min(m.kickPercent,std::max(5.f,cascade?float(m.sustainingPercent):manualSpeed()));
      if(staticTarget)r.kickBase=std::max(r.kickBase,r.staticKickFloor);
      if(m.kickMs<=40)r.kickBase=m.kickPercent; // One control tick cannot fit a staircase.
      r.kickPeak=axis.speedOutputPercent=std::min(r.kickBase,m.outputLimitPercent); return;
    }
  }
  float requested=0,velocityIntegralBefore=0;
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
      if(age>0) {
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
      const float handoffLimit=passingTarget ? std::min(velocityLimit,std::abs(r.targetVelocity)+
          m.positionKp*std::max(0.f,desired*(streamPosition-axis.current))) : velocityLimit;
      r.referenceVelocity=confirmed ? std::clamp(handoffVelocity,-handoffLimit,handoffLimit) : 0;
      r.referenceAcceleration=0;
      r.commandVelocity=r.referenceVelocity;r.commandAcceleration=0;r.profileValid=true;

    }
    const float filter=float(m.derivativeFilterMs)/1000;
    if(axis.tracking && passingTarget) {
      // Follow a moving coordinate with a velocity feedforward. Replanning a
      // zero-speed arrival (or a nonzero terminal speed at a frozen waypoint)
      // on every tick makes the shaft chase/brake at each UDP packet. Advance
      // the last coordinate for at most one measured packet interval; a lost
      // stream still returns to its exact final position after the deadline.
      const float velocity=r.positionPid.step(streamPosition-axis.current,axis.current,
          streamFade*std::clamp(r.targetVelocity,-velocityLimit,velocityLimit),
          m.positionKp,m.positionKi,m.positionKd,dt,filter,-velocityLimit,velocityLimit,!r.releasing);
      if(!profileStep(false,r.referencePosition,r.commandVelocity,r.commandAcceleration,velocity,
                      m.velocity,m.acceleration,m.deceleration,m.jerk,dt)) { motionFault(i,MotionError::Planner,now); return; }
      r.referencePosition=axis.current;r.referenceVelocity=r.commandVelocity;r.referenceAcceleration=r.commandAcceleration;
    } else if(axis.tracking) {
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
    // A stream may select a speed below the identified running minimum.
    // Let PI trim below that floor instead of enforcing repeated overruns.
    // Keep the independently identified jog/one-shot/autotune floor unchanged.
    const bool driving=desired*r.commandVelocity>.001f;
    const float floor=driving && !passingTarget
        ? float(m.sustainingPercent) : 0.f;
    const float lo=desired < 0 ? -m.outputLimitPercent : floor, hi=desired < 0 ? -floor : m.outputLimitPercent;
    const float ff=m.feedforward*r.commandVelocity;
    if (r.releasing && now==r.releaseAt && m.velocityKi>0) {
      // Transfer the actual breakaway output into the PI state instead of
      // dropping to feedforward/floor for several seconds while I rebuilds.
      // The freshly reset derivative is zero. This is an initial condition,
      // not integration during Kick: normal error integration resumes after
      // release and removes excess torque. Never retain a pulse when its
      // measured speed already exceeds the working speed.
      const bool belowLimit=!passingTarget && desired*handoffVelocity<velocityLimit;
      // At overspeed retain a working bias, never the full starting pulse.
      // Streams use the identified running minimum; a standalone trajectory
      // can also use its identified feedforward at the selected speed.
      const float seed=belowLimit ? desired*r.releaseOutput-ff-m.velocityKp*(r.commandVelocity-axis.measuredVelocity)
                                  : desired*(passingTarget?float(m.sustainingPercent):
                                      std::max(floor,std::min(r.releaseOutput,m.feedforward*velocityLimit)))-ff;
      r.velocityPid.integral=std::clamp(seed,-m.outputLimitPercent,m.outputLimitPercent);
    }
    if(passingTarget && (streamBrake || !driving)) {
      // A breakaway pulse is not a sustainable working bias. Discharge it
      // while coasting/overspeed, rather than freezing it until the next
      // packet catches up and abruptly reapplying the old starting torque.
      const float runningBias=std::max(0.f,float(m.sustainingPercent)-desired*ff);
      r.velocityPid.integral=desired*runningBias;
    }
    velocityIntegralBefore=r.velocityPid.integral;
    const float signedOutput=r.velocityPid.step(r.commandVelocity-axis.measuredVelocity,axis.measuredVelocity,ff,
       m.velocityKp,m.velocityKi,m.velocityKd,dt,filter,lo,hi,!r.releasing && !streamBrake);
    // Stored PI torque must never keep powering the old direction when the
    // trajectory already requests braking through zero. Retain direction and
    // kick state while coasting; normal target reversal still uses interlock.
    requested=(!passingTarget || driving) && !streamBrake ? std::abs(signedOutput) : 0;
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
  if(cascade && passingTarget && !r.releasing && requested>axis.speedOutputPercent) {
    // The velocity S-curve alone cannot bound torque when static friction
    // holds the shaft. Limit rising PWM too; braking/STOP remain immediate.
    const float rise=std::max(1.f,m.kickPercent-float(m.sustainingPercent))*1000/std::max(100u,m.kickMs);
    const float limited=std::min(requested,std::max(float(m.sustainingPercent),axis.speedOutputPercent)+rise*dt);
    if(limited<requested && desired*(r.commandVelocity-axis.measuredVelocity)>0)
      r.velocityPid.integral=velocityIntegralBefore;
    requested=limited;
  }
  axis.speedOutputPercent=axis.direction ? std::clamp(requested,0.f,m.outputLimitPercent) : 0;
  axis.motionPhase=MotionPhase::Run;
  const bool tuning = autoTune.active() && autoTune.axis == int(i) && autoTune.phase!=AutoTunePhase::Tracking;
  // A small correction can legitimately remain below the drive's dead band.
  // Start a full progress interval only when meaningful output is restored.
  // Autotune still observes low output: no motion there is a search result.
  if (!tuning && axis.speedOutputPercent < 7.f-1e-4f) r.progressValid=false;
  else if ((tuning || axis.speedOutputPercent > 0) && axis.calibrated && r.feedback.timed) {
    // Only a new furthest position in the commanded direction is progress.
    // Alternating ADC counts at a stationary shaft must not keep it powered.
    const float progressPosition=passingTarget ? r.feedback.positionMean(std::clamp(m.velocityWindowMs/2,40u,80u)) : axis.current;
    const float progressDegrees=passingTarget ? m.movementDegrees : std::min(.1f,m.movementDegrees);
    if (!r.progressValid || axis.direction*(progressPosition-r.progressPosition) >= progressDegrees) {
      r.progressAt=now; r.progressPosition=progressPosition; r.progressValid=true;
    } else if (uint32_t(now-r.progressAt) >= m.stallMs) {
      if(tuning) raiseAutoTuneMinimum(now);
      else if(autoTune.active() && autoTune.axis==int(i) && autoTune.phase==AutoTunePhase::Tracking)
        finishAutoTuneTrackingTrial(false,now,"no_progress");
      else motionFault(i,MotionError::Stalled,now);
    } else if(!tuning && cascade && m.kickEnabled && !r.releasing &&
        desired*distance>(activeHold?m.holdResumeDegrees:m.movementDegrees) &&
        ((passingTarget && r.kickDone && uint32_t(now-r.progressAt)>=
            uint32_t(std::clamp(2000*m.movementDegrees/std::max(.1f,std::abs(r.commandVelocity)),500.f,float(m.stallMs)))) ||
         (activeHold && !moving && uint32_t(now-r.progressAt)>=std::max(80u,m.velocityWindowMs) &&
          feedbackQuiet(r.feedback,m.velocityWindowMs,std::min(.1f,m.movementDegrees/4))))) {
      // Static friction can return in the middle of a slow stream. Do not
      // let PI wind up for seconds and retain that starting torque after the
      // shaft breaks free. Reuse the observed, energy-bounded start while
      // retaining the continuous velocity/acceleration boundary state.
      // Holding first brakes a rolling shaft without kick. If it then sticks,
      // observed lack of progress requests breakaway without waiting for I.
      axis.motionPhase=MotionPhase::Kick;r.kickRecovery=true;r.kickConfirming=false;
      r.kickAt=now;r.kickPosition=progressPosition;r.kickDirection=desired;
      r.kickBase=std::min(m.kickPercent,std::max(float(m.sustainingPercent),axis.speedOutputPercent));
      r.kickPeak=axis.speedOutputPercent=r.kickBase;r.kickDone=false;
      r.progressAt=now;r.progressPosition=progressPosition;
    }
  }
}
const char *Controller::autoTunePhaseName(AutoTunePhase phase) {
  static constexpr const char *names[]={"idle","quiet","start","low","high","coast","reposition","validate","minimum","tracking","complete","cancelled","failed"};
  return names[unsigned(phase)];
}
Result Controller::centerForAutoTune(unsigned i, uint32_t now) {
  if(remote || movementBlocked() || i>1 || (i==1 && model!=Model::G5500) || !variableSpeed(i)) return Result::Unavailable;
  if(autoTune.active()) return Result::Busy;
  if(!axes[i].calibrated) return Result::NotCalibrated;
  if(driveFault || !fault.empty() || !motionSettings[i].ready ||
      axes[0].motionError!=MotionError::None || axes[1].motionError!=MotionError::None)
    return Result::Unavailable;
  const auto &feedback=motion_[i].feedback;
  if(!feedback.timed || !feedback.valid || uint32_t(now-feedback.at)>motionSettings[i].feedbackTimeoutMs)
    return Result::Unavailable;
  const auto bounds=range(i);const float middle=bounds.start+bounds.span()/2;
  stopNow(-1,now);
  setMode(Mode::Manual,now);
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
  auto selected=options;
  if(selected.has(TuneModel))selected.stages|=TuneKick;
  if(mode==RegulatorMode::Simple)selected.stages&=~(TuneSlow|TuneFast);
  if (remote || movementBlocked() || i>1 || (i==1 && model!=Model::G5500) || !variableSpeed(i)) return Result::Unavailable;
  if (autoTune.active() || moving() || axes[0].tracking || axes[1].tracking || axes[0].holdArmed || axes[1].holdArmed ||
      mode != motionSettings[i].mode) return Result::Busy;
  const auto &m=motionSettings[i];const auto &f=motion_[i].feedback;
  if (!axes[i].calibrated) return Result::NotCalibrated;
  if (driveFault || !fault.empty() || axes[0].motionError!=MotionError::None || axes[1].motionError!=MotionError::None ||
      !m.ready || !m.valid() || !f.timed || !f.valid || uint32_t(now-f.at)>m.feedbackTimeoutMs) return Result::Unavailable;
  if (!options.valid(limit) || !selected.valid(limit) || !std::isfinite(travel) || !std::isfinite(limit) || travel<10 || travel>360 ||
      limit<10 || limit>m.outputLimitPercent || limit>100 || travel<6*m.movementDegrees) return Result::Invalid;
  // A partial run must exercise the saved breakaway and running floor, not
  // silently identify a different profile clipped by the temporary trial cap.
  if(!selected.has(TuneModel) && (m.sustainingPercent>limit ||
      (!selected.has(TuneKick) && m.kickEnabled && m.kickPercent>limit)))return Result::Invalid;
  const float margin=std::max(2.f,speeds[i].coastDegrees);
  if (axes[i].current-travel < range(i).start+margin || axes[i].current+travel > range(i).end-margin) return Result::Invalid;
  if (!prepareMotion()) return Result::Unavailable;
  const uint32_t id=autoTune.id+1;
  const float setpoint=runtimeSpeed(i);
  stopNow(-1,now);
  setMode(Mode::Manual,now);
  autoTune={};auto &t=autoTune;t.options=selected;t.initialSetpoint=setpoint;
  t.proposalValidated=!selected.has(TuneKick|TuneModel);
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
  if(t.phase==AutoTunePhase::Start) {
    // The shared progress watchdog can expire before the startup observation
    // window. No motion at a search level means "try more output", not a
    // failed working model (which has not even been identified yet).
    if(t.searchLevel<std::floor(t.limit)) {
      t.searchLevel=std::min(std::floor(t.limit),t.searchLevel+t.options.searchStepPercent);
      autoTuneCoast(AutoTunePhase::Start,t.direction,t.searchLevel,false,now);
    } else finishAutoTune(AutoTunePhase::Failed,"no_start",now);
    return;
  }
  if(t.phase==AutoTunePhase::Minimum){finishAutoTuneMinimumTrial(false,now);return;}
  if(t.phase==AutoTunePhase::Validate && t.trackingVerified) {
    finishAutoTune(AutoTunePhase::Failed,"validation_error",now);return;
  }
  if(!t.options.has(TuneModel)) {finishAutoTune(AutoTunePhase::Failed,"no_sustained_motion",now);return;}
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
  t.modelReady=false;t.proposalValidated=false;
  // Stop and confirm coast before the next bounded startup. The global time,
  // stroke, output cap and saved stall deadline are never extended.
  autoTuneCoast(AutoTunePhase::Low,direction,t.minimumLevel,false,now);
}
bool Controller::autoTuneProbe(AutoTunePhase phase,int direction,float level,uint32_t now) {
  auto &t=autoTune;const unsigned i=unsigned(t.axis);
  t.preparationAttempts=0;t.preparationRequired=t.preparationAvailable=0;
  t.internal=true;stopNow(int(i),now);t.direction=direction;
  auto probe=t.originalMotion;probe.mode=RegulatorMode::Simple;probe.holdEnabled=false;probe.outputLimitPercent=t.limit;
  probe.kickEnabled=phase!=AutoTunePhase::Start;
  probe.kickPercent=std::min(t.limit,t.searchLevel);
  // Keep the saved pulse rise time. Confirmation has a separate bounded
  // deadline, so a short saved pulse cannot reject an already moving shaft.
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
  if(phase==AutoTunePhase::Failed) {
    const auto &a=axes[unsigned(t.axis)];const auto &m=motionSettings[unsigned(t.axis)];
    t.failure={t.phase,a.motionPhase,uint32_t(now-t.phaseAt),a.current,a.target,a.measuredVelocity,
        a.speedOutputPercent,t.threshold,m.stoppedVelocity,a.direction,a.tracking,m.velocityWindowMs};
  }
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
  if(remote || movementBlocked() || t.phase!=AutoTunePhase::Complete || !id || id!=t.id) return Result::Unavailable;
  const unsigned i=unsigned(t.axis);
  if(moving() || axes[0].tracking || axes[1].tracking || axes[0].holdArmed || axes[1].holdArmed || driveFault ||
      !fault.empty() || axes[0].motionError!=MotionError::None || axes[1].motionError!=MotionError::None) return Result::Busy;
  if(rangeRevision!=t.revision || !(speeds[i]==t.originalSpeed) || motionSettings[i]!=t.originalMotion || !axes[i].calibrated)
    return Result::Invalid;
  stopNow(-1,now);
  if(t.mode==RegulatorMode::Simple) speeds[i]=t.proposedSpeed;
  motionSettings[i]=t.proposedMotion;
  t.phase=AutoTunePhase::Idle;t.reason="applied";
  if(t.minimumSetpoint>0)setRuntimeSpeed(i,t.minimumSetpoint,now);
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
  // A shorter window helps fast drives, but its noise must be measured too.
  // Retain the saved window if 80 ms amplifies noise beyond the configured
  // stopped-speed allowance or the lowest measured sustainable velocity.
  const float shortThreshold=std::max(.2f,3*t.shortNoiseVelocity);
  if(t.mode==RegulatorMode::Cascade && shortThreshold<speed &&
      shortThreshold<=std::max(t.threshold,t.originalMotion.stoppedVelocity)) {
    m.velocityWindowMs=std::min(m.velocityWindowMs,80u);
    t.threshold=std::max(t.threshold,shortThreshold);
  }
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
    m.acceleration=std::min(m.acceleration,m.velocity/lambda);
    m.deceleration=std::min(m.deceleration,m.velocity/lambda);
    m.jerk=std::min(m.jerk,m.acceleration/lambda);
    // Keep the model-based PI. Breakaway is handled by its bounded kick;
    // inflating I to recover static friction creates a stop/start limit cycle.
  }
  if(!m.valid() || m.movementDegrees>10)return false;
  t.modelReady=true;
  return validateAutoTuneProposal(now);
}
MotionSettings Controller::autoTuneTrialMotion(MotionSettings m) const {
  m.holdEnabled=false;m.outputLimitPercent=autoTune.limit;
  m.kickPercent=std::min(m.kickPercent,autoTune.limit);
  m.sustainingPercent=std::min(m.sustainingPercent,unsigned(autoTune.limit));
  return m;
}
float Controller::autoTuneMinimumUpper() const {
  const auto &t=autoTune;
  return t.mode==RegulatorMode::Cascade?t.proposedMotion.velocity:
      std::min(std::max(float(t.proposedSpeed.speedPercent),float(t.proposedMotion.sustainingPercent)),std::floor(t.limit));
}
void Controller::prepareAutoTuneMinimum(uint32_t now) {
  auto &t=autoTune;const auto &a=axes[unsigned(t.axis)];
  if(t.minimumSetpoint>0 && t.minimumConfirm)t.minimumCandidate=t.minimumSetpoint;
  else if(t.options.has(TuneModel))t.minimumCandidate=t.mode==RegulatorMode::Cascade?
      std::max(t.measurements[0].lowVelocity,t.measurements[1].lowVelocity):
      std::ceil(std::max(t.measurements[0].lowOutput,t.measurements[1].lowOutput));
  else t.minimumCandidate=t.initialSetpoint;
  const float upper=autoTuneMinimumUpper();
  const float lower=t.mode==RegulatorMode::Cascade?upper/20:
      std::max(upper/20,std::min(float(t.proposedMotion.sustainingPercent),upper));
  t.minimumCandidate=std::clamp(t.minimumCandidate,lower,upper);
  t.direction=t.upper-a.current>=a.current-t.lower?1:-1;
  autoTuneCoast(AutoTunePhase::Minimum,t.direction,t.minimumCandidate,false,now);
}
void Controller::advanceAutoTune(uint32_t now) {
  auto &t=autoTune;
  if(!t.proposalValidated) {
    if(!validateAutoTuneProposal(now))finishAutoTune(AutoTunePhase::Failed,"validation_error",now);
  } else if(t.options.has(TuneMinimum) && !t.minimumCompletePending)prepareAutoTuneMinimum(now);
  else if(t.options.trackingCases() && !t.trackingVerified)beginAutoTuneTracking(now);
  else finishAutoTune(AutoTunePhase::Complete,"",now);
}
float Controller::autoTuneCoastReserve() const {
  const auto &t=autoTune;
  return std::max(2.f,std::max(t.originalSpeed.coastDegrees,
      1.2f*std::max({t.stoppingDistance,t.measurements[0].coastDegrees,t.measurements[1].coastDegrees}))+
      t.originalMotion.movementDegrees);
}
float Controller::autoTuneValidationReserve() const {
  const auto &t=autoTune;
  return std::max({2.f,t.originalSpeed.coastDegrees,1.2f*t.stoppingDistance})+
      t.originalMotion.movementDegrees+std::max({t.measurements[0].highVelocity,t.measurements[1].highVelocity,t.peakVelocity})*
      (.15f+float(t.originalMotion.velocityWindowMs)/2000)+(t.mode==RegulatorMode::Simple?2*t.proposedSpeed.coastDegrees:0);
}
bool Controller::validateAutoTuneProposal(uint32_t now) {
  auto &t=autoTune;const auto &m=t.proposedMotion;const auto &s=t.proposedSpeed;
  const auto &a=t.measurements[0],&b=t.measurements[1];
  const float speed=std::min(a.lowVelocity,b.lowVelocity);
  const unsigned i=unsigned(t.axis);
  // Reserve the observed kick/transfer peak too. A destination planned only
  // from plateau speed can immediately trip the unchanged dynamic stroke guard.
  // Cascade stops by feedback; only Simple additionally uses a manual coast cutoff.
  const float reserve=autoTuneValidationReserve();
  const float lower=t.lower+reserve,upper=t.upper-reserve;
  const float current=axes[i].current;
  const float minimumMove=t.mode==RegulatorMode::Cascade?2*m.positionTolerance+m.movementDegrees:0;
  t.validationError=0;
  t.validationPass=t.validationReversals=0;t.validationLastDirection=0;
  t.preparationAttempts=0;t.preparationRequired=t.preparationAvailable=0;
  // A shared central band is convenient for an out-and-back test, but it is
  // not necessary: a stopped departure needs no reserve for motion away from
  // that edge. Prepare the two directions independently if their destination
  // reserves overlap. Never reduce the destination or live stopping guards.
  const float required=std::max({5.f,3*m.movementDegrees,minimumMove});
  t.validationDirectional=upper-lower<required || std::max(upper-current,current-lower)<required;
  if(t.validationDirectional) {
    t.validationDirection=t.upper-axes[i].current>=axes[i].current-t.lower?1:-1;
    return beginAutoTuneValidation(now);
  }
  t.validationOrigin=std::clamp(current,lower,upper);
  const int direction=upper-current>=current-lower?1:-1;
  const float available=direction>0?upper-current:current-lower;
  if(available<std::max(5.f,3*m.movementDegrees)) {
    finishAutoTune(AutoTunePhase::Failed,"stopping_distance",now);return false;
  }
  // Returning to the origin can be just a few degrees after the last probe.
  // A strong startup pulse then dominates the whole move, so the measured
  // working-output coast is never exercised. Allow a few seconds of work-speed
  // travel, within the same guarded stroke; retain the original accuracy test.
  const float distance=std::min(available,std::max({5.f,4*speed,6*s.coastDegrees,4*m.movementDegrees,minimumMove}));
  t.validationTarget=std::clamp(current+direction*distance,lower,upper);
  if(t.mode==RegulatorMode::Cascade && std::abs(t.validationTarget-t.validationOrigin)<minimumMove) {
    t.validationTarget=std::clamp(t.validationOrigin+direction*minimumMove,lower,upper);
    if(std::abs(t.validationTarget-t.validationOrigin)+.001f<minimumMove) {
      finishAutoTune(AutoTunePhase::Failed,"stopping_distance",now);return false;
    }
  }
  // Validate the candidate through normal position control before offering Apply.
  t.internal=true;stopNow(int(i),now);
  speeds[i]=s;motionSettings[i]=autoTuneTrialMotion(t.proposedMotion);
  t.direction=t.validationTarget>=axes[i].current?1:-1;
  autoTunePhase(AutoTunePhase::Validate,now);
  Target target{t.validationTarget,t.validationTarget,i==0,i==1};
  const auto result=start(target,now,false);t.internal=false;
  return result==Result::Ok;
}
bool Controller::beginAutoTuneValidation(uint32_t now) {
  auto &t=autoTune;const unsigned i=unsigned(t.axis);const auto &m=t.proposedMotion;
  const float reserve=autoTuneValidationReserve();
  const float required=std::max({5.f,3*m.movementDegrees,
      t.mode==RegulatorMode::Cascade?2*m.positionTolerance+m.movementDegrees:0.f});
  const int direction=t.validationPass?-t.validationDirection:t.validationDirection;
  const float edge=direction>0?t.upper-reserve:t.lower+reserve;
  const float current=axes[i].current,available=direction*(edge-current);
  t.preparationRequired=required;t.preparationAvailable=std::max(0.f,available);
  if(t.upper-t.lower-autoTuneCoastReserve()-reserve<required) {
    finishAutoTune(AutoTunePhase::Failed,"stopping_distance",now);return false;
  }
  if(available<required) {
    return prepareAutoTuneMove(AutoTunePhase::Validate,direction,required,available,now);
  }
  const float speed=std::min(t.measurements[0].lowVelocity,t.measurements[1].lowVelocity);
  const float distance=std::min(available,std::max({required,4*speed,6*t.proposedSpeed.coastDegrees,4*m.movementDegrees}));
  t.validationOrigin=current;t.validationTarget=current+direction*distance;
  t.preparationAttempts=0;t.preparationRequired=t.preparationAvailable=0;
  t.direction=direction;t.validationReversals=0;t.validationLastDirection=0;
  t.internal=true;stopNow(int(i),now);
  speeds[i]=t.proposedSpeed;motionSettings[i]=autoTuneTrialMotion(m);
  autoTunePhase(AutoTunePhase::Validate,now);
  const auto result=start({t.validationTarget,t.validationTarget,i==0,i==1},now,false);t.internal=false;
  if(result!=Result::Ok)finishAutoTune(AutoTunePhase::Failed,"validation_error",now);
  return result==Result::Ok;
}
bool Controller::prepareAutoTuneMove(AutoTunePhase next,int direction,float required,float available,uint32_t now) {
  auto &t=autoTune;
  t.preparationRequired=required;t.preparationAvailable=std::max(0.f,available);
  // A live stopping guard can end a transfer before its planned goal. From
  // confirmed rest another bounded approach can reclaim that missing room.
  // The whole preparation has one deadline, including all short approaches.
  // Returning safely is ordinary work, not a limited number of failed trials.
  if(!t.preparationAttempts)t.preparationAt=now;
  else if(uint32_t(now-t.preparationAt)>60000u) {
    finishAutoTune(AutoTunePhase::Failed,"reposition_timeout",now);return false;
  }
  ++t.preparationAttempts;
  const float level=next==AutoTunePhase::Minimum?t.minimumCandidate:t.level;
  autoTuneCoast(next,direction,level,true,now);return true;
}
void Controller::beginAutoTuneMinimum(uint32_t now) {
  auto &t=autoTune;const unsigned i=unsigned(t.axis);
  // A minimum trial must fit the complete uninterrupted observation, not only
  // its first kick. Use the trial speed for arrival lag; the live guard still
  // includes the actual startup peak and stops a probe early when necessary.
  const float speed=t.mode==RegulatorMode::Cascade?t.minimumCandidate:
      std::max(t.threshold,std::max(t.measurements[0].lowVelocity,t.measurements[1].lowVelocity));
  const float reserve=autoTuneCoastReserve()+speed*(.15f+float(t.originalMotion.velocityWindowMs)/2000);
  const float usable=t.upper-t.lower-reserve-autoTuneCoastReserve();
  const float required=std::min(.9f*usable,std::max(2*t.proposedMotion.movementDegrees,2.6f*speed)+
      t.minimumStartupTravel[t.direction>0?0:1]);
  const float available=t.direction>0?t.upper-reserve-axes[i].current:axes[i].current-t.lower-reserve;
  // Leave transfer accuracy slack so a safe stop within position tolerance
  // cannot cause an endless request to reach the exact mathematical edge.
  // If a whole observation fits from rest, prepare it before restarting. If
  // its requested speed is too high, let the existing live guard/measurement
  // checks decide; never reject it just because of a historical peak.
  if(required>0 && available<required && usable>=2.6f*speed) {
    prepareAutoTuneMove(AutoTunePhase::Minimum,t.direction,required,available,now);return;
  }
  t.preparationAttempts=0;t.preparationRequired=t.preparationAvailable=0;
  t.internal=true;stopNow(int(i),now);
  motionSettings[i]=autoTuneTrialMotion(t.proposedMotion);speeds[i]=t.proposedSpeed;
  if(t.mode==RegulatorMode::Cascade)motionSettings[i].velocity=t.minimumCandidate;
  else speeds[i].speedPercent=speeds[i].approachSpeedPercent=unsigned(t.minimumCandidate);
  autoTunePhase(AutoTunePhase::Minimum,now);
  t.minimumAt=0;t.minimumSamples=0;t.minimumSpeedSum=t.minimumOutputSum=t.minimumPeak=0;
  t.minimumProgress=axes[i].current;t.minimumProgressAt=now;
  const auto result=jog(i,t.direction,false,now);t.internal=false;
  if(result!=Result::Ok)finishAutoTune(AutoTunePhase::Failed,"minimum_speed_unstable",now);
}
void Controller::finishAutoTuneMinimumTrial(bool passed,uint32_t now) {
  auto &t=autoTune;
  if(!passed) {
    if(t.minimumConfirm) {
      // A marginal point can pass once, then stick or burst on the repeated
      // start. Withdraw it and search upward under the same output, stroke
      // and absolute deadlines. The replacement needs both directions and
      // another independent confirmation before it can become a result.
      t.minimumSetpoint=t.minimumSpeed=t.minimumOutput=0;
      t.minimumConfirm=t.minimumCompletePending=false;
    }
    if(t.minimumSpeed<=0) {
      const float upper=autoTuneMinimumUpper();
      if(t.minimumCandidate>=upper) {
        finishAutoTune(AutoTunePhase::Failed,"minimum_speed_unstable",now);return;
      }
      t.minimumCandidate=std::min(upper,t.mode==RegulatorMode::Cascade?t.minimumCandidate*1.25f:t.minimumCandidate+1);
      t.minimumRaised=true;t.minimumPass=0;
      autoTuneCoast(AutoTunePhase::Minimum,t.direction,t.minimumCandidate,false,now);return;
    }
    // A failed lower point is evidence of a limit, not a new recommended speed.
    t.minimumCandidate=t.minimumSetpoint;
    t.minimumConfirm=true;t.minimumPass=0;
  } else if(t.minimumPass==0) {
    t.minimumDirectionSpeed=t.minimumSpeedSum/t.minimumSamples;
    t.minimumDirectionOutput=t.minimumOutputSum/t.minimumSamples;
    t.minimumPass=1;t.direction=-t.direction;
  } else {
    if(t.minimumConfirm) {
      t.minimumCompletePending=true;
      autoTuneCoast(AutoTunePhase::Minimum,t.direction,t.minimumCandidate,false,now);return;
    }
    t.minimumSetpoint=t.minimumCandidate;
    t.minimumSpeed=std::max(t.minimumDirectionSpeed,t.minimumSpeedSum/t.minimumSamples);
    t.minimumOutput=t.mode==RegulatorMode::Simple?t.minimumCandidate:
        std::max(t.minimumDirectionOutput,t.minimumOutputSum/t.minimumSamples);
    const float next=t.mode==RegulatorMode::Cascade?t.minimumCandidate*.7f:
        std::max(std::floor(t.minimumCandidate*.8f),std::min(float(t.proposedMotion.sustainingPercent),autoTuneMinimumUpper()));
    // Noise is judged from measured motion, not from the requested velocity:
    // friction can make those differ considerably at the lower end.
    const float lower=t.mode==RegulatorMode::Cascade?t.proposedMotion.velocity/20:
        std::max(t.limit/20,std::min(float(t.proposedMotion.sustainingPercent),autoTuneMinimumUpper()));
    if(++t.minimumTrial>=4 || t.minimumRaised || next<lower || next>=t.minimumCandidate) {
      t.minimumConfirm=true;
    } else t.minimumCandidate=next;
    t.minimumPass=0;
  }
  // Every direction starts from confirmed rest, through the normal kick and
  // protections. Do not ramp down a moving shaft and call that a startup test.
  autoTuneCoast(AutoTunePhase::Minimum,t.direction,t.minimumCandidate,false,now);
}
MotionSettings Controller::autoTuneTrackingCandidate(unsigned candidate) const {
  auto m=autoTune.trackingBase;
  // Preserve measured breakaway/feedforward, accuracy and the operator's
  // acceleration, jerk and output ceilings. Search local gains, not torque.
  if(candidate==1) {m.positionKp*=.6f;m.velocityKp*=.8f;m.velocityKi*=.6f;}
  if(candidate==2) {m.positionKp*=1.4f;m.velocityKp*=1.2f;m.velocityKi*=1.2f;}
  m.positionKp=std::clamp(m.positionKp,0.f,1000.f);
  m.velocityKp=std::clamp(m.velocityKp,0.f,10000.f);
  m.velocityKi=std::clamp(m.velocityKi,0.f,10000.f);
  return m;
}
bool Controller::planAutoTuneTracking(uint32_t now) {
  auto &t=autoTune;const auto &m=t.trackingBase;
  const float coast=autoTuneCoastReserve();
  // A historical one-sample startup peak is not a steady arrival velocity.
  // Reserve the measured coast and plateau speed here. The unchanged live
  // guard still stops at the actual velocity, including fresh differences.
  const float measured=std::max(t.measurements[0].highVelocity,t.measurements[1].highVelocity);
  t.trackingReserve=std::max(t.trackingReserve,coast+
      std::max(measured,m.velocity)*(.15f+float(t.originalMotion.velocityWindowMs)/2000));
  const float usable=.85f*(t.upper-t.lower-coast-t.trackingReserve);
  const float motion=std::max({4*m.movementDegrees,4*m.positionTolerance,
      2*std::max(t.measurements[0].coastDegrees,t.measurements[1].coastDegrees),8*(t.noiseHigh-t.noiseLow)});
  if(usable<motion) {finishAutoTune(AutoTunePhase::Failed,"stopping_distance",now);return false;}
  const float minimum=t.minimumSetpoint>0?t.minimumSetpoint:t.initialSetpoint;
  t.trackingFast=std::min(m.velocity,minimum);
  t.trackingSlow=std::min(t.minimumSetpoint>0?minimum/4:minimum/2,t.trackingFast/2);
  const uint32_t startup=std::max<uint32_t>({t.measurements[0].startMs,t.measurements[1].startMs,
      m.kickEnabled?std::min(2800u,m.kickMs)*2/3:0u});
  const unsigned delay=std::max(m.velocityWindowMs,unsigned(2000*std::max(t.measurements[0].responseSeconds,t.measurements[1].responseSeconds)));
  const float moving=t.minimumSpeed>0?t.minimumSpeed:std::max(t.measurements[0].lowVelocity,t.measurements[1].lowVelocity);
  const auto rounded=[](unsigned value,unsigned period){return ((value+period-1)/period)*period;};
  // A partial tune may start with a high saved cruise speed and no fresh
  // minimum-speed result. Bound that request before computing acceleration
  // warmup; rejecting an oversized initial request would skip a fitting test.
  constexpr unsigned maxSeriesMs=30000;
  const unsigned baseWarmup=rounded(1500u+unsigned(startup)+delay,1500u);
  const float timeVelocity=moving+float(maxSeriesMs-std::min(maxSeriesMs,baseWarmup+3000u))*
      std::max(.001f,m.acceleration)/1000;
  t.trackingFast=std::min({t.trackingFast,.95f*usable*1000/(baseWarmup+3000u),timeVelocity});
  t.trackingSlow=std::min(t.trackingSlow,t.trackingFast/2);
  bool fitted=false;
  for(unsigned attempt=0;attempt<8 && !fitted;++attempt) {
    fitted=true;
    for(unsigned band=0;band<2;++band) {
      if(!t.options.has(band?TuneFast:TuneSlow))continue;
      float &velocity=band?t.trackingFast:t.trackingSlow;
      if(velocity<=0) {finishAutoTune(AutoTunePhase::Failed,"stopping_distance",now);return false;}
      float distance=0;
      for(unsigned cadence=0;cadence<2;++cadence) {
        const unsigned index=2*band+cadence,period=cadence?1500:200;
        const unsigned acceleration=unsigned(1000*std::max(0.f,velocity-moving)/std::max(.001f,m.acceleration));
        const unsigned warmup=rounded(period+startup+delay+acceleration,period);
        const unsigned observe=rounded(std::max({2000u,2*period,unsigned(std::ceil(1000*motion/velocity))}),period);
        if(warmup+observe>maxSeriesMs) {finishAutoTune(AutoTunePhase::Failed,"stopping_distance",now);return false;}
        t.trackingWarmupMs[index]=warmup;t.trackingDurationMs[index]=warmup+observe;
        distance=std::max(distance,velocity*float(warmup+observe)/1000);
      }
      if(distance>usable) {velocity*=.95f*usable/distance;fitted=false;}
    }
    if(t.options.has(TuneSlow) && t.options.has(TuneFast) && t.trackingSlow>t.trackingFast/2) {
      t.trackingSlow=t.trackingFast/2;fitted=false;
    }
  }
  if(!fitted) {finishAutoTune(AutoTunePhase::Failed,"stopping_distance",now);return false;}
  // Variable-length series must leave time for all three candidates and the
  // final independent validation. Keep one absolute, bounded run deadline:
  // replanning may increase the plan allowance, never restart the clock.
  uint32_t seriesMs=0;
  for(const auto duration:t.trackingDurationMs)seriesMs+=6*duration;
  t.trackingTimeoutMs=std::max(t.trackingTimeoutMs,
      std::min<uint32_t>(1200000,120000+seriesMs+30000*t.options.trackingCases()+60000));
  t.trackingResults={};t.trackingCandidate=0;t.trackingCase=t.options.has(TuneSlow)?0:4;
  t.trackingScored=false;t.trackingPlanned=true;t.trackingPeriodMs=200;
  return true;
}
void Controller::beginAutoTuneTracking(uint32_t now) {
  auto &t=autoTune;const unsigned i=unsigned(t.axis);auto &a=axes[i];
  if(!t.trackingPlanned) {
    t.trackingBase=t.proposedMotion;
    if(!planAutoTuneTracking(now))return;
    t.trackingDirection=t.upper-a.current>=a.current-t.lower?1:-1;
  }
  if(t.trackingScored) {
    t.trackingScored=false;
    if(t.trackingResults[t.trackingCandidate].rejected || ++t.trackingCase==(t.options.has(TuneFast)?8u:4u)) {
      ++t.trackingCandidate;t.trackingCase=t.options.has(TuneSlow)?0:4;
    }
  }
  if(t.trackingCandidate==t.trackingResults.size()) {
    bool found=false;float best=0;
    for(unsigned n=0;n<t.trackingResults.size();++n) {
      const auto &r=t.trackingResults[n];
      if(r.rejected || r.completed!=t.options.trackingCases())continue;
      if(!found || r.score<best) {found=true;best=r.score;t.trackingBest=n;}
    }
    if(!found) {finishAutoTune(AutoTunePhase::Failed,"tracking_unstable",now);return;}
    // Avoid changing the baseline for an improvement below five percent.
    const auto &baseline=t.trackingResults[0];
    if(!baseline.rejected && baseline.completed==t.options.trackingCases() && best>=baseline.score*.95f)t.trackingBest=0;
    t.proposedMotion=autoTuneTrackingCandidate(t.trackingBest);t.trackingVerified=true;
    // Gain changes must still pass ordinary approaches and independent
    // minimum-speed starts before Apply becomes available.
    t.minimumCompletePending=false;t.minimumConfirm=true;t.minimumPass=0;
    t.minimumCandidate=t.minimumSetpoint;
    t.proposalValidated=false;advanceAutoTune(now);
    return;
  }
  motionSettings[i]=autoTuneTrialMotion(autoTuneTrackingCandidate(t.trackingCandidate));speeds[i]=t.proposedSpeed;
  const auto &m=motionSettings[i];
  t.trackingPeriodMs=(t.trackingCase/2)%2?1500:200;
  t.trackingVelocity=t.trackingCase<4?t.trackingSlow:t.trackingFast;
  t.direction=t.trackingDirection*(t.trackingCase%2?-1:1);
  const float measured=std::max(t.measurements[0].highVelocity,t.measurements[1].highVelocity);
  t.trackingReserve=std::max(t.trackingReserve,autoTuneCoastReserve()+
      std::max(measured,t.trackingBase.velocity)*(.15f+float(t.originalMotion.velocityWindowMs)/2000));
  const float reserve=t.trackingReserve;
  const float seconds=float(t.trackingDurationMs[t.trackingCase/2])/1000;
  const float destination=a.current+t.direction*t.trackingVelocity*seconds;
  if((t.direction>0 && destination>t.upper-reserve) || (t.direction<0 && destination<t.lower+reserve)) {
    const float required=t.trackingVelocity*seconds;
    const float available=t.direction>0?t.upper-reserve-a.current:a.current-t.lower-reserve;
    t.preparationRequired=required;t.preparationAvailable=std::max(0.f,available);
    const float usable=t.upper-t.lower-reserve-autoTuneCoastReserve();
    if(usable<required) {
      if(planAutoTuneTracking(now))beginAutoTuneTracking(now);
      return;
    }
    // Reuse the existing guarded transfer, then start this same case at rest.
    prepareAutoTuneMove(AutoTunePhase::Tracking,t.direction,required,available,now);return;
  }
  t.preparationAttempts=0;t.preparationRequired=t.preparationAvailable=0;
  t.internal=true;stopNow(int(i),now);autoTunePhase(AutoTunePhase::Tracking,now);
  t.trackingAt=now;t.sampledAt=motion_[i].feedback.at;t.trackingOrigin=t.trackingGoal=a.current;
  t.trackingStartMean=motion_[i].feedback.positionMean(m.velocityWindowMs);
  t.trackingCommand=t.trackingSamples=t.trackingStoppedMs=t.trackingKicks=t.trackingReversals=0;
  t.trackingSeconds=t.trackingErrorSum=t.trackingPeak=t.trackingOvershoot=t.trackingVariation=t.trackingOutput=0;
  t.trackingLastPhase=MotionPhase::Idle;t.trackingLastDirection=0;
  const auto result=start({a.current,a.current,i==0,i==1},now,false);t.internal=false;
  if(result!=Result::Ok)finishAutoTune(AutoTunePhase::Failed,"start_rejected",now);
}
void Controller::finishAutoTuneTrackingTrial(bool passed,uint32_t now,const char *reason) {
  auto &t=autoTune;auto &r=t.trackingResults[t.trackingCandidate];
  r.peakError=std::max(r.peakError,t.trackingPeak);
  r.overshoot=std::max(r.overshoot,t.trackingOvershoot);
  if(!passed || t.trackingSamples<8) {r.rejected=true;r.reason=*reason?reason:"insufficient_samples";r.failedCase=int(t.trackingCase);}
  else {
    const auto &m=motionSettings[unsigned(t.axis)];
    const float scale=std::max(m.positionTolerance,t.trackingVelocity*t.trackingPeriodMs/1000);
    const float mean=t.trackingErrorSum/t.trackingSeconds;
    // Equal weight for each case; long sparse-command gaps must not drown
    // out slow tracking. Penalize bursts and overshoot as well as mean lag.
    r.score+=mean/scale+.5f*t.trackingPeak/scale+2*t.trackingOvershoot/m.positionTolerance+
        float(t.trackingStoppedMs)/(t.trackingDurationMs[t.trackingCase/2]-t.trackingWarmupMs[t.trackingCase/2])+float(t.trackingKicks)+2*t.trackingReversals+
        t.trackingVariation/(t.limit*30);
    r.meanError+=mean/t.options.trackingCases();r.stoppedMs+=t.trackingStoppedMs;
    r.kicks+=t.trackingKicks;r.reversals+=t.trackingReversals;++r.completed;
  }
  t.trackingScored=true;
  autoTuneCoast(AutoTunePhase::Tracking,t.direction,0,false,now);
}
void Controller::tickAutoTuneTracking(uint32_t now,float dt) {
  auto &t=autoTune;const unsigned i=unsigned(t.axis);const auto &a=axes[i];const auto &m=motionSettings[i];
  const unsigned duration=t.trackingDurationMs[t.trackingCase/2],warmup=t.trackingWarmupMs[t.trackingCase/2];
  const uint32_t elapsed=uint32_t(now-t.trackingAt);
  // At most one command per tick; timing gaps are never replayed as a burst.
  const unsigned command=std::min<uint32_t>(duration,elapsed)/t.trackingPeriodMs;
  if(command>t.trackingCommand) {
    t.trackingCommand=command;
    t.trackingGoal=t.trackingOrigin+t.direction*t.trackingVelocity*float(command*t.trackingPeriodMs)/1000;
    t.internal=true;const auto result=start({t.trackingGoal,t.trackingGoal,i==0,i==1},now,false);t.internal=false;
    if(result!=Result::Ok) {finishAutoTune(AutoTunePhase::Failed,"start_rejected",now);return;}
  }
  const auto &feedback=motion_[i].feedback;
  const unsigned sampleElapsed=int32_t(feedback.at-t.trackingAt)<0?0:std::min<uint32_t>(duration,uint32_t(feedback.at-t.trackingAt));
  const float reference=t.trackingOrigin+t.direction*t.trackingVelocity*sampleElapsed/1000;
  const float error=std::abs(a.current-reference);
  t.trackingPeak=std::max(t.trackingPeak,error);
  // During a sparse stream the mechanism may legitimately pass the last
  // received waypoint before the next arrives. Judge lead against the
  // continuous trajectory at the measurement timestamp, not a stale packet.
  t.trackingOvershoot=std::max(t.trackingOvershoot,t.direction*(a.current-reference));
  if(a.motionPhase==MotionPhase::Kick && t.trackingLastPhase!=MotionPhase::Kick)++t.trackingKicks;
  if(a.direction && a.direction!=t.trackingLastDirection) {
    if(t.trackingLastDirection)++t.trackingReversals;
    t.trackingLastDirection=a.direction;
  }
  t.trackingLastPhase=a.motionPhase;
  if(feedback.at!=t.sampledAt) {
    const unsigned previous=int32_t(t.sampledAt-t.trackingAt)<0?0:std::min<uint32_t>(duration,uint32_t(t.sampledAt-t.trackingAt));
    if(sampleElapsed>warmup) {
      const float seconds=float(sampleElapsed-std::max(previous,warmup))/1000;
      ++t.trackingSamples;t.trackingErrorSum+=error*seconds;t.trackingSeconds+=seconds;
      if(std::abs(a.measuredVelocity)<=m.stoppedVelocity)t.trackingStoppedMs+=unsigned(seconds*1000+.5f);
      t.trackingVariation+=std::abs(a.speedOutputPercent-t.trackingOutput);
    }
    t.sampledAt=feedback.at;
  }
  t.trackingOutput=a.speedOutputPercent;
  const float overshootLimit=std::max(2*m.positionTolerance,3*m.movementDegrees);
  if(t.trackingOvershoot>overshootLimit || t.trackingReversals>1) {
    finishAutoTuneTrackingTrial(false,now,t.trackingReversals>1?"oscillation":"overshoot");return;
  }
  if(elapsed<duration)return;
  const float progress=t.direction*(feedback.positionMean(m.velocityWindowMs)-t.trackingStartMean);
  const bool progressed=progress>=std::max({2*m.movementDegrees,4*(t.noiseHigh-t.noiseLow),
      .4f*t.trackingVelocity*duration/1000});
  // This trial measures an active command stream. STOP/coast ends it; ordinary
  // point-to-point accuracy is validated separately after choosing the gains.
  // Waiting for arrival here counted the end of a stream as more streaming,
  // and rejected otherwise useful candidates on an unrelated final kick.
  finishAutoTuneTrackingTrial(progressed,now,progressed?"":"no_progress");
  (void)dt;
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
  if(uint32_t(now-t.startedAt)>(t.trackingPeriodMs?t.trackingTimeoutMs:120000u)) {finishAutoTune(AutoTunePhase::Failed,"timeout",now);return;}
  if(t.preparationAttempts && (t.phase==AutoTunePhase::Coast || t.phase==AutoTunePhase::Reposition) &&
      uint32_t(now-t.preparationAt)>60000u) {
    finishAutoTune(AutoTunePhase::Failed,"reposition_timeout",now);return;
  }
  // Allow stationary sensor drift without borrowing the tuned axis's movement
  // threshold. A powered second axis still aborts immediately at any offset.
  const float otherTolerance=std::max(5.f,motionSettings[1-i].movementDegrees);
  if(std::abs(axes[1-i].current-t.otherPosition)>otherTolerance || axes[1-i].direction) {
    finishAutoTune(AutoTunePhase::Failed,"other_axis_moved",now);return;
  }
  const float velocity=t.direction*a.measuredVelocity;
  if(f.at!=t.positionAt) {
    const float delta=a.current-t.lastPosition;
    // The quiet-stage position noise must not become an apparent startup
    // speed when divided by one short sample interval. Otherwise the live
    // guard cuts every return pulse before breakaway near the departure edge.
    // Keep the measured-velocity guard and every displacement beyond noise.
    const float noise=2*(t.noiseHigh-t.noiseLow);
    t.sampleVelocity=std::copysign(std::max(0.f,std::abs(delta)-noise),delta)*1000/uint32_t(f.at-t.positionAt);
    t.lastPosition=a.current;t.positionAt=f.at;
  }
  const float coast=std::max({t.stoppingDistance,t.measurements[0].coastDegrees,t.measurements[1].coastDegrees});
  // The filtered velocity lags during kick. Include fresh position differences
  // and estimator latency in the stopping reserve, never just steady speed.
  const float speed=std::max(std::abs(a.measuredVelocity),std::abs(t.sampleVelocity));
  if(t.phase!=AutoTunePhase::Validate && t.phase!=AutoTunePhase::Tracking &&
     (t.phase!=AutoTunePhase::Minimum || !t.options.has(TuneModel)))t.peakVelocity=std::max(t.peakVelocity,speed);
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
  if(t.phase==AutoTunePhase::Minimum && !t.minimumAt) {
    auto &startup=t.minimumStartupTravel[t.direction>0?0:1];
    startup=std::max(startup,t.direction*(a.current-t.phasePosition));
  }
  if(nearEdge && t.phase==AutoTunePhase::Tracking) {
    if(guard>t.trackingReserve+.1f) {
      autoTuneCoast(AutoTunePhase::Tracking,t.direction,0,false,now);
      t.trackingReserve=guard+std::max(2*t.originalMotion.positionTolerance,2*t.originalMotion.movementDegrees);
      planAutoTuneTracking(now);
    } else finishAutoTuneTrackingTrial(false,now,"travel_limit");
    return;
  }
  if(nearEdge) {
    if(t.phase==AutoTunePhase::Start || t.phase==AutoTunePhase::Low || t.phase==AutoTunePhase::High || t.phase==AutoTunePhase::Minimum) {
      ++t.relocations;
      if(t.phase==AutoTunePhase::High && t.relocations>1 && keepMeasuredHigh())return;
      prepareAutoTuneMove(t.phase,t.direction,0,0,now);return;
    }
    if(t.phase==AutoTunePhase::Reposition) {
      t.internal=true;stopNow(int(i),now);t.internal=false;return;
    }
    if(t.phase!=AutoTunePhase::Coast) {finishAutoTune(AutoTunePhase::Failed,"travel_limit",now);return;}
  }
  if(t.phase==AutoTunePhase::Tracking) {tickAutoTuneTracking(now,dt);return;}
  if(t.phase==AutoTunePhase::Quiet) {
    if(f.at!=t.sampledAt){
      t.sampledAt=f.at;t.noiseLow=std::min(t.noiseLow,a.current);t.noiseHigh=std::max(t.noiseHigh,a.current);
      t.noiseVelocity=std::max(t.noiseVelocity,std::abs(a.measuredVelocity));
      t.shortNoiseVelocity=std::max(t.shortNoiseVelocity,std::abs(f.velocityInWindow(std::min(80u,t.originalMotion.velocityWindowMs))));
    }
    if(uint32_t(now-t.phaseAt)<600)return;
    t.threshold=std::max(.2f,3*t.noiseVelocity);
    if(t.noiseHigh-t.noiseLow>t.originalMotion.movementDegrees/2 || t.threshold>10) {finishAutoTune(AutoTunePhase::Failed,"feedback_noise",now);return;}
    // A saved kick is not an identified minimum, especially after changing
    // PWM frequency or the mechanics. Search from below it on every new tune.
    t.searchLevel=std::min(t.options.searchStartPercent,t.limit);
    if(t.options.has(TuneKick))autoTuneProbe(AutoTunePhase::Start,1,t.searchLevel,now);
    else {
      t.searchLevel=std::min(t.proposedMotion.kickPercent,t.limit);
      advanceAutoTune(now);
    }
    return;
  }
  if(t.phase==AutoTunePhase::Minimum) {
    if(a.motionPhase==MotionPhase::Run && motion_[i].kickDone && a.direction) {
      auto &started=t.measurements[a.direction>0?0:1];
      started.startOutput=std::max(started.startOutput,motion_[i].kickPeak);
      started.startMs=std::max(started.startMs,uint32_t(motion_[i].releaseAt-motion_[i].kickAt));
    }
    if(uint32_t(now-t.phaseAt)>std::min(15000u,t.originalMotion.stallMs+5000)) {
      finishAutoTuneMinimumTrial(false,now);return;
    }
    if(a.motionPhase!=MotionPhase::Run || motion_[i].releasing ||
       (t.mode==RegulatorMode::Cascade && std::abs(a.requestedVelocity)<.9f*t.minimumCandidate) ||
       (t.mode==RegulatorMode::Simple && a.speedOutputPercent<.9f*t.minimumCandidate))return;
    if(!t.minimumAt) {
      t.minimumAt=now;t.minimumProgressAt=now;t.minimumProgress=a.current;
    }
    // Exclude handoff transients, then require uninterrupted progress in
    // several windows. ADC jitter alone cannot pass a stationary mechanism.
    if(uint32_t(now-t.minimumAt)<500)return;
    if(f.at==t.sampledAt)return;
    t.sampledAt=f.at;
    t.minimumSpeedSum+=std::max(0.f,velocity);t.minimumOutputSum+=a.speedOutputPercent;
    t.minimumPeak=std::max(t.minimumPeak,velocity);++t.minimumSamples;
    if(uint32_t(now-t.minimumProgressAt)>=600) {
      const float required=std::max(2*(t.noiseHigh-t.noiseLow),.15f*(t.mode==RegulatorMode::Cascade?t.minimumCandidate:t.threshold));
      if(t.direction*(a.current-t.minimumProgress)<std::max(.01f,required)) {
        finishAutoTuneMinimumTrial(false,now);return;
      }
      t.minimumProgressAt=now;t.minimumProgress=a.current;
    }
    if(uint32_t(now-t.minimumAt)<2100 || !t.minimumSamples)return;
    const float mean=t.minimumSpeedSum/t.minimumSamples;
    const bool stable=mean>t.threshold && t.minimumPeak<=std::max(mean*1.8f,mean+2*t.threshold) &&
        (t.mode!=RegulatorMode::Cascade || (mean>=.5f*t.minimumCandidate && mean<=1.6f*t.minimumCandidate));
    finishAutoTuneMinimumTrial(stable,now);return;
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
      if(!t.options.has(TuneModel)) {
        if(t.direction>0)autoTuneCoast(AutoTunePhase::Start,-1,t.options.searchStartPercent,false,now);
        else {
          auto &m=t.proposedMotion;const auto &a=t.measurements[0],&b=t.measurements[1];
          m.kickEnabled=true;m.kickPercent=std::min(t.limit,5*std::ceil(std::max(a.startOutput,b.startOutput)/5));
          m.kickMs=std::clamp(unsigned(1.5f*std::max(a.startMs,b.startMs)+200),200u,10000u);
          t.searchLevel=m.kickPercent;
          autoTuneCoast(AutoTunePhase::Validate,t.direction,0,false,now);
        }
        return;
      }
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
    // Identify breakaway at a constant PWM, rather than attributing delayed
    // motion to the top of a fast ramp. Stop/settle between unsuccessful
    // levels; each pulse and the complete procedure retain their deadlines.
    if(autoTuneStartupExpired(t,elapsed,(a.current-t.phasePosition)*t.direction,velocity)) {
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
    const unsigned deadline=t.phase==AutoTunePhase::Coast?
        5000u+std::max(300u,t.originalMotion.settleMs)+t.originalMotion.velocityWindowMs:60000u;
    if(uint32_t(now-t.phaseAt)>deadline) {
      const char *reason=t.phase==AutoTunePhase::Coast?"coast_timeout":
          t.phase==AutoTunePhase::Reposition?"reposition_timeout":"validation_timeout";
      finishAutoTune(AutoTunePhase::Failed,reason,now);return;
    }
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
        if(t.validationDirectional) {beginAutoTuneValidation(now);return;}
        t.validationTarget=t.validationOrigin;
        t.direction=t.validationTarget>=a.current?1:-1;
        autoTunePhase(AutoTunePhase::Validate,now);
        t.internal=true;
        const auto result=start({t.validationTarget,t.validationTarget,i==0,i==1},now,false);
        t.internal=false;
        if(result!=Result::Ok)finishAutoTune(AutoTunePhase::Failed,"validation_error",now);
        return;
      }
      t.proposalValidated=true;advanceAutoTune(now);return;
    }
    if(t.phase==AutoTunePhase::Reposition) {
      if(t.nextPhase==AutoTunePhase::Tracking)beginAutoTuneTracking(now);
      else if(t.nextPhase==AutoTunePhase::Validate)beginAutoTuneValidation(now);
      else if(t.nextPhase==AutoTunePhase::Minimum) {
        t.direction=t.nextDirection;beginAutoTuneMinimum(now);
      } else autoTuneProbe(t.nextPhase,t.nextDirection,t.nextLevel,now);
      return;
    }
    const float stoppedDistance=std::max(0.f,t.direction*(a.current-t.phasePosition));
    t.stoppingDistance=std::max(t.stoppingDistance,stoppedDistance);
    // A cutoff during a startup search is a safety reserve, not the coast of
    // the working output that will be proposed to the user.
    if(!t.relocate && (t.nextPhase==AutoTunePhase::Start || t.nextPhase==AutoTunePhase::Validate))
      measurement.coastDegrees=stoppedDistance;
    if(t.relocate) {
      const float coastReserve=autoTuneCoastReserve();
      if(2*coastReserve>=t.upper-t.lower) {
        if(t.nextPhase==AutoTunePhase::High && keepMeasuredHigh())return;
        finishAutoTune(AutoTunePhase::Failed,"stopping_distance",now);return;
      }
      float reserve=std::max({2.f,1.5f*t.originalSpeed.coastDegrees,1.2f*std::max({t.stoppingDistance,t.measurements[0].coastDegrees,t.measurements[1].coastDegrees})})+
          t.originalMotion.movementDegrees+t.peakVelocity*(.15f+float(t.originalMotion.velocityWindowMs)/2000)+
          2*t.originalSpeed.coastDegrees;
      // If that return left too little room, reclaim the full permitted
      // stroke. A historical startup peak can greatly exceed transfer speed;
      // it must not reject the return before it has even started. The live
      // guard above still stops at actual speed + feedback lag + measured coast.
      if(t.nextPhase==AutoTunePhase::Validate || t.nextPhase==AutoTunePhase::Tracking || t.nextPhase==AutoTunePhase::Minimum ||
          t.relocations>1 || 2*reserve>=(t.upper-t.lower)*.75f)reserve=coastReserve;
      t.internal=true;stopNow(int(i),now);
      auto m=t.originalMotion;m.mode=RegulatorMode::Simple;m.holdEnabled=false;m.outputLimitPercent=t.limit;
      m.kickEnabled=true;m.kickPercent=std::min(t.limit,t.searchLevel);
      m.kickMs=std::min({2800u,t.originalMotion.stallMs,t.originalMotion.kickEnabled?t.originalMotion.kickMs:2800u});
      speeds[i]=t.originalSpeed;
      speeds[i].speedPercent=std::min(unsigned(t.limit),speeds[i].speedPercent);
      if(t.preparationAttempts>1) {
        // A second approach starts from rest closer to the departure edge.
        // Release its kick on noise-confirmed small motion and use the measured
        // sustaining output, instead of repeating the same large transfer.
        m.movementDegrees=std::min(m.movementDegrees,std::max(.5f,6*(t.noiseHigh-t.noiseLow)));
        const unsigned floor=unsigned(std::ceil(std::max(t.measurements[0].lowOutput,t.measurements[1].lowOutput)));
        if(floor)speeds[i].speedPercent=std::min(speeds[i].speedPercent,floor);
      }
      motionSettings[i]=m;
      speeds[i].approachSpeedPercent=std::min(speeds[i].approachSpeedPercent,speeds[i].speedPercent);
      float goal=t.nextDirection>0?t.lower+reserve:t.upper-reserve;
      if(t.nextPhase==AutoTunePhase::Tracking) {
        // Return far enough for this series, rather than traversing the whole
        // stroke before every short case. Include approach/cutoff uncertainty
        // so stopping just short of this transfer goal still leaves room.
        const float slack=std::max({coastReserve,2*t.originalMotion.positionTolerance,3*t.originalMotion.movementDegrees});
        goal=std::clamp(t.nextDirection>0?t.upper-t.trackingReserve-float(t.trackingDurationMs[t.trackingCase/2])*t.trackingVelocity/1000-slack:
            t.lower+t.trackingReserve+float(t.trackingDurationMs[t.trackingCase/2])*t.trackingVelocity/1000+slack,t.lower+coastReserve,t.upper-coastReserve);
      }
      autoTunePhase(AutoTunePhase::Reposition,now);
      const auto result=start({goal,goal,i==0,i==1},now,false);t.internal=false;
      if(result!=Result::Ok)finishAutoTune(AutoTunePhase::Failed,"start_rejected",now);
    } else if(t.nextPhase==AutoTunePhase::Tracking) {
      beginAutoTuneTracking(now);
    } else if(t.nextPhase==AutoTunePhase::Minimum) {
      if(t.minimumCompletePending) {
        advanceAutoTune(now);
      }
      else {t.direction=t.nextDirection;beginAutoTuneMinimum(now);}
    } else if(t.nextPhase==AutoTunePhase::Validate) {
      if(t.options.has(TuneModel) && !t.modelReady) {
        if(!proposeAutoTune(now))finishAutoTune(AutoTunePhase::Failed,"model_invalid",now);
      } else advanceAutoTune(now);
    } else autoTuneProbe(t.nextPhase,t.nextDirection,t.nextLevel,now);
  }
}
} // namespace forkrot
