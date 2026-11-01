#include "build_config.hpp"
#if ESP32_SPP_PROFILE
// Minimal direct PWM profile; no PID, autotune or trajectory solver. GPL-3.0.
#include "control.hpp"
#include <cmath>
#include <cstring>
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
int sign(float n) { return (n>0)-(n<0); }
uint32_t checksum(const uint8_t *b, size_t n) {
  uint32_t c = 2166136261u;
  for (size_t i = 0; i < n; ++i) c = (c ^ b[i]) * 16777619u;
  return c;
}
}
bool prepareMotionPlanner() { return true; }
bool Controller::prepareMotion() { return true; }
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
void MotionRuntime::cancel(bool) {
  static_cast<MotionState &>(*this) = {};
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
  if(remote || i>1 || !settings.valid())return false;
  auto direct=settings;
  direct.mode=RegulatorMode::Simple;
  direct.kickEnabled=direct.holdEnabled=false;
  if(motionSettings[i]==direct)return true;
  stopNow(int(i),now);motionSettings[i]=direct;
  resetRuntimeSpeed(i,now);return true;
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
void Controller::tickMotion(unsigned i,uint32_t now) {
  auto &a=axes[i];auto &r=motion_[i];const auto &m=motionSettings[i];
  a.holdArmed=false;a.requestedVelocity=0;a.profileAcceleration=0;
  if(!m.ready || driveFault || a.motionError!=MotionError::None) {
    a.wanted=0;a.tracking=false;a.speedOutputPercent=0;output(i,0,now);cancelMotion(i);return;
  }
  if(!a.wanted) {
    a.speedOutputPercent=0;output(i,0,now);cancelMotion(i);return;
  }
  if(r.feedback.timed && uint32_t(now-r.feedback.at)>m.feedbackTimeoutMs) {
    motionFault(i,MotionError::FeedbackStale,now);return;
  }
  const unsigned period=controlPeriodMs();
  if(r.started && int32_t(now-r.nextAt)<0)return;
  r.timing.record(r.started?uint32_t(now-r.at):0,period);
  r.nextAt=now+period;r.at=now;r.started=true;
  const int previous=a.direction;
  output(i,a.wanted,now); // Includes the existing 300ms reverse interlock.
  if(!a.direction) {a.speedOutputPercent=0;r.progressDirection=0;a.motionPhase=MotionPhase::Idle;return;}
  const float requested=(manualAbsolute_[i]&&!a.tracking?runtimeSpeedLimit(i):runtimeSpeed(i))*manualScale_[i];
  a.speedOutputPercent=std::clamp(requested,0.f,m.outputLimitPercent);
  a.motionPhase=MotionPhase::Run;
  // No acceleration/deceleration/approach ramp or kick. Keep motion supervision.
  if(a.direction!=previous || r.progressDirection!=a.direction || !r.progressValid) {
    r.progressDirection=a.direction;r.progressPosition=a.current;r.progressAt=now;r.progressValid=true;
  }
  if(a.calibrated && (a.current-r.progressPosition)*a.direction>=std::min(.2f,m.movementDegrees)) {
    r.progressPosition=a.current;r.progressAt=now;
  }
  if(a.calibrated && uint32_t(now-r.progressAt)>=m.stallMs)motionFault(i,MotionError::Stalled,now);
}
const char *Controller::autoTunePhaseName(AutoTunePhase) { return "disabled"; }
Result Controller::centerForAutoTune(unsigned,uint32_t) { return Result::Unavailable; }
Result Controller::startAutoTune(unsigned,RegulatorMode,float,float,uint32_t,const AutoTuneOptions &) { return Result::Unavailable; }
Result Controller::cancelAutoTune(uint32_t) { return Result::Unavailable; }
Result Controller::applyAutoTune(uint32_t,uint32_t) { return Result::Unavailable; }
void Controller::finishAutoTune(AutoTunePhase,const char *,uint32_t) {}
void Controller::tickAutoTune(uint32_t) {}
} // namespace forkrot

#endif
