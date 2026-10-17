#include "hardware.hpp"
#include "storage.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
namespace forkrot::crowpanel {
bool beginBus() {
  return true;
}
} // namespace forkrot::crowpanel
using namespace forkrot;
static bool close(float a, float b) {
  return std::abs(a - b) < .002f;
}
int main() {
  static_assert(DRIVE_SIMULATION);
  Controller c;
  c.feedback(100, 90, true, true);
  c.speeds[0] = {80, 20, 10, 1000, 500, 10};
  c.tick(0);
  assert(c.variableSpeed(0) && c.variableSpeed(1));
  auto jog = [&](unsigned seq, float scale, unsigned at) {
    return c.manualDrive(1, 1, 1, seq, 1, scale, 0, false, at);
  };
  assert(jog(1, .5f, 0) == Result::Ok);
  c.tick(0);
  assert(close(c.axes[0].speedOutputPercent, 20));
  c.tick(100);
  assert(close(c.axes[0].current, 100.268f) && close(c.axes[0].speedOutputPercent, 28));
  c.tick(200);
  assert(close(c.axes[0].speedOutputPercent, 36));
  c.tick(300);
  assert(close(c.axes[0].speedOutputPercent, 40));
  assert(jog(2, .25f, 300) == Result::Ok);
  c.tick(400);
  assert(close(c.axes[0].speedOutputPercent, 24));
  c.tick(500);
  assert(close(c.axes[0].speedOutputPercent, 20));
  jog(3, -1, 500);
  c.tick(600);
  assert(c.axes[0].direction == 1 && close(c.axes[0].speedOutputPercent, 4));
  c.tick(700);
  assert(c.axes[0].direction == 0);
  c.tick(999);
  assert(c.axes[0].direction == 0);
  c.tick(1000);
  assert(c.axes[0].direction == -1);
  const auto pos = c.axes[0].current;
  c.tick(1100);
  assert(c.axes[0].current < pos);
  c.stopNow(-1, 1100);
  const float stopped = c.axes[0].current;
  c.tick(1200);
  assert(close(c.axes[0].current, stopped));
  assert(c.speeds[0].speedPercent == 80);
  Controller axes;
  axes.feedback(100, 90, true, true);
  axes.speeds[0] = {80, 80, 80, 0, 0, 1};
  axes.speeds[1] = {40, 40, 40, 0, 0, 1};
  axes.tick(0);
  axes.target({200, 170, true, true}, false, 0);
  axes.tick(0);
  axes.tick(1000);
  assert(close(axes.axes[0].current, 110.72f) && close(axes.axes[1].current, 95.36f));
  axes.stop();
  axes.feedback(199, 169, true, true);
  axes.speeds[0].approachSpeedPercent = 10;
  axes.speeds[0].slowdownDegrees = 10;
  axes.target({200, 0, true, false}, false, 1100);
  axes.tick(1100);
  assert(axes.axes[0].speedOutputPercent < 20);
  axes.tick(20000);
  assert(close(axes.axes[0].current, 200) && !axes.axes[0].tracking);
  // Virtual PWM settings migrate from the legacy AZ defaults and persist per axis.
  Preferences::database.clear();
  Storage storage;
  Runtime runtime;
  storage.loadForkSettings(runtime);
  runtime.controller.speeds[0] = {12, 4, 3, 1000, 500, 5};
  runtime.controller.speeds[1] = {35, 7, 4, 1200, 700, 8};
  storage.saveForkSettings(runtime);
  Runtime loaded;
  storage.loadForkSettings(loaded);
  assert(loaded.controller.speeds[0].speedPercent == 12 &&
         loaded.controller.speeds[0].startSpeedPercent == 4);
  assert(loaded.controller.speeds[1].speedPercent == 35 &&
         loaded.controller.speeds[1].accelerationMs == 1200);
  auto result = api(loaded.controller, loaded.protocol, loaded.loggerSettings, "/api/settings",
                    {{"azSpeedPercent", "1"}, {"elSpeedPercent", "2"}}, 0);
  assert(result.status == 200);
  Hardware hw(loaded);
  hw.beginDirections();
  hw.beginAdc();
  hw.applyOutputs();
  assert(!fakeHardware.readsRaw && !fakeHardware.readsMv && !fakeHardware.attachCalls);
  std::cout
      << "PASS: simulated independent speeds, acceleration/deceleration/reverse, approach, STOP and per-axis NVS/API; no motor output\n";
}
