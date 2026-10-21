// Motion control shared by firmware and native tests. GPL-3.0.
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
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
  {"StallMs", &MotionSettings::stallMs, 500, 30000}
};
// SustainingPercent fits in a reserved header byte. Keep the legacy record
// length and field offsets, including inside the combined drive NVS value.
inline constexpr size_t MotionRecordSize = 12 + 4 * (std::size(motionRealFields) + std::size(motionIntFields) - 1);
using MotionRecord = std::array<uint8_t, MotionRecordSize>;
MotionRecord encodeMotionSettings(const MotionSettings &);
bool decodeMotionSettings(const MotionRecord &, MotionSettings &);
struct MotionPid {
  float integral = 0, derivative = 0, lastMeasurement = 0;
  bool initialized = false;
  void reset() { *this = {}; }
  float step(float error, float measurement, float feedforward, float kp, float ki, float kd,
             float dt, float filterSeconds, float lo, float hi, bool integrate = true);
};
// Timestamped, bounded least-squares velocity estimator; unchanged samples are real
// zero-motion evidence only when a new ADC conversion has actually occurred.
struct MotionFeedback {
  struct Sample { float position = 0; uint32_t at = 0; };
  std::array<Sample, 32> samples{};
  unsigned count = 0, next = 0, rejectedSamples = 0;
  bool timed = false, valid = false;
  uint32_t at = 0, rejectedAt = 0;
  float velocity = 0;
  float coastSeconds = 0, coastPosition = 0, coastVelocity = 0;
  uint32_t coastAt = 0;
  int coastDirection = 0;
  bool powered = false, coastTracking = false;
  void sample(float position, bool calibrated, uint32_t stamp, unsigned window);
  bool progressed(int direction, float degrees, unsigned window) const;
};
struct MotionRuntime {
  MotionFeedback feedback;
  MotionPid positionPid, velocityPid;
  bool started = false, profileValid = false, settling = false, releasing = false, arriving = false;
  bool progressValid = false, kickDone = false, streamingTarget = false, profileTracking = false;
  uint32_t targetChangedAt = 0;
  uint32_t at = 0, kickAt = 0, releaseAt = 0, settledAt = 0, progressAt = 0;
  float referencePosition = 0, referenceVelocity = 0, referenceAcceleration = 0;
  float ramp = 0, rampRate = 0, kickPosition = 0, progressPosition = 0;
  float kickBase = 0, kickPeak = 0, releaseOutput = 0;
  float commandVelocity = 0, commandAcceleration = 0;
  float plannedSpeedLimit = 0;
  int kickDirection = 0, targetDirection = 0;
  bool crossedTarget = false;
  void cancel();
};
} // namespace forkrot
