// Motion control shared by firmware and native tests. GPL-3.0.
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include "motion_timing.hpp"
namespace forkrot {
enum class RegulatorMode : uint8_t { Simple, Cascade };
enum class MotionPhase : uint8_t { Idle, Kick, Run, Braking, Holding, Fault };
enum class MotionError : uint8_t { None, KickTimeout, FeedbackStale, FeedbackInvalid, Stalled, Planner };
const char *regulatorName(RegulatorMode);
const char *motionPhaseName(MotionPhase);
const char *motionErrorName(MotionError);
// Allocate the bounded shared scratch once, before control is admitted.
bool prepareMotionPlanner();
struct MotionSettings {
  RegulatorMode mode = RegulatorMode::Simple;
  // Readiness is automatic; retained in the record/API for compatibility.
  bool ready = true, kickEnabled = false, holdEnabled = false;
  float outputLimitPercent = 100, kickPercent = 50, movementDegrees = 5.f;
  float velocity = 3, acceleration = 2, deceleration = 2, jerk = 8;
  float positionKp = 2, positionKi = 0, positionKd = .2f;
  float velocityKp = 10, velocityKi = 5, velocityKd = 0;
  float feedforward = 7.462687f;
  float positionTolerance = .5f, holdResumeDegrees = 1, stoppedVelocity = .1f;
  unsigned kickMs = 500, kickReleaseMs = 100, velocityWindowMs = 200;
  unsigned derivativeFilterMs = 100, feedbackTimeoutMs = 500, settleMs = 300, stallMs = 3000;
  unsigned holdReverseMs = 300;
  unsigned sustainingPercent = 0;
  bool valid() const;
  bool operator==(const MotionSettings &) const;
  bool operator!=(const MotionSettings &o) const { return !(*this == o); }
};
struct MotionRealField { const char *name; float MotionSettings::*member; float lo, hi; };
inline constexpr MotionRealField motionRealFields[] = {
  {"OutputLimitPercent", &MotionSettings::outputLimitPercent, 1, 100},
  {"KickPercent", &MotionSettings::kickPercent, 1, 100},
  {"MovementDegrees", &MotionSettings::movementDegrees, .001f, 10},
  {"Velocity", &MotionSettings::velocity, .001f, 360},
  {"Acceleration", &MotionSettings::acceleration, .001f, 1000},
  {"Deceleration", &MotionSettings::deceleration, .001f, 1000},
  {"Jerk", &MotionSettings::jerk, .001f, 10000},
  {"PositionKp", &MotionSettings::positionKp, 0, 1000},
  {"PositionKi", &MotionSettings::positionKi, 0, 1000},
  {"PositionKd", &MotionSettings::positionKd, 0, 1000},
  {"VelocityKp", &MotionSettings::velocityKp, 0, 10000},
  {"VelocityKi", &MotionSettings::velocityKi, 0, 10000},
  {"VelocityKd", &MotionSettings::velocityKd, 0, 10000},
  {"Feedforward", &MotionSettings::feedforward, 0, 100000},
  {"PositionTolerance", &MotionSettings::positionTolerance, .001f, 90},
  {"HoldResumeDegrees", &MotionSettings::holdResumeDegrees, .001f, 180},
  {"StoppedVelocity", &MotionSettings::stoppedVelocity, .001f, 10}
};
struct MotionIntField { const char *name; unsigned MotionSettings::*member; unsigned lo, hi; };
inline constexpr MotionIntField motionIntFields[] = {
  {"SustainingPercent", &MotionSettings::sustainingPercent, 0, 100},
  {"KickMs", &MotionSettings::kickMs, 20, 10000},
  {"KickReleaseMs", &MotionSettings::kickReleaseMs, 20, 2000},
  {"VelocityWindowMs", &MotionSettings::velocityWindowMs, 40, 600},
  {"DerivativeFilterMs", &MotionSettings::derivativeFilterMs, 20, 2000},
  {"FeedbackTimeoutMs", &MotionSettings::feedbackTimeoutMs, 100, 5000},
  {"SettleMs", &MotionSettings::settleMs, 20, 5000},
  {"HoldReverseMs", &MotionSettings::holdReverseMs, 0, 2000},
  {"StallMs", &MotionSettings::stallMs, 500, 30000}
};
// Version 3 stores SustainingPercent and HoldReverseMs in the reserved header
// bytes. Preserve the legacy length and field offsets, including the drive NVS.
inline constexpr size_t MotionRecordSize = 12 + 4 * (std::size(motionRealFields) + std::size(motionIntFields) - 2);
using MotionRecord = std::array<uint8_t, MotionRecordSize>;
MotionRecord encodeMotionSettings(const MotionSettings &);
bool decodeMotionSettings(const MotionRecord &, MotionSettings &);
// Timestamped, bounded least-squares velocity estimator; unchanged samples are real
// zero-motion evidence only when a new ADC conversion has actually occurred.
struct MotionFeedback {
  struct Sample { float position = 0; uint32_t at = 0; };
  // Preserve the entire maximum 600 ms window, including both endpoints.
  std::array<Sample, 8> samples{};
  unsigned count = 0, next = 0, rejectedSamples = 0;
  bool timed = false, valid = false;
  uint32_t at = 0, rejectedAt = 0;
  uint32_t rejectedTotal = 0, invalidEvents = 0, rejectedIntervalMs = 0;
  float rejectedPosition = 0, lastGoodPosition = 0;
  float velocity = 0;
  float coastSeconds = 0, coastPosition = 0, coastVelocity = 0;
  // Separate directional distance/v^2 estimates for a stationary goal.
  // The stream controller retains its original coastSeconds model.
  std::array<float, 2> staticCoastFactor{};
  uint32_t coastAt = 0;
  int coastDirection = 0;
  bool powered = false, coastTracking = false;
  void sample(float position, bool calibrated, uint32_t stamp, unsigned window);
  float velocityInWindow(unsigned window) const;
  bool progressed(int direction, float degrees, unsigned window) const;
  float positionMean(unsigned window) const;
  float staticCoastDistance(float velocity) const;
};
// Only transient regulation state is cleared by STOP/idle. Feedback and
// timing have a separate lifetime and must never be copied through the stack.
struct MotionState {
  bool started=false,progressValid=false;
  uint32_t at=0,nextAt=0,progressAt=0;
  float progressPosition=0;
  int progressDirection=0;
};
struct MotionRuntime : MotionState {
  MotionFeedback feedback;
  MotionTiming timing;
  void cancel(bool retainTarget = false);
};
} // namespace forkrot
