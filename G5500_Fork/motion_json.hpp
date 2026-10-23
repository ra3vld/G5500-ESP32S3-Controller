// GPL-3.0. Shared bounded projections; controller coefficients stay out of live frames.
#pragma once
#include "control.hpp"
#include "json_writer.hpp"
namespace forkrot {
inline const SpeedSettings &reportedSpeed(const Controller &c,unsigned i) {
  return c.autoTune.active() && c.autoTune.axis==int(i) ? c.autoTune.originalSpeed : c.speeds[i];
}
inline const MotionSettings &reportedMotion(const Controller &c,unsigned i) {
  return c.autoTune.active() && c.autoTune.axis==int(i) ? c.autoTune.originalMotion : c.motionSettings[i];
}
inline bool writeAutoTune(JsonWriter &j, const Controller &c, bool full=false) {
  const auto &t=c.autoTune;
  j << "{\"phase\":" << jsonQuoted(Controller::autoTunePhaseName(t.phase))
    << ",\"active\":" << (t.active()?"true":"false") << ",\"id\":" << t.id
    << ",\"axis\":" << t.axis << ",\"mode\":" << jsonQuoted(regulatorName(t.mode))
    << ",\"direction\":" << t.direction << ",\"output\":" << (t.active()?c.axes[unsigned(t.axis)].speedOutputPercent:0)
    << ",\"reason\":" << jsonQuoted(t.reason) << ",\"validationError\":" << t.validationError;
  if(t.active())j << ",\"stages\":" << t.options.stages;
  const unsigned cases=t.options.trackingCases(),first=t.options.has(TuneSlow)?0:4;
  if(t.active() && t.trackingPeriodMs && !t.trackingVerified)
    j << ",\"trackingTrial\":" << std::min(3*cases,t.trackingCandidate*cases+t.trackingCase-first+1)
      << ",\"trackingTrials\":" << 3*cases
      << ",\"trackingPeriodMs\":" << ((t.trackingCase/2)%2?1500:200)
      << ",\"trackingDurationMs\":" << t.trackingDurationMs[t.trackingCase/2]
      << ",\"trackingTravel\":" << (t.trackingCase<4?t.trackingSlow:t.trackingFast)*t.trackingDurationMs[t.trackingCase/2]/1000;
  if(full) {
    if(t.phase==AutoTunePhase::Failed && t.failure.phase!=AutoTunePhase::Idle) {
      const auto &f=t.failure;
      j << ",\"failure\":{\"phase\":" << jsonQuoted(Controller::autoTunePhaseName(f.phase))
        << ",\"motionPhase\":" << jsonQuoted(motionPhaseName(f.motionPhase)) << ",\"elapsedMs\":" << f.elapsedMs
        << ",\"position\":" << f.position << ",\"target\":" << f.target << ",\"velocity\":" << f.velocity
        << ",\"output\":" << f.output << ",\"threshold\":" << f.threshold << ",\"stoppedVelocity\":" << f.stoppedVelocity
        << ",\"direction\":" << f.direction << ",\"tracking\":" << (f.tracking?"true":"false")
        << ",\"velocityWindowMs\":" << f.velocityWindowMs;
      if(t.preparationRequired>0)
        j << ",\"requiredTravel\":" << t.preparationRequired << ",\"availableTravel\":" << t.preparationAvailable
          << ",\"preparationAttempts\":" << t.preparationAttempts;
      j << '}';
    }
    j << ",\"options\":{";
    bool comma=false;
    for(const auto &field:autoTuneRealFields) {
      if(comma)j << ',';
      comma=true;j << jsonQuoted(field.name) << ':' << t.options.*field.member;
    }
    for(const auto &field:autoTuneIntFields)j << ',' << jsonQuoted(field.name) << ':' << t.options.*field.member;
    j << ",\"methodVersion\":14},\"timeoutMs\":" << (t.trackingPeriodMs?t.trackingTimeoutMs:120000u)
      << ",\"minimumCandidate\":" << t.minimumCandidate
      << ",\"minimumSetpoint\":" << t.minimumSetpoint
      << ",\"minimumVelocity\":" << t.minimumSpeed
      << ",\"minimumOutput\":" << t.minimumOutput
      << ",\"minimumVerified\":" << (t.phase==AutoTunePhase::Complete && t.minimumCompletePending?"true":"false");
    j << ",\"tracking\":{\"verified\":" << (t.trackingVerified && t.phase==AutoTunePhase::Complete?"true":"false")
      << ",\"candidate\":" << t.trackingCandidate << ",\"case\":" << t.trackingCase
      << ",\"selected\":" << t.trackingBest << ",\"periodMs\":" << t.trackingPeriodMs
      << ",\"casesPerCandidate\":" << cases
      << ",\"slowVelocity\":" << (t.options.has(TuneSlow)?t.trackingSlow:0)
      << ",\"fastVelocity\":" << (t.options.has(TuneFast)?t.trackingFast:0)
      << ",\"reserve\":" << t.trackingReserve << ",\"warmupMs\":[";
    for(unsigned n=0;n<4;++n) {if(n)j << ',';j << t.trackingWarmupMs[n];}
    j << "],\"durationMs\":[";
    for(unsigned n=0;n<4;++n) {if(n)j << ',';j << t.trackingDurationMs[n];}
    j << "],\"candidates\":[";
    for(unsigned n=0;n<t.trackingResults.size();++n) {
      if(n)j << ',';
      const auto &r=t.trackingResults[n];
      j << "{\"score\":" << r.score << ",\"meanError\":" << r.meanError << ",\"peakError\":" << r.peakError
        << ",\"overshoot\":" << r.overshoot << ",\"completed\":" << r.completed
        << ",\"kicks\":" << r.kicks << ",\"reversals\":" << r.reversals << ",\"stoppedMs\":" << r.stoppedMs
        << ",\"rejected\":" << (r.rejected?"true":"false") << ",\"failedCase\":" << r.failedCase << ",\"reason\":" << jsonQuoted(r.reason) << '}';
    }
    j << "]}";
  }
  if(full && t.phase==AutoTunePhase::Complete) {
    const auto &m=t.proposedMotion;const auto &s=t.proposedSpeed;
    j << ",\"result\":{\"speedPercent\":" << s.speedPercent << ",\"approachSpeedPercent\":" << s.approachSpeedPercent
      << ",\"accelerationMs\":" << s.accelerationMs << ",\"decelerationMs\":" << s.decelerationMs
      << ",\"slowdownDegrees\":" << s.slowdownDegrees << ",\"coastDegrees\":" << s.coastDegrees
      << ",\"decelerationFactor\":" << s.decelerationFactor
      << ",\"holdEnabled\":" << (m.holdEnabled?"true":"false")
      << ",\"kickEnabled\":" << (m.kickEnabled?"true":"false");
    j << ",\"minimumSetpoint\":" << t.minimumSetpoint
      << ",\"minimumVelocity\":" << t.minimumSpeed << ",\"minimumOutput\":" << t.minimumOutput;
    for(const auto &field:motionRealFields)j << ",\"" << field.name << "\":" << m.*field.member;
    for(const auto &field:motionIntFields)j << ",\"" << field.name << "\":" << m.*field.member;
    j << '}';
  }
  if(full && t.id) {
    j << ",\"origin\":" << t.origin << ",\"lower\":" << t.lower << ",\"upper\":" << t.upper
      << ",\"probeOutput\":" << t.level << ",\"relocations\":" << t.relocations
      << ",\"peakVelocity\":" << t.peakVelocity << ",\"stoppingDistance\":" << t.stoppingDistance << ",\"measurements\":[";
    for(unsigned i=0;i<2;++i) {
      if(i)j << ',';
      const auto &v=t.measurements[i];
      j << "{\"startOutput\":" << v.startOutput << ",\"lowOutput\":" << v.lowOutput << ",\"highOutput\":" << v.highOutput
        << ",\"lowVelocity\":" << v.lowVelocity << ",\"highVelocity\":" << v.highVelocity
        << ",\"startMs\":" << v.startMs << ",\"responseSeconds\":" << v.responseSeconds << ",\"coastDegrees\":" << v.coastDegrees << '}';
    }
    j << ']';
  }
  j << '}';return j.good();
}
inline void writeAxisMotion(JsonWriter &j, const Controller &c, unsigned i) {
  const auto &a=c.axes[i]; const auto &m=reportedMotion(c,i);
  j << ",\"regulatorMode\":" << jsonQuoted(regulatorName(m.mode))
    << ",\"motionReady\":" << (m.ready ? "true" : "false")
    << ",\"motionPhase\":" << jsonQuoted(motionPhaseName(a.motionPhase))
    << ",\"motionFault\":" << jsonQuoted(motionErrorName(a.motionError))
    << ",\"measuredVelocity\":" << a.measuredVelocity << ",\"requestedVelocity\":" << a.requestedVelocity
    << ",\"profileAcceleration\":" << a.profileAcceleration
    << ",\"holdEnabled\":" << (m.mode==RegulatorMode::Cascade && m.holdEnabled ? "true" : "false")
    << ",\"holdArmed\":" << (a.holdArmed ? "true" : "false");
}
inline bool writeMotionConfiguration(JsonWriter &j, const Controller &c) {
  j << "{\"controlPeriodMs\":" << c.controlPeriodMs() << ",\"axes\":[";
  for (unsigned i=0;i<2;++i) {
    if(i) j << ',';
    const auto &m=reportedMotion(c,i);
    j << "{\"regulatorMode\":" << jsonQuoted(regulatorName(m.mode))
      << ",\"ready\":" << (m.ready?"true":"false")
      << ",\"kickEnabled\":" << (m.kickEnabled?"true":"false")
      << ",\"holdEnabled\":" << (m.holdEnabled?"true":"false");
    for (const auto &f:motionRealFields) j << ",\"" << f.name << "\":" << m.*f.member;
    for (const auto &f:motionIntFields) j << ",\"" << f.name << "\":" << m.*f.member;
    j << '}';
  }
  j << "]}"; return j.good();
}
} // namespace forkrot
