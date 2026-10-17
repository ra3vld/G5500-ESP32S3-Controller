#include "api.hpp"
#include "storage.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace forkrot;

static bool close(float a, float b) { return std::abs(a - b) < 0.001f; }
static Controller ready() {
  Controller c;
  c.model = Model::G800DXA;
  c.speedEnabled = true;
  c.feedback(20, 30, true, true);
  c.speeds[0] = {100, 20, 10, 1000, 1000, 10, 2.5f, 1};
  return c;
}
static void cutoff() {
  for (int dir : {-1, 1}) {
    auto c = ready();
    const float target = dir > 0 ? 90 : 10;
    c.target({target, 0, true, false}, false, 0);
    c.tick(0);
    c.feedback(target - dir * 2.6f, 0, true, false);
    c.tick(0);
    assert(c.axes[0].direction == dir && c.axes[0].tracking);
    c.feedback(target - dir * 2.5f, 0, true, false);
    c.tick(0);
    assert(!c.moving() && c.axes[0].completed && c.axes[0].speedOutputPercent == 0);
    c.feedback(target - dir * 3.5f, 0, true, false);
    c.tick(0);
    assert(!c.moving()); // No restart when feedback drifts outside the cutoff.
  }
  auto c = ready();
  c.model = Model::G5500;
  c.feedback(20, 30, true, true);
  c.speeds[1].coastDegrees = 1.5f;
  c.target({90, 60, true, true}, false, 0);
  c.tick(0);
  c.feedback(87.5f, 58.4f, true, true);
  c.tick(0);
  assert(c.axes[0].completed && c.axes[1].tracking);
  c.feedback(87.5f, 58.5f, true, true);
  c.tick(0);
  assert(!c.moving() && c.axes[1].completed);
  c.speeds[0].coastDegrees = 0;
  c.feedback(89.9f, 60, true, true);
  c.target({90, 0, true, false}, false, 0);
  assert(c.axes[0].tracking);
  c.feedback(90.1f, 60, true, true);
  c.tick(0);
  assert(c.axes[0].completed && !c.moving());
  c.maximum = 10;
  c.speeds[0].coastDegrees = 90;
  assert(close(c.tolerance(0), 1));
}
static void ramp() {
  for (float factor : {0.5f, 1.f, 2.f}) {
    auto c = ready();
    c.speeds[0].decelerationFactor = factor;
    c.jog(0, 1, false, 0);
    c.tick(0);
    c.tick(1000);
    assert(close(c.axes[0].speedOutputPercent, 100)); // Acceleration is independent.
    c.target({90, 0, true, false}, false, 1000);
    c.feedback(84, 0, true, false);
    c.tick(1100);
    assert(close(c.axes[0].speedOutputPercent, 100 - factor * 10));
    c.stopNow(-1, 1100);
    assert(!c.moving() && c.axes[0].speedOutputPercent == 0);

    c = ready();
    c.speeds[0].decelerationFactor = factor;
    c.jog(0, 1, false, 0); c.tick(0); c.tick(1000);
    c.jog(0, -1, false, 1000); c.tick(1100);
    assert(c.axes[0].direction == 1 && close(c.axes[0].speedOutputPercent, 100 - factor * 10));
    const uint32_t stoppedAt = 1000 + uint32_t(1000 / factor);
    c.tick(stoppedAt);
    assert(c.axes[0].direction == 0 && c.axes[0].speedOutputPercent == 0);
    c.tick(stoppedAt + 299); assert(c.axes[0].direction == 0);
    c.tick(stoppedAt + 300); assert(c.axes[0].direction == -1);
  }
}
static void settings() {
  auto c = ready(); Protocol p(c); Settings cfg;
  assert(api(c,p,cfg,"/api/settings",{{"coastDegrees","2.5"},{"azCoastDegrees","2.5"},
      {"decelerationFactor","0.5"},{"elCoastDegrees","1.2"},{"elDecelerationFactor","2"}},0).status == 200);
  assert(close(c.speeds[0].coastDegrees,2.5f) && close(c.speeds[0].decelerationFactor,.5f));
  assert(close(c.speeds[1].coastDegrees,1.2f) && close(c.speeds[1].decelerationFactor,2));
  const auto before = stateJson(c,p,cfg,0);
  for (const auto &bad : {Form{{"coastDegrees","-0.1"}}, Form{{"elCoastDegrees","90.1"}},
       Form{{"coastDegrees","nan"}}, Form{{"decelerationFactor","inf"}},
       Form{{"decelerationFactor","0"}}, Form{{"elDecelerationFactor","10.1"}},
       Form{{"decelerationFactor","0.09"}}, Form{{"coastDegrees","2"},{"azCoastDegrees","3"}},
       Form{{"decelerationFactor","1"},{"azDecelerationFactor","2"}}}) {
    Form form = bad; form["elSpeedPercent"] = "67";
    assert(api(c,p,cfg,"/api/settings",form,0).status == 400);
    assert(stateJson(c,p,cfg,0) == before);
  }
  assert(before.find("\"azCoastDegrees\":2.5") != std::string::npos);
  assert(before.find("\"elDecelerationFactor\":2") != std::string::npos);
  for (const auto &edge : {Form{{"coastDegrees","0"},{"decelerationFactor","0.1"}},
                          Form{{"coastDegrees","90"},{"decelerationFactor","10"}}})
    assert(api(c,p,cfg,"/api/settings",edge,0).status == 200);
}
static void persistence() {
  Preferences::database.clear();
  Storage storage; Runtime saved;
  saved.controller.speeds[0].coastDegrees = 2.5f;
  saved.controller.speeds[0].decelerationFactor = .5f;
  saved.controller.speeds[1].coastDegrees = 1.2f;
  saved.controller.speeds[1].decelerationFactor = 2;
  storage.saveForkSettings(saved);
  Runtime loaded; storage.loadForkSettings(loaded);
  for (unsigned i=0;i<2;++i) {
    assert(close(saved.controller.speeds[i].coastDegrees, loaded.controller.speeds[i].coastDegrees));
    assert(close(saved.controller.speeds[i].decelerationFactor, loaded.controller.speeds[i].decelerationFactor));
  }
  assert(!loaded.controller.moving() && !loaded.controller.pendingValid);
#if DRIVE_INTERFACE == DRIVE_YAESU && !DRIVE_SIMULATION
  const char *space = "forkcfg";
#else
  const char *space = "forkpwm";
#endif
  Preferences::database[space]["azcoast"] = std::numeric_limits<float>::quiet_NaN();
  Preferences::database[space]["azdecelf"] = -1.f;
  Runtime corrupted; storage.loadForkSettings(corrupted);
  Controller defaults;
  assert(corrupted.controller.speeds[0].coastDegrees == defaults.speeds[0].coastDegrees);
  assert(corrupted.controller.speeds[0].decelerationFactor == 1);
}
int main() {
  cutoff(); ramp(); settings(); persistence();
  std::cout << "PASS: coast cutoff both directions/axes, zero/crossing, ramp factors, STOP/reverse, API atomicity and NVS\n";
}
