// GPL-3.0. Shared bounded projections; controller coefficients stay out of live frames.
#pragma once
#include "control.hpp"
#include "json_writer.hpp"
namespace forkrot {
inline void writeAxisMotion(JsonWriter &j, const Controller &c, unsigned i) {
  const auto &a=c.axes[i]; const auto &m=c.motionSettings[i];
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
  j << "{\"axes\":[";
  for (unsigned i=0;i<2;++i) {
    if(i) j << ',';
    const auto &m=c.motionSettings[i];
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
