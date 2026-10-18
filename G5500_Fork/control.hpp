// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "hardware_config.hpp"
#include "az_range.hpp"
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
  ManualDrive
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
  unsigned speedPercent = 100, startSpeedPercent = 18, approachSpeedPercent = 18;
  unsigned accelerationMs = 1800, decelerationMs = 1200, slowdownDegrees = 3;
  float coastDegrees = 0, decelerationFactor = 1;
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
  unsigned &speedPercent = speeds[0].speedPercent;
  unsigned &startSpeedPercent = speeds[0].startSpeedPercent;
  unsigned &approachSpeedPercent = speeds[0].approachSpeedPercent;
  unsigned &accelerationMs = speeds[0].accelerationMs;
  unsigned &decelerationMs = speeds[0].decelerationMs;
  unsigned &slowdownDegrees = speeds[0].slowdownDegrees;
  bool driveFault = false;
  bool variableSpeed(unsigned i) const;
  float tolerance(unsigned i) const {
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
  Controller();
  void feedback(float az, float el, bool azCal, bool elCal);
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
  void closeManual(ManualSession &, uint32_t now);
  uint32_t autoHourStartedAt_ = 0, sequenceAdvancedAt_ = 0, sequenceIntervalMs_ = 0;
  uint32_t lastSpeedUpdateAt_ = 0;
  bool speedUpdateStarted_ = false;
  FixedVector<Target, 16> sequence_, pendingSequence_;
  size_t sequenceIndex_ = 0;
  Result start(Target t, uint32_t now, bool external = false);
  void output(unsigned axis, int dir, uint32_t now);
  void expire(uint32_t now);
};
bool number(std::string_view s, float &out);
float heading(float angle);
float nearest(float angle, float current, float maximum, bool bidirectional = false);
float nearest(float angle, float current, AzRange range, bool bidirectional = false);
} // namespace forkrot
