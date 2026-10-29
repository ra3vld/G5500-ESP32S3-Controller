#include "hardware.hpp"
#include "local_input.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace forkrot;

static Controller readyController() {
  Controller c;c.setupRequired=true;
  for (auto &m : c.motionSettings) m.ready = true;
  c.feedback(100, 60, true, true);
  return c;
}
static void buttons() {
  AxisButtons b;
  assert(b.sample(true, false, 0) == 0); // Held during boot.
  assert(b.sample(true, false, 200) == 0);
  b.sample(false, false, 210); b.sample(false, false, 250);
  assert(b.neutral());
  assert(b.sample(false, true, 260) == 0);
  assert(b.sample(false, false, 270) == 0); // Bounce.
  assert(b.sample(false, true, 280) == 0);
  assert(b.sample(false, true, 319) == 0);
  assert(b.sample(false, true, 320) == 1);
  assert(b.sample(false, false, 321) == 0); // Immediate release.
  assert(b.sample(true, true, 330) == 0);
  assert(b.sample(true, false, 380) == 0); // Conflict remains blocked.
  assert(b.sample(true, false, 480) == 0);
  b.sample(false, false, 490); b.sample(false, false, 530);
  b.sample(true, false, 540);
  assert(b.sample(true, false, 580) == -1);
  AxisButtons wrap;
  wrap.sample(false, false, 0xfffffff0u);
  wrap.sample(false, false, 30);
  wrap.sample(true, false, 40);
  assert(wrap.sample(true, false, 80) == -1);
}
static void joystick() {
  JoystickAxis j(100, 2000, 3900, 200, 50, false);
  assert(j.sample(2000) == 0 && j.neutral(2000));
  assert(j.sample(2249) == 0 && !j.neutral(2249));
  assert(j.sample(2250) > 0);
  assert(j.sample(2220) > 0); // Hysteresis.
  assert(j.sample(2200) == 0);
  assert(j.sample(2220) == 0);
  assert(j.sample(3900) == 1 && j.sample(4095) == 1);
  assert(j.sample(100) == -1 && j.sample(0) == -1);
  assert(std::abs(j.sample(3050) - .5f) < .001f);
  JoystickAxis inverted(0, 2048, 4095, 200, 50, true);
  assert(inverted.sample(4095) == -1 && inverted.sample(0) == 1);
}
static void gestures() {
  auto c = readyController();
  LocalAxisInput input(c, 3);
  input.sample(1, -1, 0, true, 0);
  input.sample(1, -1, 0, true, 100);
  assert(!c.moving());
  input.sample(0, 0, 3, true, 110); input.sample(0, 0, 3, true, 150);
  input.sample(.5f, -1, 0, true, 160);
  assert(c.axes[0].wanted == 1 && c.axes[1].wanted == -1);
  input.sample(0, -1, 1, true, 170);
  assert(!c.axes[0].wanted && c.axes[1].wanted == -1);
  input.sample(0, 0, 3, true, 180);
  assert(!c.axes[0].wanted && !c.axes[1].wanted);
  input.sample(0, 0, 3, true, 220);
  input.sample(-1, 1, 0, true, 230);
  c.stopNow(-1, 240);
  input.sample(-1, 1, 0, true, 250); input.sample(-1, 1, 0, true, 300);
  assert(!c.axes[0].wanted && !c.axes[1].wanted);
  input.sample(0, 0, 3, true, 310); input.sample(0, 0, 3, true, 350);
  input.sample(1, 0, 2, true, 360);
  assert(c.axes[0].wanted == 1);
  assert(c.target({160, 0, true, false}, false, 370) == Result::Ok);
  input.sample(0, 0, 3, true, 380);
  assert(c.axes[0].tracking && c.axes[0].target == 160); // Release cannot stop new target.
  input.sample(1, 0, 2, true, 390);
  assert(c.axes[0].tracking);
  input.sample(0, 0, 3, true, 400); input.sample(0, 0, 3, true, 440);
  input.sample(-1, 0, 2, true, 450);
  assert(!c.axes[0].tracking && c.axes[0].wanted == -1);
  input.sample(-1, 0, 2, true, 750); // Sampling gap: stop, require neutral.
  assert(c.axes[0].wanted == 0);
  input.sample(-1, 0, 2, true, 760);
  assert(c.axes[0].wanted == 0);
  input.sample(0, 0, 3, true, 770); input.sample(0, 0, 3, true, 810);
  input.sample(1, 0, 2, true, 820);
  assert(c.axes[0].wanted == 1);
  c.tick(1820); // Existing manual lease expires if input servicing disappears.
  assert(c.axes[0].wanted == 0);
}
static void protection() {
  for (unsigned scenario = 0; scenario < 5; ++scenario) {
    auto c = readyController(); LocalAxisInput input(c, 3);
    input.sample(0, 0, 3, true, 0); input.sample(0, 0, 3, true, 40);
    if (scenario == 0) c.configurationLocked = true;
    if (scenario == 1) c.driveFault = true;
    if (scenario == 2) c.axes[0].motionError = MotionError::Stalled;
    if (scenario == 3) c.autoTune.phase = AutoTunePhase::Quiet;
    input.sample(1, 0, 2, scenario != 4, 50);
    assert(c.axes[0].wanted == 0);
    c.configurationLocked = c.driveFault = false;
    c.axes[0].motionError = MotionError::None; c.autoTune.phase = AutoTunePhase::Idle;
    input.sample(1, 0, 2, true, 60); assert(c.axes[0].wanted == 0);
    input.sample(0, 0, 3, true, 70); input.sample(0, 0, 3, true, 110);
    input.sample(1, 0, 2, true, 120); assert(c.axes[0].wanted == 1);
  }
  auto c = readyController(); c.setModel(Model::G800DXA);
  LocalAxisInput input(c, 3);
  input.sample(0, 1, 1, true, 0); input.sample(0, 1, 1, true, 40);
  input.sample(-1, 1, 0, true, 50);
  assert(c.axes[0].wanted == -1 && !c.axes[1].wanted);
  c = readyController(); c.feedback(100, 60, false, true);
  c.hardware.drive = DRIVE_BTS7960; c.motionSettings[0].mode = RegulatorMode::Cascade;
  LocalAxisInput uncalibrated(c, 1);
  uncalibrated.sample(0, 0, 1, true, 0); uncalibrated.sample(0, 0, 1, true, 40);
  uncalibrated.sample(1, 0, 0, true, 50); assert(!c.axes[0].wanted);
}
static void movement() {
  auto c = readyController(); c.hardware.simulation = true;
  c.speeds[0] = {80, 20, 10, 100, 100, 10};
  c.speeds[1] = c.speeds[0];
  LocalAxisInput input(c, 3);
  input.sample(0, 0, 3, true, 0); input.sample(0, 0, 3, true, 40);
  for (uint32_t now = 50; now <= 900; now += 10) {
    input.sample(.5f, 1, 0, true, now); c.tick(now);
  }
  assert(std::abs(c.axes[0].speedOutputPercent - 42) < .01f); // Half travel between 5% minimum and working speed.
  assert(std::abs(c.axes[1].speedOutputPercent - 80) < .01f);
  assert(c.axes[0].current > 100 && c.axes[1].current > 60);
  input.sample(0, 0, 3, true, 910); c.tick(910);
  assert(!c.moving());
  input.sample(0, 0, 3, true, 950);
  c.feedback(c.azRange.end, c.elRange.end, true, true);
  input.sample(1, 1, 0, true, 960); c.tick(960);
  assert(!c.moving()); // A held command cannot cross the configured end stops.
  input.sample(-1, -1, 0, true, 970);
  for (uint32_t now = 970; now <= 1350; now += 10) {
    input.sample(-1, -1, 0, true, now); c.tick(now);
  }
  assert(c.axes[0].direction == -1 && c.axes[1].direction == -1);
  assert(c.axes[0].current < c.azRange.end && c.axes[1].current < c.elRange.end);
}
static void runtimePinOwnership() {
  const HardwareConfig original;
  assert(!board::validateHardware(original));
  for (int pin : board::localInputPins) {
    if (pin < 0) continue;
    const auto *owner = board::localPinOwner(pin);
    assert(owner && !board::unavailablePinOwner(pin));
    auto config = original;
    config.pins.uartRx = pin;
    const auto uart = board::validateHardwareDetailed(config);
    assert(uart && std::string(uart.error) == "gpio_conflict");
    config = original;
    config.buttonGpio = pin; config.buttonPullup = false;
    const auto button = board::validateHardwareDetailed(config);
    assert(button && std::string(button.error) == "gpio_conflict");
    if (board::adc(pin)) {
      config = original; config.simulation = true; config.pins.azAdc = pin;
      assert(!board::validateHardware(config)); // Inactive motor roles do not own a wire.
      config.simulation = false;
      const auto motor = board::validateHardwareDetailed(config);
      assert(motor && std::string(motor.error) == "gpio_conflict");
    }
  }
  Runtime recovery; recovery.recovery = true;
  Hardware h(recovery); fakeHardware = {};
  h.beginInputs(); h.serviceLocalInputs();
  assert(fakeHardware.events.empty() && !fakeHardware.readsRaw && !fakeHardware.readsMv);
}
static void gpioAdapter() {
  Runtime r; r.controller = readyController();
  Hardware h(r); fakeHardware = {};
#if LOCAL_INPUT_MODE & LOCAL_INPUT_BUTTONS
  for (int pin : board::localInputPins) if (pin >= 0) fakeHardware.inputOverrides[pin] = HIGH;
#endif
#if LOCAL_INPUT_MODE & LOCAL_INPUT_JOYSTICK
  fakeHardware.millivolts[LOCAL_AZ_ADC_GPIO] = LOCAL_AZ_ADC_CENTER;
  fakeHardware.millivolts[LOCAL_EL_ADC_GPIO] = LOCAL_EL_ADC_CENTER;
#endif
  h.beginInputs();
  auto at = [&](uint32_t now) { fakeHardware.now = now; h.serviceLocalInputs(); };
  for (uint32_t now = 0; now <= 100; now += 10) at(now);
#if LOCAL_INPUT_MODE == LOCAL_INPUT_BUTTONS
  assert(board::localInputPins[0] == LOCAL_AZ_NEG_GPIO);
  fakeHardware.inputOverrides[LOCAL_AZ_POS_GPIO] = LOW;
  fakeHardware.inputOverrides[LOCAL_EL_NEG_GPIO] = LOW;
  for (uint32_t now = 110; now <= 160; now += 10) at(now);
  assert(r.controller.axes[0].wanted == (LOCAL_AZ_INVERT ? -1 : 1));
  assert(r.controller.axes[1].wanted == (LOCAL_EL_INVERT ? 1 : -1));
  fakeHardware.inputOverrides[LOCAL_AZ_POS_GPIO] = HIGH;
  at(170); assert(!r.controller.axes[0].wanted && r.controller.axes[1].wanted);
  fakeHardware.inputOverrides[LOCAL_EL_NEG_GPIO] = HIGH;
  at(180); assert(!r.controller.axes[1].wanted);
  assert(!fakeHardware.readsRaw && !fakeHardware.readsMv);
  auto cfg = r.controller.hardware; cfg.buttonGpio = LOCAL_AZ_NEG_GPIO;
  assert(board::validateHardware(cfg));
#elif LOCAL_INPUT_MODE == LOCAL_INPUT_JOYSTICK
  assert(fakeHardware.readsRaw == 110 && !fakeHardware.readsMv);
  fakeHardware.millivolts[LOCAL_AZ_ADC_GPIO] = LOCAL_AZ_ADC_MAX;
  fakeHardware.millivolts[LOCAL_EL_ADC_GPIO] = LOCAL_EL_ADC_MIN;
  at(110);
  assert(r.controller.axes[0].wanted == (LOCAL_AZ_INVERT ? -1 : 1));
  assert(r.controller.axes[1].wanted == (LOCAL_EL_INVERT ? 1 : -1));
  fakeHardware.millivolts[LOCAL_AZ_ADC_GPIO] = LOCAL_AZ_ADC_CENTER;
  fakeHardware.millivolts[LOCAL_EL_ADC_GPIO] = LOCAL_EL_ADC_CENTER;
  at(120); assert(!r.controller.axes[0].wanted && !r.controller.axes[1].wanted);
  if (SELECTED_BOARD == BOARD_S3) {
    auto cfg = r.controller.hardware; cfg.bluetooth = true; cfg.bluetoothMode = BT_HC05;
    assert(board::validateHardware(cfg)); // Enabling HC-05 cannot steal GPIO6.
  }
#elif LOCAL_INPUT_MODE == LOCAL_INPUT_BOTH
  assert(fakeHardware.readsRaw>0 && !fakeHardware.readsMv);
#else
  assert(!fakeHardware.readsRaw && !fakeHardware.readsMv);
#endif
  Runtime hud; hud.hud.config.role = DeviceRole::Hud;
  Hardware remote(hud); fakeHardware = {}; remote.beginInputs();
  remote.serviceLocalInputs();
  assert(!hud.controller.moving());
#if LOCAL_INPUT_MODE & LOCAL_INPUT_JOYSTICK
  assert(fakeHardware.readsRaw>0); // HUD reads its joystick, never position feedback.
#else
  assert(!fakeHardware.readsRaw);
#endif
}
static void combinedInputs() {
  Runtime r; r.controller=readyController(); auto &c=r.controller.hardware;
  c.simulation=true; c.bluetooth=false; c.buttonGpio=-1; c.local[0]=LOCAL_INPUT_BOTH;
  for(const auto &d:pinDefinitions) c.pins.*d.member=-1;
  bool used[49]{};
  auto pin=[&](bool analog) {
    for(int n=0;n<=48;++n) if(!used[n] && !board::fixedPinOwner(n) && !board::localPinOwner(n) &&
        (analog?board::adc(n):board::pullup(n))) { used[n]=true;return n; }
    assert(false);return -1;
  };
  c.pins.localAzAdc=pin(true); c.pins.localElAdc=pin(true);
  c.pins.localAzNegative=pin(false); c.pins.localAzPositive=pin(false);
  c.pins.localElNegative=pin(false); c.pins.localElPositive=pin(false);
  assert(!board::validateHardware(c) && c.localMask()==3);
  HardwareRecord record;record.good=record.candidate=c;HardwareRecord decoded;
  assert(decodeHardware(encodeHardware(record),decoded) && decoded.good.local[0]==LOCAL_INPUT_BOTH);
  auto bad=c;bad.pins.localAzPositive=bad.pins.localAzAdc;assert(board::validateHardware(bad));
  bad=c;bad.pins.localAzPositive=-1;assert(board::validateHardware(bad));
  fakeHardware={};
  for(int n:{c.pins.localAzNegative,c.pins.localAzPositive,c.pins.localElNegative,c.pins.localElPositive})fakeHardware.inputOverrides[n]=HIGH;
  fakeHardware.millivolts[c.pins.localAzAdc]=c.local[4];fakeHardware.millivolts[c.pins.localElAdc]=c.local[7];
  Hardware h(r);h.beginInputs();
  auto sample=[&](uint32_t now){fakeHardware.now=now;h.serviceLocalInputs();};
  sample(0);sample(40);sample(80);
  fakeHardware.inputOverrides[c.pins.localAzPositive]=LOW;sample(90);sample(130);
  assert(r.controller.axes[0].wanted==(c.local[1]?-1:1)); // Neutral joystick does not cancel button.
  fakeHardware.millivolts[c.pins.localElAdc]=4095;sample(140);
  assert(r.controller.axes[0].wanted && r.controller.axes[1].wanted); // Independent devices/axes.
  fakeHardware.millivolts[c.pins.localAzAdc]=0;sample(150);
  assert(!r.controller.axes[0].wanted); // Opposing devices stop this axis.
  fakeHardware.inputOverrides[c.pins.localAzPositive]=HIGH;sample(160);sample(200);
  assert(!r.controller.axes[0].wanted); // Releasing just one device cannot reverse motion.
  fakeHardware.millivolts[c.pins.localAzAdc]=c.local[4];fakeHardware.millivolts[c.pins.localElAdc]=c.local[7];
  sample(210);sample(250);sample(290);
  fakeHardware.millivolts[c.pins.localAzAdc]=4095;sample(300);
  assert(r.controller.axes[0].wanted==(c.local[1]?-1:1));
  assert(h.startJoystickCalibration());assert(r.controller.configurationLocked && !r.controller.axes[0].wanted);
  fakeHardware.inputOverrides[c.pins.localAzPositive]=LOW;sample(310);sample(350);
  assert(!r.controller.axes[0].wanted && h.calibratingJoystick());
  h.cancelJoystickCalibration();sample(360);sample(400);assert(!r.controller.axes[0].wanted);
}
int main() {
  buttons(); joystick(); combinedInputs(); gestures(); protection(); movement(); runtimePinOwnership(); gpioAdapter();
  std::cout << "PASS: local controls debounce, proportional ADC, simultaneous axes, ownership, STOP, lease, faults and GPIO adapter\n";
}
