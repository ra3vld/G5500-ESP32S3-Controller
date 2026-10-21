// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "hardware_config.hpp"
#include "az_range.hpp"
#include "motion.hpp"
#include "runtime_speeds.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <string_view>
#include "fixed_vector.hpp"
namespace forkrot {
enum class Model { G5500, G800DXA };
enum class Mode { Manual, Auto, AutoHour };
enum class Result {
  Ok,
  Pending,
  Ignored,
  Invalid,
  NotCalibrated,
  Deferred,
  Unavailable,
  Busy,
  Timeout,
  Cancelled
};
struct Target {
  float az = 0, el = 0;
  bool hasAz = true, hasEl = false;
};
// Commands carry intent, never GPIO levels or simulated motor state.
enum class ControlOp : uint8_t {
  Target,
  Jog,
  Stop,
  Execute,
  Mode,
  Preset,
  SavePreset,
  Sequence,
  ReleaseJog,
  ToggleMode,
  ExecuteDisplayed,
  ManualDrive,
  ResetMotion
};
struct ControlCommand {
  ControlOp op = ControlOp::Stop;
  Target target{};
  bool external = false;
  bool followInManual = false;
  int axis = -1, direction = 0;
  unsigned slot = 0;
  Mode mode = Mode::Manual;
  uint32_t interval = 1000;
  uint8_t count = 0;
  std::array<Target, 16> points{};
  char label[49] = {};
  uint32_t gesture = 0;
  uint8_t axesMask = 0;
  float manual[2]{};
  bool release = false;
};
class CommandSink {
public:
  virtual ~CommandSink() = default;
  virtual Result submit(const ControlCommand &) = 0;
  virtual Result submitAt(const ControlCommand &command, uint32_t) { return submit(command); }
  virtual bool available() const = 0;
  virtual uint32_t requestId() const = 0;
  virtual uint32_t selectionGeneration() const { return 0; }
  virtual Result lastResult() const = 0;
  virtual float intendedTarget(unsigned, float fallback) const { return fallback; }
  virtual bool manualAvailable() const {
    return false;
  }
};
struct SpeedSettings {
  unsigned speedPercent = 50, startSpeedPercent = 18, approachSpeedPercent = 18;
  unsigned accelerationMs = 1800, decelerationMs = 1200, slowdownDegrees = 3;
  float coastDegrees = 0, decelerationFactor = 1;
  bool operator==(const SpeedSettings &o) const {
    return speedPercent==o.speedPercent && startSpeedPercent==o.startSpeedPercent &&
      approachSpeedPercent==o.approachSpeedPercent && accelerationMs==o.accelerationMs &&
      decelerationMs==o.decelerationMs && slowdownDegrees==o.slowdownDegrees &&
      coastDegrees==o.coastDegrees && decelerationFactor==o.decelerationFactor;
  }
};
struct Axis {
  float current = 0, target = 0;
  bool calibrated = false, tracking = false, completed = false, accepted = false;
  float acceptedTarget = 0;
  int direction = 0, wanted = 0, previous = 0;
  uint32_t offAt = 0, runAt = 0;
  bool hasRun = false;
  bool targetKnown = false;
  float speedOutputPercent = 0;
  float measuredVelocity = 0, requestedVelocity = 0, profileAcceleration = 0;
  MotionPhase motionPhase = MotionPhase::Idle;
  MotionError motionError = MotionError::None;
  bool holdArmed = false;
};
enum class AutoTunePhase : uint8_t { Idle, Quiet, Start, Low, High, Coast, Reposition, Validate, Complete, Cancelled, Failed };
struct AutoTuneMeasurement {
  float startOutput = 0, lowOutput = 0, highOutput = 0, lowVelocity = 0, highVelocity = 0;
  float responseSeconds = 0, coastDegrees = 0;
  uint32_t startMs = 0;
};
// Volatile methodology for one identification run; never stored in drive settings.
struct AutoTuneOptions {
  float searchStartPercent = 5, searchStepPercent = 5, workingFraction = .5f;
  float lambdaFactor = 1, positionGainFactor = 1;
  unsigned startupObservationMs = 500, plateauMinMs = 200, plateauTimeoutMs = 2500;
  bool valid(float outputLimit) const;
};
struct AutoTuneRealField { const char *name; float AutoTuneOptions::*member; float lo, hi; };
inline constexpr AutoTuneRealField autoTuneRealFields[] = {
  {"tuneSearchStartPercent", &AutoTuneOptions::searchStartPercent, 1, 100},
  {"tuneSearchStepPercent", &AutoTuneOptions::searchStepPercent, 1, 20},
  {"tuneWorkingFraction", &AutoTuneOptions::workingFraction, .1f, 1},
  {"tuneLambdaFactor", &AutoTuneOptions::lambdaFactor, .5f, 4},
  {"tunePositionGainFactor", &AutoTuneOptions::positionGainFactor, .25f, 2}
};
struct AutoTuneIntField { const char *name; unsigned AutoTuneOptions::*member; unsigned lo, hi; };
inline constexpr AutoTuneIntField autoTuneIntFields[] = {
  {"tuneStartupObservationMs", &AutoTuneOptions::startupObservationMs, 100, 2000},
  {"tunePlateauMinMs", &AutoTuneOptions::plateauMinMs, 160, 1000},
  {"tunePlateauTimeoutMs", &AutoTuneOptions::plateauTimeoutMs, 1000, 10000}
};
struct AutoTuneState {
  AutoTuneOptions options{};
  AutoTunePhase phase = AutoTunePhase::Idle;
  RegulatorMode mode = RegulatorMode::Simple;
  int axis = -1, direction = 1;
  AutoTunePhase nextPhase = AutoTunePhase::Start;
  int nextDirection = 1;
  unsigned relocations = 0;
  uint32_t id = 0, startedAt = 0, phaseAt = 0, tickAt = 0, sampledAt = 0, revision = 0, settledAt = 0;
  const char *reason = "";
  float origin = 0, otherPosition = 0, lower = 0, upper = 0, limit = 0, level = 0;
  float phasePosition = 0, noiseLow = 0, noiseHigh = 0, noiseVelocity = 0, threshold = 0;
  float peakVelocity = 0, validationError = 0, validationTarget = 0;
  float validationOrigin = 0;
  unsigned validationPass = 0, validationReversals = 0;
  int validationLastDirection = 0;
  float nextLevel = 0, searchLevel = 0, minimumLevel = 0;
  float lastPosition = 0, sampleVelocity = 0, stoppingDistance = 0;
  float plateauVelocity = 0;
  uint32_t positionAt = 0, plateauAt = 0;
  std::array<float, 16> positions{};
  std::array<uint32_t, 16> sampleTimes{};
  unsigned sampleCount = 0, sampleNext = 0;
  bool internal = false, ticked = false, settled = false, relocate = false, plateauReady = false;
  SpeedSettings originalSpeed{}, proposedSpeed{};
  MotionSettings originalMotion{}, proposedMotion{};
  std::array<AutoTuneMeasurement, 2> measurements{};
  struct Plateau { float output = 0, velocity = 0, responseSeconds = 0; };
  std::array<Plateau, 2> confirmedHigh{};
  bool active() const { return phase >= AutoTunePhase::Quiet && phase <= AutoTunePhase::Validate; }
};
struct Preset {
  std::string label;
  Target target;
};
class Controller {
public:
  HardwareConfig hardware;
  bool configurationLocked = false;
  void configureHardware(const HardwareConfig &config);
  CommandSink *remote = nullptr; // Non-owning. A mirror must never run tick()/feedback().
  uint32_t motionEpoch[2] = {};
  uint32_t snapshotAt = 0, remoteRemaining = 0;
  size_t remoteSequenceIndex = 0, remoteSequenceCount = 0;
  bool remoteVariable[2] = {};
  float remoteTolerance[2] = {};
  float effectiveTolerance(unsigned i) const { return remote ? remoteTolerance[i] : tolerance(i); }
  bool available() const { return !remote || remote->available(); }
  float intendedTarget(unsigned axis, float fallback) const {
    return remote ? remote->intendedTarget(axis, fallback) : fallback;
  }
  Model model = Model::G5500;
  Mode mode = Mode::Manual;
  AzRange azRange;
  RotationRange elRange{0,180};
  uint8_t elProfile = EL_STANDARD;
  const RotationRange &range(unsigned axis) const { return axis ? elRange : azRange; }
  float &minimum = azRange.start, &maximum = azRange.end;
  uint8_t azProfile = AZ_P45;
  uint32_t rangeRevision = 1;
  std::array<Axis, 2> axes{};
  std::array<Preset, 4> presets{};
  Target pending{};
  Target lastTarget{0, 0, false, false}; // Display/repeat only; never starts motion by itself.
  bool pendingValid = false, presetActive = false, targetKnown = false;
  int presetSlot = -1;
  bool speedEnabled = DRIVE_INTERFACE != DRIVE_YAESU;
  std::array<SpeedSettings, 2> speeds{};
  std::array<MotionSettings, 2> motionSettings{};
  AutoTuneState autoTune{};
  Result centerForAutoTune(unsigned axis, uint32_t now);
  Result startAutoTune(unsigned axis, RegulatorMode mode, float travel, float outputLimit, uint32_t now, const AutoTuneOptions &options = {});
  Result cancelAutoTune(uint32_t now);
  Result applyAutoTune(uint32_t id, uint32_t now);
  static const char *autoTunePhaseName(AutoTunePhase);
  unsigned &speedPercent = speeds[0].speedPercent;
  unsigned &startSpeedPercent = speeds[0].startSpeedPercent;
  unsigned &approachSpeedPercent = speeds[0].approachSpeedPercent;
  unsigned &accelerationMs = speeds[0].accelerationMs;
  unsigned &decelerationMs = speeds[0].decelerationMs;
  unsigned &slowdownDegrees = speeds[0].slowdownDegrees;
  bool driveFault = false;
  bool variableSpeed(unsigned i) const;
  bool runtimeSpeedAvailable(unsigned axis) const;
  float runtimeSpeed(unsigned axis) const;
  float runtimeSpeedLimit(unsigned axis) const;
  RuntimeSpeeds runtimeSpeeds;
  void resetRuntimeSpeed(unsigned axis, uint32_t now);
  Result setRuntimeSpeed(unsigned axis, float value, uint32_t now);
  float tolerance(unsigned i) const {
    if (variableSpeed(i) && motionSettings[i].mode == RegulatorMode::Cascade)
      return motionSettings[i].positionTolerance;
    return std::min(speeds[i].coastDegrees, range(i).span() / 10);
  }
  size_t sequenceIndex() const {
    return remote ? remoteSequenceIndex : sequence_.empty() ? 0 : sequenceIndex_ + 1;
  }
  size_t sequenceCount() const {
    return remote ? remoteSequenceCount : sequence_.empty() ? pendingSequence_.size() : sequence_.size();
  }
  Controller(const Controller &other);
  Controller &operator=(const Controller &other);
  uint32_t stopEpoch = 0;
  std::string fault;
  bool hasDriveError() const {
    return driveFault || !fault.empty() || axes[0].motionError != MotionError::None ||
           (model == Model::G5500 && axes[1].motionError != MotionError::None);
  }
  Controller();
  void feedback(float az, float el, bool azCal, bool elCal);
  void feedbackAt(unsigned axis, float position, bool calibrated, uint32_t sampledAt);
  // A changed calibration starts a new measurement coordinate system.
  void resetFeedback(int axis, uint32_t now);
  bool configureMotion(unsigned axis, const MotionSettings &, uint32_t now);
  Result resetMotion(int axis, uint32_t now);
  Result target(Target t, bool external, uint32_t now, bool followInManual = false);
  Result jog(unsigned axis, int direction, bool external, uint32_t now);
  void stop(int axis = -1);
  void stopNow(int axis, uint32_t now);
  bool stopOwnedJog(unsigned axis, uint32_t epoch, uint32_t now);
  Result execute(uint32_t now);
  Target displayedTarget() const;
  Result executeDisplayed(uint32_t now);
  bool manualAvailable() const {
    return !remote || remote->manualAvailable();
  }
  Result manualDrive(uint64_t source, uint64_t boot, uint32_t gesture, uint32_t serial,
                     uint8_t mask, float az, float el, bool release, uint32_t now);
  Result setMode(Mode m, uint32_t now);
  void restoreMode(Mode m); // An expired/nonpersistent hour always boots manual.
  void setModel(Model m);
  void setMaximum(float value);
  bool setRange(AzRange range, uint8_t profile, uint32_t now = 0);
  bool setElRange(RotationRange range, uint8_t profile, uint32_t now = 0);
  Result preset(unsigned slot, uint32_t now);
  Result savePreset(unsigned slot, const std::string &label, Target t);
  Result sequence(const Target *points, size_t count, uint32_t interval, uint32_t now,
                  bool followInManual = false);
  Result sequence(const std::vector<Target> &points, uint32_t interval, uint32_t now,
                  bool followInManual = false) {
    return sequence(points.data(), points.size(), interval, now, followInManual);
  }
  void cancelSequence();
  void tick(uint32_t now);
  uint32_t remaining(uint32_t now) const;
  bool valid(Target t) const;
  bool moving() const;
  static const char *modeName(Mode m);
  static const char *resultName(Result r);

private:
  struct ManualSession {
    uint64_t source = 0, boot = 0;
    uint32_t gesture = 0, serial = 0, renewed = 0, epoch[2]{};
    uint8_t claimed = 0, blocked = 0;
    bool used = false, closed = false;
  } manualSessions_[4];
  float manualScale_[2] = {1, 1};
  unsigned runtimeSpeedIndex(unsigned axis) const;
  void closeManual(ManualSession &, uint32_t now);
  uint32_t autoHourStartedAt_ = 0, sequenceAdvancedAt_ = 0, sequenceIntervalMs_ = 0;
  uint32_t lastSpeedUpdateAt_ = 0;
  bool speedUpdateStarted_ = false;
  std::array<MotionRuntime, 2> motion_{};
  void cancelMotion(unsigned axis);
  void motionFault(unsigned axis, MotionError, uint32_t now);
  void recoverStall(unsigned axis, uint32_t now);
  void tickMotion(unsigned axis, uint32_t now);
  void tickAutoTune(uint32_t now);
  void finishAutoTune(AutoTunePhase, const char *reason, uint32_t now);
  void autoTunePhase(AutoTunePhase, uint32_t now);
  void autoTuneLevel(float level);
  void raiseAutoTuneMinimum(uint32_t now);
  bool autoTuneProbe(AutoTunePhase, int direction, float level, uint32_t now);
  void autoTuneCoast(AutoTunePhase next, int direction, float level, bool relocate, uint32_t now);
  bool proposeAutoTune(uint32_t now);
  FixedVector<Target, 16> sequence_, pendingSequence_;
  size_t sequenceIndex_ = 0;
  Result start(Target t, uint32_t now, bool retryStall = true);
  void output(unsigned axis, int dir, uint32_t now);
  void expire(uint32_t now);
};
bool number(std::string_view s, float &out);
float heading(float angle);
float nearest(float angle, float current, float maximum, bool bidirectional = false);
float nearest(float angle, float current, AzRange range, bool bidirectional = false);
} // namespace forkrot
