// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace forkrot {
enum class Model { G5500, G800DXA };
enum class Mode { Manual, Auto, AutoHour };
enum class Result { Ok, Pending, Ignored, Invalid, NotCalibrated };
struct Target {
  float az = 0, el = 0;
  bool hasAz = true, hasEl = false;
};
struct SpeedSettings {
  unsigned speedPercent = 100, startSpeedPercent = 18, approachSpeedPercent = 18;
  unsigned accelerationMs = 1800, decelerationMs = 1200, slowdownDegrees = 3;
};
struct Axis {
  float current = 0, target = 0;
  bool calibrated = false, tracking = false, completed = false, accepted = false;
  float acceptedTarget = 0;
  int direction = 0, wanted = 0, previous = 0;
  uint32_t offAt = 0, runAt = 0;
  bool hasRun = false;
  float speedOutputPercent = 0;
};
struct Preset {
  std::string label;
  Target target;
};
class Controller {
public:
  Model model = Model::G5500;
  Mode mode = Mode::Manual;
  float maximum = 450;
  std::array<Axis, 2> axes{};
  std::array<Preset, 4> presets{};
  Target pending{};
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
  static constexpr float tolerance(unsigned i) { return i ? EL_STOP_TOLERANCE_DEG : AZ_STOP_TOLERANCE_DEG; }
  size_t sequenceIndex() const { return sequence_.empty() ? 0 : sequenceIndex_ + 1; }
  size_t sequenceCount() const { return sequence_.empty() ? pendingSequence_.size() : sequence_.size(); }
  Controller(const Controller &other);
  Controller &operator=(const Controller &other);
  uint32_t stopEpoch = 0;
  std::string fault;
  Controller();
  void feedback(float az, float el, bool azCal, bool elCal);
  Result target(Target t, bool external, uint32_t now);
  Result jog(unsigned axis, int direction, bool external, uint32_t now);
  void stop(int axis = -1);
  Result execute(uint32_t now);
  Result setMode(Mode m, uint32_t now);
  void restoreMode(Mode m); // An expired/nonpersistent hour always boots manual.
  void setModel(Model m);
  void setMaximum(float value);
  Result preset(unsigned slot, uint32_t now);
  Result savePreset(unsigned slot, const std::string &label, Target t);
  Result sequence(const std::vector<Target> &points, uint32_t interval, uint32_t now);
  void cancelSequence();
  void tick(uint32_t now);
  uint32_t remaining(uint32_t now) const;
  bool valid(Target t) const;
  bool moving() const;
  static const char *modeName(Mode m);
  static const char *resultName(Result r);

private:
  uint32_t autoHourStartedAt_ = 0, sequenceAdvancedAt_ = 0, sequenceIntervalMs_ = 0;
  uint32_t lastSpeedUpdateAt_ = 0;
  bool speedUpdateStarted_ = false;
  std::vector<Target> sequence_, pendingSequence_;
  size_t sequenceIndex_ = 0;
  Result start(Target t, uint32_t now, bool external = false);
  void output(unsigned axis, int dir, uint32_t now);
  void expire(uint32_t now);
};
bool number(const std::string &s, float &out);
float heading(float angle);
float nearest(float angle, float current, float maximum, bool bidirectional = false);
} // namespace forkrot
