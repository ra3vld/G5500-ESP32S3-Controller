#include "hardware.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
namespace forkrot::crowpanel { bool beginBus() { return true; } }
using namespace forkrot;
static void close(float a, float b) { assert(std::abs(a - b) < .002f); }
int main() {
  static_assert(DRIVE_SIMULATION == 1);
  Runtime runtime;
  Hardware hw(runtime);
  auto &c = runtime.controller;
  hw.beginDirections(); hw.beginAdc(); hw.beginInputs(); hw.allStop();
  assert(c.axes[0].calibrated && c.axes[1].calibrated && !c.driveFault);
  assert(!c.variableSpeed(0) && !c.variableSpeed(1));
  assert(stateJson(c, runtime.protocol, runtime.loggerSettings, 0).find("\"drive\":\"SIMULATED\"") != std::string::npos);
  fakeHardware.now = 100;
  c.tick(100);
  assert(c.target({134, 67, true, true}, false, 100) == Result::Ok);
  for (unsigned now = 100; now <= 10200; now += 100) {
    fakeHardware.now = now; hw.controlRotator();
    if (now == 1100) { close(c.axes[0].current, 13.4f); close(c.axes[1].current, 13.4f); }
  }
  close(c.axes[0].current, 134); close(c.axes[1].current, 67); assert(!c.moving());
  assert(c.target({250, 100, true, true}, true, 10200) == Result::Pending);
  fakeHardware.now = 11100; hw.controlRotator(); close(c.axes[0].current, 134);
  c.setMode(Mode::Auto, 11100); hw.controlRotator();
  fakeHardware.now = 12100; hw.controlRotator(); close(c.axes[0].current, 147.4f);
  hw.allStop(); fakeHardware.now += 2000; hw.controlRotator(); close(c.axes[0].current, 147.4f);
  // Exercise every motor-facing entry point, including jog and ADC diagnostics.
  for (char direction : {'R', 'L', 'U', 'D', 'S', 'A', 'E'}) {
    hw.moveDir(direction); fakeHardware.now += 500; hw.controlRotator(); hw.allStop();
  }
  for (int pin : {board::PIN_AZ_ADC, board::PIN_EL_ADC}) {
    int raw = -1, mv = -1; hw.adcDiagRead(pin, raw, mv); assert(!raw && !mv);
    assert(hw.adcAvgMv(pin) == 0);
  }
  assert(hw.filteredMv(0) == 0 && hw.filteredMv(1) == 0);
  assert(!fakeHardware.readsRaw && !fakeHardware.readsMv && !fakeHardware.attachCalls);
  for (const auto &event : fakeHardware.events)
    assert(event.pin >= 0 && (event.pin == board::PIN_CONFIRM || event.pin == board::PIN_LED));
  // A HUD still takes remote feedback; the build flag must not animate it locally.
  c.feedback(20, 30, true, true); c.remote = &runtime.hud; c.axes[0].direction = 1;
  c.tick(fakeHardware.now + 1000); close(c.axes[0].current, 20);
  std::printf("PASS: simulation board=%d drive=%d; motion/STOP/remote and no motor GPIO/PWM/ADC\n", SELECTED_BOARD, DRIVE_INTERFACE);
}
