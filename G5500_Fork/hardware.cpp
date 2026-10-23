// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "hardware.hpp"
#include <cmath>
#include <driver/gpio.h>
#include "crowpanel_board.hpp"
namespace forkrot {
using namespace board;
namespace {
// Arduino 3.x digitalWrite requires an existing GPIO owner. Set the GPIO latch
// directly before enabling output, and independently of LEDC ownership.
[[maybe_unused]] void preloadLevel(int pin, int level) { gpio_set_level(static_cast<gpio_num_t>(pin), level); }
}

uint32_t Hardware::motorPwmDuty(uint32_t duty) const {
  const auto &c = runtime_.controller.hardware;
  return c.drive == DRIVE_BTS7960 && c.pwmInverted ? 1023u-duty : duty;
}
int Hardware::idleOutputLevel(int pin) const {
  const auto &p=runtime_.controller.hardware.pins;
  const auto &c = runtime_.controller.hardware;
  return c.drive == DRIVE_BTS7960 && c.pwmInverted &&
      (pin == p.azNegative || pin == p.azPositive || pin == p.elPositive || pin == p.elNegative) ? HIGH : LOW;
}
int Hardware::adcAvgMv(int pin) {
  if (runtime_.hud.isHud() || runtime_.recovery)
    return 0;
  if (runtime_.controller.hardware.simulation || !board::adc(pin)) {
  (void)pin;
  return 0;
  } else {
  uint32_t s = 0;
  for (unsigned i = 0; i < MotionAdcSamples; i++) {
    // Spread conversions across the dual-PWM profile's 1 kHz period. A burst of
    // adjacent reads aliases switching interference into a false shaft jump.
    // Four spaced reads leave time for both axes in the 5 ms control period.
    if(i) delayMicroseconds(200);
    s += analogReadMilliVolts(pin);
  }
  return s / MotionAdcSamples;
  }
}

int Hardware::filteredMv(unsigned axis) {
  const auto &p=runtime_.controller.hardware.pins;
  if (runtime_.hud.isHud() || runtime_.recovery)
    return 0;
  auto &filter = filters_[axis];
  const uint32_t now = millis();
  if (!filter.due(now,runtime_.controller.controlPeriodMs())) return filter.value();
  const int mv = adcAvgMv(axis ? p.elAdc : p.azAdc);
  return filter.sample(mv, millis(),runtime_.controller.controlPeriodMs()); // timestamp the completed conversion
}
bool Hardware::buttonReleased() const {
  if (runtime_.controller.hardware.buttonGpio < 0) return true;
  return digitalRead(runtime_.controller.hardware.buttonGpio) == (runtime_.controller.hardware.buttonLow ? HIGH : LOW);
}
void Hardware::adcDiagRead(int pin, int &raw, int &mv) {
  if (runtime_.hud.isHud() || runtime_.recovery) {
    raw = mv = 0;
    return;
  }
  if (runtime_.controller.hardware.simulation || !board::adc(pin)) {
  mv = adcAvgMv(pin);
  raw = 0;
  } else {
  uint32_t sr = 0, sm = 0;
  for (int i = 0; i < 25; i++) {
    sr += analogRead(pin);
    sm += analogReadMilliVolts(pin);
    delayMicroseconds(150);
  }
  raw = (int)(sr / 25);
  mv = (int)(sm / 25);
  }
}

void Hardware::rgbSet(uint8_t r, uint8_t g, uint8_t b) {
  const auto &p=runtime_.controller.hardware.pins;
  if(runtime_.recovery) return;
  if (p.led >= 0) digitalWrite(p.led, (r || g || b) ? HIGH : LOW);
  if (p.rgb >= 0) {
    rgb_.write(p.rgb, r, g, b);
  }
}

void Hardware::rgbStart(RgbState s) {
  rgbState = s;
  if (s == RGB_RX) {
    rgbSet(0, RGB_LEVEL, 0);
    rgbUntilMs = millis() + RGB_FLASH_MS;
  } else if (s == RGB_TX) {
    rgbSet(RGB_LEVEL, 0, 0);
    rgbUntilMs = millis() + RGB_FLASH_MS;
  } else if (s == RGB_HEARTBEAT) {
    rgbSet(0, 0, RGB_LEVEL);
    rgbUntilMs = millis() + RGB_HEARTBEAT_ON_MS;
  } else {
    rgbSet(0, 0, 0);
    rgbUntilMs = 0;
  }
}

void Hardware::flashRx() {
  // RX gets shown first. A TX response generated immediately afterwards is queued.
  rgbStart(RGB_RX);
}

void Hardware::flashTx() {
  if (rgbState == RGB_RX && (int32_t)(rgbUntilMs - millis()) > 0) {
    pendingTxFlash = true;
  } else {
    rgbStart(RGB_TX);
  }
}

void Hardware::updateRgb() {
  uint32_t now = millis();
  if (rgbState != RGB_IDLE && (int32_t)(now - rgbUntilMs) >= 0) {
    rgbSet(0, 0, 0);
    rgbState = RGB_IDLE;
    if (pendingTxFlash) {
      pendingTxFlash = false;
      rgbStart(RGB_TX);
      return;
    }
  }
  if (rgbState == RGB_IDLE && now - lastHeartbeatMs >= RGB_HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    rgbStart(RGB_HEARTBEAT);
  }
}

void Hardware::stopAzimuth() {
  runtime_.controller.stop(0);
  runtime_.controller.tick(millis());
  applyOutputs();
}

void Hardware::allStop() {
  runtime_.controller.stop();
  runtime_.controller.tick(millis());
  applyOutputs();
}

void Hardware::moveDir(char d) {
  if (d == 'S') {
    runtime_.controller.stop();
  } else if (d == 'A' || d == 'E') {
    runtime_.controller.stop(d == 'A' ? 0 : 1);
  } else if (d == 'L' || d == 'R' || d == 'U' || d == 'D') {
    runtime_.controller.jog(d == 'U' || d == 'D' ? 1 : 0, d == 'R' || d == 'U' ? 1 : -1, false,
                            millis());
  }
  runtime_.controller.tick(millis());
  applyOutputs();
}

String Hardware::motion() {
  bool l = runtime_.controller.axes[0].direction < 0;
  bool r = runtime_.controller.axes[0].direction > 0;
  bool u = runtime_.controller.axes[1].direction > 0;
  bool d = runtime_.controller.axes[1].direction < 0;

  if (u && r) {
    return "UP+RIGHT";
  }
  if (u && l) {
    return "UP+LEFT";
  }
  if (d && r) {
    return "DOWN+RIGHT";
  }
  if (d && l) {
    return "DOWN+LEFT";
  }
  if (l) {
    return "LEFT";
  }
  if (r) {
    return "RIGHT";
  }
  if (u) {
    return "UP";
  }
  if (d) {
    return "DOWN";
  }
  return "STOP";
}

void Hardware::controlRotator() {
  runtime_.controller.tick(millis());
  applyOutputs();
}

void Hardware::outputsOff() {
  const auto &p=runtime_.controller.hardware.pins;
  const auto &c = runtime_.controller.hardware;
#if CROWPANEL_LOCAL_DRIVE
  if (directionsStarted_ && !c.simulation && !runtime_.hud.isHud() && !crowpanelDrive_.stop()) crowpanelFault();
  return;
#endif
  if (!directionsStarted_ || c.simulation || runtime_.hud.isHud()) return;
  if (c.drive == DRIVE_BTS7960) {
    preloadLevel(p.azSpeedEnable, LOW);
    if(!c.pins.singleAxis()) preloadLevel(p.elSpeedEnable, LOW);
  }
  const int pins[] = {p.azNegative, p.azPositive, p.elNegative, p.elPositive, p.azSpeedEnable, c.drive != DRIVE_YAESU ? p.elSpeedEnable : -1};
  const int pwm[] = {c.drive == DRIVE_BTS7960 ? p.azNegative : p.azSpeedEnable,
                    c.drive == DRIVE_BTS7960 ? p.azPositive : p.elSpeedEnable,
                    c.drive == DRIVE_BTS7960 ? p.elNegative : -1,
                    c.drive == DRIVE_BTS7960 ? p.elPositive : -1};
  for (unsigned i=0; i<4; ++i) if (attached_[i]) ledcWrite(pwm[i], motorPwmDuty(0));
  for (int pin : pins) if (pin >= 0) preloadLevel(pin, idleOutputLevel(pin));
}
bool Hardware::preparePwm(bool enable) {
  const auto &p=runtime_.controller.hardware.pins;
  const auto &c = runtime_.controller.hardware;
  if (runtime_.hud.isHud() || runtime_.controller.configurationLocked || c.simulation) return false;
  const int pins[] = {c.drive == DRIVE_BTS7960 ? p.azNegative : p.azSpeedEnable,
                      c.drive == DRIVE_BTS7960 ? p.azPositive : p.elSpeedEnable,
                      c.drive == DRIVE_BTS7960 ? p.elNegative : -1,
                      c.drive == DRIVE_BTS7960 ? p.elPositive : -1};
  if (pwmFailed) return false;
  if (enable && !pwmReady) {
    outputsOff();
    const unsigned count = c.drive == DRIVE_BTS7960 ? (c.pins.singleAxis()?2:4) : c.drive == DRIVE_SEPARATE_PWM ? (c.pins.singleAxis()?1:2) : 1;
    for (unsigned i=0; i<count; ++i) {
      attached_[i] = ledcAttach(pins[i], c.drive == DRIVE_YAESU ? 20000 : 1000, 10);
      if (!attached_[i]) {
        outputsOff();
        for (unsigned j=0; j<=i; ++j) {
          if (attached_[j]) ledcDetach(pins[j]);
          attached_[j] = false;
          preloadLevel(pins[j], idleOutputLevel(pins[j])); pinMode(pins[j], OUTPUT);
        }
        pwmFailed = true;
        runtime_.controller.stop(); runtime_.controller.driveFault = true;
        runtime_.controller.tick(millis()); runtime_.controller.fault = "pwm_attach_failed";
        return false;
      }
      ledcWrite(pins[i], motorPwmDuty(0));
    }
    pwmReady = true;
  }
  if (!enable && pwmReady) {
    outputsOff();
    for (unsigned i=0; i<4; ++i) if (attached_[i]) {
      ledcDetach(pins[i]); attached_[i] = false;
      preloadLevel(pins[i], idleOutputLevel(pins[i])); pinMode(pins[i], OUTPUT);
    }
    pwmReady = false;
  }
  return true;
}
void Hardware::applyOutputs() {
  if (runtime_.controller.configurationLocked) { outputsOff(); return; }
  if (runtime_.hud.isHud()) return;
  const auto &c = runtime_.controller.hardware;
  if (c.simulation) {
    // Both runtime implementations are compiled; the virtual path never owns motor pins.
  }
#if CROWPANEL_LOCAL_DRIVE
  else {
    auto &controller = runtime_.controller;
    if (controller.model != Model::G800DXA || !directionsStarted_) {
      if (directionsStarted_) crowpanelDrive_.stop();
      crowpanelFault();
    } else if (!crowpanelDrive_.apply(controller.driveFault ? 0 : controller.axes[0].direction,
          uint32_t(std::clamp(lroundf(controller.axes[0].speedOutputPercent*1023.f/100.f), 0l, 1023l)),
          controller.variableSpeed(0))) crowpanelFault();
  }
#else
  else {
    const auto &p=c.pins;
    if(!directionsStarted_) return;
    const bool ready = preparePwm(c.drive != DRIVE_YAESU || runtime_.controller.variableSpeed(0));
    const int neg[2] = {p.azNegative, p.elNegative}, pos[2] = {p.azPositive, p.elPositive}, speed[2] = {p.azSpeedEnable, p.elSpeedEnable};
    if (c.drive == DRIVE_BTS7960) {
      for (unsigned i=0; i<(c.pins.singleAxis()?1u:2u); ++i)
        if (!ready || !runtime_.controller.axes[i].direction || runtime_.controller.axes[i].speedOutputPercent <= 0)
          digitalWrite(speed[i], LOW);
    }
    for (unsigned i=0; i<(c.pins.singleAxis()?1u:2u); ++i) {
      const auto &axis = runtime_.controller.axes[i];
      const int d = ready ? axis.direction : 0;
      const uint32_t duty = d ? uint32_t(lroundf(axis.speedOutputPercent*1023.f/100.f)) : 0;
      if (c.drive == DRIVE_BTS7960) {
        if (!d || !duty) digitalWrite(speed[i], LOW);
        if (ready) {
          ledcWrite(d > 0 ? neg[i] : pos[i], motorPwmDuty(0));
          ledcWrite(d > 0 ? pos[i] : neg[i], motorPwmDuty(duty));
        }
        digitalWrite(speed[i], d && duty ? HIGH : LOW);
      } else {
        if (d <= 0) digitalWrite(pos[i], LOW);
        if (d >= 0) digitalWrite(neg[i], LOW);
        if (d < 0) digitalWrite(neg[i], HIGH);
        if (d > 0) digitalWrite(pos[i], HIGH);
        if (pwmReady && (c.drive == DRIVE_SEPARATE_PWM || i == 0)) ledcWrite(speed[i], duty);
      }
    }
  }
#endif
  runtime_.targetAz = runtime_.controller.axes[0].target; runtime_.targetEl = runtime_.controller.axes[1].target;
  runtime_.targetValid = runtime_.controller.targetKnown;
  runtime_.azTrack = runtime_.controller.axes[0].tracking; runtime_.elTrack = runtime_.controller.axes[1].tracking;
}
void Hardware::beginDirections() {
  const auto &c=runtime_.controller.hardware;
  if(runtime_.recovery) return;
#if SELECTED_BOARD == BOARD_CROWPANEL_21
  crowpanel::beginBus();
#endif
  if(c.pins.led>=0) { preloadLevel(c.pins.led,LOW); pinMode(c.pins.led,OUTPUT); }
  if(runtime_.hud.isHud() || c.simulation) return;
  if(board::validateHardware(c)) {
    runtime_.controller.driveFault=true; runtime_.controller.fault="pin_configuration_invalid"; return;
  }
#if CROWPANEL_LOCAL_DRIVE
  directionsStarted_=true;
  if(!crowpanelDrive_.begin(c)) crowpanelFault();
#else
  if(c.drive==DRIVE_BTS7960) {
    preloadLevel(c.pins.azSpeedEnable,LOW); pinMode(c.pins.azSpeedEnable,OUTPUT);
    if(!c.pins.singleAxis()) { preloadLevel(c.pins.elSpeedEnable,LOW); pinMode(c.pins.elSpeedEnable,OUTPUT); }
  }
  const int motorPins[]={c.pins.azNegative,c.pins.azPositive,c.pins.elPositive,c.pins.elNegative,
      c.pins.azSpeedEnable,c.drive!=DRIVE_YAESU?c.pins.elSpeedEnable:-1};
  for(int pin:motorPins) if(pin>=0) { preloadLevel(pin,idleOutputLevel(pin)); pinMode(pin,OUTPUT); }
  directionsStarted_=true;
#endif
}
void Hardware::beginAdc() {
  const auto &p=runtime_.controller.hardware.pins;
  if (runtime_.hud.isHud() || runtime_.recovery || runtime_.controller.hardware.simulation || board::validateHardware(runtime_.controller.hardware)) return;
  analogReadResolution(12);
  // Arduino 3.3 configures the ADC channel on its first read, before per-pin attenuation.
  analogRead(p.azAdc); analogSetPinAttenuation(p.azAdc, ADC_11db);
  if (board::adc(p.elAdc)) { analogRead(p.elAdc); analogSetPinAttenuation(p.elAdc, ADC_11db); }
}
#if CROWPANEL_LOCAL_DRIVE
void Hardware::crowpanelFault() {
  auto &controller = runtime_.controller;
  if (!controller.driveFault || controller.moving() || controller.axes[0].wanted || controller.axes[1].wanted)
    controller.stopNow(-1, millis());
  controller.driveFault = true;
  controller.fault = "crowpanel_drive_failed";
}
#endif
void Hardware::beginInputs() {
  const auto &c = runtime_.controller.hardware;
  if (!runtime_.recovery && c.buttonGpio >= 0) pinMode(c.buttonGpio, c.buttonPullup ? INPUT_PULLUP : INPUT);
  if (runtime_.recovery || board::validateHardware(c, runtime_.hud.config.uart)) return;
  localInput_.configure(c.localMask());
  for(const auto &d:pinDefinitions) if((d.group==PinGroup::Buttons || d.group==PinGroup::Joystick) && board::pinActive(c,d)) {
    const int pin=c.pins.*d.member;
    if(pin<0) continue;
    pinMode(pin, d.group==PinGroup::Buttons?INPUT_PULLUP:INPUT);
    if(d.group==PinGroup::Joystick) { analogReadResolution(12); analogSetPinAttenuation(pin,ADC_11db); }
  }
  for(unsigned i=0;i<2;++i) {
    const auto at=3+i*3;
    localJoystick_[i]=JoystickAxis(c.local[at],c.local[at+1],c.local[at+2],c.local[9],c.local[10],c.local[1+i]);
  }
  localInputsStarted_ = c.localMask()!=0;
}
bool Hardware::startJoystickCalibration() {
  if(!localInputsStarted_ || runtime_.controller.hardware.local[0]!=LOCAL_INPUT_JOYSTICK ||
      runtime_.controller.configurationLocked || runtime_.controller.autoTune.active()) return false;
  runtime_.controller.stopNow(-1,millis()); applyOutputs();
  localInput_.sample(0,0,0,false,millis());
  calibrating_=true;
  runtime_.controller.configurationLocked=true;
  calibrationUntil_=millis()+2000;
  return true;
}
bool Hardware::pollJoystickCalibration() {
  if(!calibrating_ || int32_t(millis()-calibrationUntil_)>=0) { cancelJoystickCalibration(); return false; }
  calibrationUntil_=millis()+2000;
  return true;
}
void Hardware::cancelJoystickCalibration() {
  if(!calibrating_) return;
  calibrating_=false;
  runtime_.controller.configurationLocked=runtime_.recovery || runtime_.hardwareTrial;
  localInput_.sample(0,0,0,false,millis());
}
void Hardware::serviceLocalInputs() {
  const uint32_t now = millis();
  if (!localInputsStarted_) return;
  if(calibrating_ && int32_t(now-calibrationUntil_)>=0) cancelJoystickCalibration();
  if (runtime_.recovery || (runtime_.controller.configurationLocked && !calibrating_)) {
    localInput_.sample(0, 0, 0, false, now); return;
  }
  if (localSampled_ && uint32_t(now - localSampleAt_) < 10) return;
  localSampled_ = true; localSampleAt_ = now;
  const auto &c=runtime_.controller.hardware;
  const auto &p=c.pins;
  float values[2]{};
  uint8_t neutralMask = 0;
  const int negative[]={p.localAzNegative,p.localElNegative}, positive[]={p.localAzPositive,p.localElPositive};
  const int pins[]={p.localAzAdc,p.localElAdc};
  for (unsigned i = 0; i < 2; ++i) {
    if (!(c.localMask() & (1u << i)) || (!calibrating_ && i == 1 && runtime_.controller.model == Model::G800DXA)) {
      neutralMask |= 1u << i; continue;
    }
    if(c.local[0]==LOCAL_INPUT_BUTTONS) {
      values[i] = localButtons_[i].sample(digitalRead(negative[i]) == LOW, digitalRead(positive[i]) == LOW, now);
      if (localButtons_[i].neutral()) neutralMask |= 1u << i;
      if (c.local[1+i]) values[i] = -values[i];
    } else {
      int samples[5];
      for (int &value : samples) value = analogRead(pins[i]);
      std::sort(samples, samples + 5); localRaw_[i]=samples[2];
      values[i] = localJoystick_[i].sample(samples[2]);
      if (localJoystick_[i].neutral(samples[2])) neutralMask |= 1u << i;
    }
  }
  // Selection changes, disconnect and remote takeover require a new neutral gesture.
  const auto selection=runtime_.hud.selectionEpoch;
  localInput_.sample(values[0], values[1], neutralMask,
      !calibrating_ && (!runtime_.hud.isHud() || runtime_.hud.manualAvailable()), now, selection);
}
} // namespace forkrot
