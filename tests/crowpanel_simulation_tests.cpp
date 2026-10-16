#include "hardware.hpp"
#include "crowpanel_knob.hpp"
#include "storage.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
namespace forkrot::crowpanel { bool beginBus() { return true; } }
using namespace forkrot;
static void close(float value, float expected) { assert(std::abs(value - expected) < 0.002f); }
int main() {
  Runtime runtime;
  Hardware hw(runtime);
  hw.beginDirections(); hw.beginAdc(); hw.beginInputs();
  auto &c = runtime.controller;
  assert(!c.driveFault && c.axes[0].calibrated && c.axes[1].calibrated);
  c.tick(100);
  assert(c.target({134, 67, true, true}, false, 100) == Result::Ok);
  c.tick(100);
  for (uint32_t now = 200; now <= 1100; now += 100) c.tick(now);
  close(c.axes[0].current, 13.4f); close(c.axes[1].current, 13.4f);
  c.tick(5100);
  close(c.axes[0].current, 67); close(c.axes[1].current, 67);
  assert(!c.axes[1].tracking);
  c.tick(10100);
  close(c.axes[0].current, 134); assert(!c.moving());
  // STOP holds position even though the old target remains known.
  c.target({250, 0, true, false}, false, 10100); c.tick(10100); c.tick(11100);
  c.stopNow(-1, 11100); const float stopped = c.axes[0].current;
  c.tick(21100); close(c.axes[0].current, stopped);
  // Manual remote target waits; the same target in auto starts simulated motion.
  assert(c.target({250, 0, true, false}, true, 21100) == Result::Pending);
  c.tick(22100); close(c.axes[0].current, stopped);
  c.setMode(Mode::Auto, 22100); c.tick(22100); c.tick(23100);
  close(c.axes[0].current, stopped + 13.4f);
  // A changed target behind the axis must not teleport it across the old direction.
  const float beforeReverse = c.axes[0].current;
  c.target({0, 0, true, false}, false, 23100); c.tick(23100);
  close(c.axes[0].current, beforeReverse);
  c.tick(23400); c.tick(24400); close(c.axes[0].current, beforeReverse - 13.4f);
  c.tick(100000); close(c.axes[0].current, 0); assert(!c.moving());
  // Jog obeys mechanical bounds and millis wraparound; no GPIO/ADC calls.
  Controller wrap;
  wrap.tick(0xfffffff0u); wrap.jog(0, 1, false, 0xfffffff0u); wrap.tick(0xfffffff0u);
  wrap.tick(uint32_t(0xfffffff0u + 1000u)); close(wrap.axes[0].current, 13.4f);
  wrap.tick(uint32_t(0xfffffff0u + 200000u)); close(wrap.axes[0].current, 450);
  assert(!wrap.moving());
  // Regression: several detents without any press must reach the LAST target.
  for (const auto mode : {Mode::Manual, Mode::Auto}) {
    for (unsigned axis = 0; axis < 2; ++axis) {
      for (bool reverse : {false, true}) {
        Controller following;
        following.feedback(reverse ? 140.f : 0.f, reverse ? 140.f : 0.f, true, true);
        following.setMode(mode, 100);
        CrowPanelKnob encoder(following);
        if (axis) encoder.selectAxis();
        following.tick(100);
        uint32_t now = 100;
        for (int step = 1; step <= 5; ++step) {
          now += 55;
          encoder.sample(true, false, reverse ? 1 : -1, now);
          following.tick(now);
          close(following.axes[axis].target, reverse ? 140.f - 20.f * step : 20.f * step);
        }
        const float lastTarget = following.axes[axis].target;
        for (int i = 0; i < 200; ++i) {
          now += 55; encoder.sample(true, false, 0, now); following.tick(now);
        }
        close(following.axes[axis].current, lastTarget);
        assert(!following.moving());
      }
    }
  }
  // Knob pause holds both axes; the next single click reaches the original targets.
  for (const auto mode : {Mode::Manual, Mode::Auto}) {
    Controller paused; CrowPanelKnob pauseKnob(paused);
    uint32_t now = 100;
    auto sample = [&](bool down, uint32_t elapsed = 40) {
      now += elapsed; paused.tick(now); pauseKnob.sample(true, down, 0, now); paused.tick(now);
    };
    auto press = [&] { sample(true); sample(true); };
    auto release = [&] { sample(false); sample(false); };
    sample(false); paused.setMode(mode, now);
    paused.target({100, 50, true, true}, false, now); paused.tick(now);
    sample(false, 1000); press();
    const float az = paused.axes[0].current, el = paused.axes[1].current;
    release(); sample(false, 2000);
    close(paused.axes[0].current, az); close(paused.axes[1].current, el);
    assert(!paused.moving());
    press(); release(); sample(false, 351);
    assert(paused.moving() && paused.axes[0].target == 100 && paused.axes[1].target == 50);
    for (int i = 0; i < 100; ++i) sample(false, 100);
    close(paused.axes[0].current, 100); close(paused.axes[1].current, 50);
    assert(!paused.moving());
  }
  // Actual CrowPanel settings: one detent starts motion in manual without a click.
  Controller immediate;
  CrowPanelKnob knob(immediate);
  immediate.tick(100);
  knob.sample(true, false, -1, 100);
  immediate.tick(100); immediate.tick(1100);
  close(immediate.axes[0].current, 13.4f);
  assert(immediate.mode == Mode::Manual && immediate.axes[0].target == 20);
  knob.sample(true, false, -1, 1100);
  assert(immediate.axes[0].target == 40);
  knob.sample(true, true, 0, 1100);
  knob.sample(true, true, 0, 1140);
  assert(immediate.axes[0].target == 40 && !immediate.moving());
  knob.sample(true, false, 0, 1140);
  knob.sample(true, false, 0, 1180);
  immediate.tick(2100); close(immediate.axes[0].current, 13.4f);
  knob.sample(true, true, 0, 2100);
  knob.sample(true, true, 0, 2140);
  knob.sample(true, false, 0, 2180);
  knob.sample(true, false, 0, 2220);
  knob.sample(true, false, 0, 2571);
  immediate.tick(2571);
  assert(immediate.moving() && immediate.axes[0].target == 40);
  immediate.tick(3571); close(immediate.axes[0].current, 26.8f);
  immediate.tick(4571); close(immediate.axes[0].current, 40);
  assert(!immediate.moving());
  knob.sample(true, false, -1, 4611);
  close(immediate.axes[0].target, 60);
  // Runtime setting: default-on, strict API boolean, persisted off/on across reload.
  Preferences::database.clear();
  Storage storage;
  storage.loadForkSettings(runtime);
  assert(runtime.loggerSettings.encoderInverted);
  auto setting = [&](const char *value) {
    return api(c, runtime.protocol, runtime.loggerSettings, "/api/settings", {{"encoderInverted", value}}, 0);
  };
  assert(setting("false").status == 200 && !runtime.loggerSettings.encoderInverted);
  assert(setting("invalid").status == 400 && !runtime.loggerSettings.encoderInverted);
  storage.saveForkSettings(runtime);
  assert(!std::get<bool>(Preferences::database["forkcfg"]["encinv"]));
  Runtime loaded; storage.loadForkSettings(loaded);
  assert(!loaded.loggerSettings.encoderInverted);
  assert(setting("true").status == 200);
  storage.saveForkSettings(runtime); storage.loadForkSettings(loaded);
  assert(loaded.loggerSettings.encoderInverted);
  assert(stateJson(c, runtime.protocol, runtime.loggerSettings, 0).find("\"encoderInverted\":true") != std::string::npos);
  hw.applyOutputs(); hw.allStop(); hw.filteredMv(0); hw.filteredMv(1);
  assert(fakeHardware.events.empty() && fakeHardware.readsRaw == 0 && fakeHardware.readsMv == 0);
  std::puts("PASS: simulated 13.4 deg/s, exact arrival, STOP, manual/auto, reverse, limits, millis wrap; no GPIO/ADC");
}
