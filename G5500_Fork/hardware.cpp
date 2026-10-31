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
int Hardware::adcAvgMv(int pin, int secondPin, int *secondValue) {
  if (runtime_.hud.isHud() || runtime_.recovery)
    return 0;
  if (runtime_.controller.hardware.simulation || !board::adc(pin)) {
  (void)pin;
  return 0;
  } else {
  const auto &pins=runtime_.controller.hardware.pins;
  const auto read=[&](int input) {
    const int value=adc_.read(input);
    auto *diagnostic=input==pins.azAdc ? &adcDiagnostics_[0] : input==pins.elAdc ? &adcDiagnostics_[1] : nullptr;
    if(diagnostic && diagnostic->requested) {
      const auto now=millis();
      if(diagnostic->active(now)) diagnostic->sample(value<0 ? -1 : analogRead(input),value,now);
      else diagnostic->requested=false;
    }
    return value;
  };
  uint32_t s = 0, secondSum=0;
  bool valid=true,secondValid=true;
  for (unsigned i = 0; i < MotionAdcSamples; i++) {
    // Spread conversions across the dual-PWM profile's 1 kHz period. A burst of
    // adjacent reads aliases switching interference into a false shaft jump.
    // Interleave due axes: both retain four spaced reads while sharing the
    // three waits, rather than spending another 600 us waiting for EL.
    if(i) delayMicroseconds(200);
    const int value=read(pin);
    if(value<0) {valid=false;if(!secondValue)return -1;}
    else s+=value;
    if(secondValue) {
      const int other=read(secondPin);
      if(other<0)secondValid=false;else secondSum+=other;
    }
  }
  if(secondValue)*secondValue=secondValid?int(secondSum/MotionAdcSamples):-1;
  return valid?int(s/MotionAdcSamples):-1;
  }
}

int Hardware::filteredMv(unsigned axis) {
  if (runtime_.hud.isHud() || runtime_.recovery)
    return 0;
  auto &filter = filters_[axis];
  const uint32_t now = millis();
  const unsigned period=runtime_.controller.controlPeriodMs();
  if(positionAttempted_[axis] &&
      uint32_t(now-positionAttemptAt_[axis])<period) return filter.value();
  if (!filter.due(now,runtime_.controller.controlPeriodMs())) return filter.value();
  positionAttempted_[axis]=true;positionAttemptAt_[axis]=now;
  const auto &c=runtime_.controller.hardware;
  if(axis==0 && !c.simulation && runtime_.controller.model==Model::G5500 &&
      !c.sensorType(0) && !c.sensorType(1) && board::adc(c.pins.azAdc) && board::adc(c.pins.elAdc) &&
      filters_[1].due(now,period) && (!positionAttempted_[1] || uint32_t(now-positionAttemptAt_[1])>=period)) {
    positionAttempted_[1]=true;positionAttemptAt_[1]=now;
    int other=-1;const int first=adcAvgMv(c.pins.azAdc,c.pins.elAdc,&other);
    const int values[]={first,other};const uint32_t sampled=millis();
    for(unsigned i=0;i<2;++i) {
      const int directed=c.sensorInverted(i)&&values[i]>=0?3300-values[i]:values[i];
      positionValid_[i]=directed>=0;
      if(positionValid_[i])filters_[i].sample(directed,sampled,period);
    }
    return filter.value();
  }
  const int value = positionValue(axis);
  if(value<0) return filter.value(); // failed reads never advance the feedback timestamp
  return filter.sample(value, millis(),runtime_.controller.controlPeriodMs()); // timestamp the completed conversion
}
int Hardware::positionValue(unsigned axis) {
  if(axis>1 || runtime_.hud.isHud() || runtime_.recovery) return -1;
  const auto &c=runtime_.controller.hardware;
  if(c.simulation) return 0;
  const int pin=axis?c.pins.elAdc:c.pins.azAdc;
  const int value=c.sensorType(axis)?sensors_.read(c,axis):board::adc(pin)?adcAvgMv(pin):-1;
  const int directed = !c.sensorType(axis) && c.sensorInverted(axis) && value>=0 ? 3300-value : value;
  positionValid_[axis]=directed>=0;
  return directed;
}
bool Hardware::positionDiagRead(unsigned axis,int &raw,int &value,int *millivolts) {
  if(runtime_.controller.hardware.sensorType(axis)) {
    value=positionValue(axis);raw=sensors_.raw(axis);
    return true;
  } else {
    const auto &c=runtime_.controller.hardware;
    const int pin=axis?c.pins.elAdc:c.pins.azAdc;
    const bool ready=adcDiagRead(pin,raw,value);
    if(millivolts) *millivolts=value;
    if(value>=0 && c.sensorInverted(axis) && !c.simulation && !runtime_.recovery && !runtime_.hud.isHud() && board::adc(pin)) value=3300-value;
    return ready;
  }
}
bool Hardware::buttonReleased() const {
  if (runtime_.controller.hardware.buttonGpio < 0) return true;
  return digitalRead(runtime_.controller.hardware.buttonGpio) == (runtime_.controller.hardware.buttonLow ? HIGH : LOW);
}
bool Hardware::adcDiagRead(int pin, int &raw, int &mv) {
  if (runtime_.hud.isHud() || runtime_.recovery ||
      runtime_.controller.hardware.simulation || !board::adc(pin)) {
    raw = mv = 0;
    return true;
  }
  raw = mv = -1;
  const auto &pins=runtime_.controller.hardware.pins;
  auto *diagnostic=pin==pins.azAdc ? &adcDiagnostics_[0] : pin==pins.elAdc ? &adcDiagnostics_[1] : nullptr;
  if(!diagnostic)return false;
  const auto now=millis();
  if(!diagnostic->active(now)) diagnostic->rawSum=diagnostic->mvSum=diagnostic->count=0;
  diagnostic->requested=true;diagnostic->requestedAt=now;
  if(!diagnostic->ready || uint32_t(now-diagnostic->sampledAt)>=AdcDiagnostic::MaxAgeMs)return false;
  raw=diagnostic->raw;mv=diagnostic->mv;return true;
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

void Hardware::allStop() {
  runtime_.controller.stop();
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
  for(unsigned i=0;i<(c.singleAxis()?1u:2u);++i)
    if(c.enablePin(i)>=0) preloadLevel(c.enablePin(i),LOW);
  pwmDirection_[0]=pwmDirection_[1]=0;
  const int pwm[] = {c.drive == DRIVE_BTS7960 ? p.azNegative : p.azSpeedEnable,
                    c.drive == DRIVE_BTS7960 ? p.azPositive : p.elSpeedEnable,
                    c.drive == DRIVE_BTS7960 ? p.elNegative : -1,
                    c.drive == DRIVE_BTS7960 ? p.elPositive : -1};
  for (unsigned i=0; i<4; ++i) if (attached_[i]) ledcWrite(pwm[i], motorPwmDuty(0));
  for(const auto &d:pinDefinitions) if(d.group==PinGroup::Drive && d.kind==PinKind::Output && board::pinActive(c,d)) {
    const int pin=p.*d.member;if(pin>=0)preloadLevel(pin,idleOutputLevel(pin));
  }
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
    const unsigned count = c.drive == DRIVE_BTS7960 ? (c.singleAxis()?2:4) : c.separatePwm() ? (c.singleAxis()?1:2) : 1;
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
  if (runtime_.controller.configurationLocked || runtime_.controller.driveFault) { outputsOff(); return; }
  if (runtime_.hud.isHud()) return;
  const auto &c = runtime_.controller.hardware;
  if(!c.simulation) for(unsigned i=0;i<2;++i)
    if(c.sensorType(i) && !positionValid_[i]) runtime_.controller.feedbackAt(i,NAN,false,millis());
  if (c.simulation) {
    // Both runtime implementations are compiled; the virtual path never owns motor pins.
  }
#if CROWPANEL_LOCAL_DRIVE
  else {
    auto &controller = runtime_.controller;
    if (controller.model == Model::G5500 || !directionsStarted_) {
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
    const bool ready = preparePwm(c.drive != DRIVE_YAESU || runtime_.controller.variableSpeed(0)) && !runtime_.controller.driveFault;
    const int neg[2] = {p.azNegative, p.elNegative}, pos[2] = {p.azPositive, p.elPositive}, speed[2] = {p.azSpeedEnable, p.elSpeedEnable};
    for (unsigned i=0; i<(c.singleAxis()?1u:2u); ++i) {
      const auto &axis=runtime_.controller.axes[i];
      if(c.enablePin(i)>=0 && (!ready || !axis.direction || axis.speedOutputPercent<=0 || axis.direction!=pwmDirection_[i]))
        digitalWrite(c.enablePin(i),LOW);
    }
    for (unsigned i=0; i<(c.singleAxis()?1u:2u); ++i) {
      if(!c.hasDirections(i)) continue;
      const auto &axis = runtime_.controller.axes[i];
      const int d = ready ? axis.direction : 0;
      const uint32_t duty = d ? uint32_t(lroundf(axis.speedOutputPercent*1023.f/100.f)) : 0;
      if (c.drive == DRIVE_BTS7960) {
        if (ready) {
          ledcWrite(d > 0 ? neg[i] : pos[i], motorPwmDuty(0));
          ledcWrite(d > 0 ? pos[i] : neg[i], motorPwmDuty(duty));
        }
      } else if(c.drive==DRIVE_DIR_PWM) {
        if(pwmReady) {
          if(d && pwmDirection_[i]!=d) {
            ledcWrite(speed[i],0); // Never change DIR while PWM is live.
            digitalWrite(pos[i],d>0?HIGH:LOW);
          }
          ledcWrite(speed[i],duty);
        }
      } else {
        if (d <= 0) digitalWrite(pos[i], LOW);
        if (d >= 0) digitalWrite(neg[i], LOW);
        if (d < 0) digitalWrite(neg[i], HIGH);
        if (d > 0) digitalWrite(pos[i], HIGH);
        if (pwmReady && (c.separatePwm() || i == 0)) ledcWrite(speed[i], duty);
      }
      if(c.enablePin(i)>=0) digitalWrite(c.enablePin(i),d && duty ? HIGH:LOW);
      pwmDirection_[i]=d && duty ? d:0;
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
  if(!c.localAxisConfigured(0) && !c.localAxisConfigured(1)) return;
#if CROWPANEL_LOCAL_DRIVE
  directionsStarted_=true;
  if(!crowpanelDrive_.begin(c)) crowpanelFault();
#else
  // Disable an optional enable before acquiring direction/PWM outputs.
  for(unsigned i=0;i<(c.singleAxis()?1u:2u);++i) if(c.enablePin(i)>=0) {
    preloadLevel(c.enablePin(i),LOW);pinMode(c.enablePin(i),OUTPUT);
  }
  for(const auto &d:pinDefinitions) if(d.group==PinGroup::Drive && d.kind==PinKind::Output && board::pinActive(c,d)) {
    const int pin=c.pins.*d.member;if(pin>=0){preloadLevel(pin,idleOutputLevel(pin));pinMode(pin,OUTPUT);}
  }
  directionsStarted_=true;
#endif
}
void Hardware::beginAdc() {
  for (auto &diagnostic:adcDiagnostics_) diagnostic={};
  const auto &p=runtime_.controller.hardware.pins;
  if (runtime_.hud.isHud() || runtime_.recovery || runtime_.controller.hardware.simulation || board::validateHardware(runtime_.controller.hardware)) return;
  analogReadResolution(12);
  sensors_.begin(runtime_.controller.hardware);
  // Arduino 3.3 configures the ADC channel on its first read, before per-pin attenuation.
  if (!runtime_.controller.hardware.sensorType(0) && board::adc(p.azAdc)) {
    analogRead(p.azAdc); analogSetPinAttenuation(p.azAdc, ADC_11db);
  }
  if (!runtime_.controller.hardware.sensorType(1) && board::adc(p.elAdc)) { analogRead(p.elAdc); analogSetPinAttenuation(p.elAdc, ADC_11db); }
  if((!runtime_.controller.hardware.sensorType(0) && board::adc(p.azAdc)) ||
      (!runtime_.controller.hardware.sensorType(1) && board::adc(p.elAdc)))adc_.begin();
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
#if LOCAL_INPUT_SUPPORT

  const auto &c = runtime_.controller.hardware;
  if (!runtime_.recovery && c.buttonGpio >= 0) pinMode(c.buttonGpio, c.buttonPullup ? INPUT_PULLUP : INPUT);
  if (runtime_.recovery || board::validateHardware(c, runtime_.hud.config.uart,runtime_.hud.isHud())) return;
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

#else

#endif
}
bool Hardware::startJoystickCalibration() {
#if LOCAL_INPUT_SUPPORT

  if(!localInputsStarted_ || !(runtime_.controller.hardware.local[0]&LOCAL_INPUT_JOYSTICK) ||
      runtime_.controller.configurationLocked || runtime_.controller.autoTune.active()) return false;
  runtime_.controller.stopNow(-1,millis()); applyOutputs();
  localInput_.sample(0,0,0,false,millis());
  calibrating_=true;
  runtime_.controller.configurationLocked=true;
  calibrationUntil_=millis()+2000;
  return true;

#else
return false;
#endif
}
bool Hardware::pollJoystickCalibration() {
#if LOCAL_INPUT_SUPPORT

  if(!calibrating_ || int32_t(millis()-calibrationUntil_)>=0) { cancelJoystickCalibration(); return false; }
  calibrationUntil_=millis()+2000;
  return true;

#else
return false;
#endif
}
void Hardware::cancelJoystickCalibration() {
#if LOCAL_INPUT_SUPPORT

  if(!calibrating_) return;
  calibrating_=false;
  runtime_.controller.configurationLocked=runtime_.recovery;
  localInput_.sample(0,0,0,false,millis());

#else

#endif
}
void Hardware::serviceLocalInputs() {
#if LOCAL_INPUT_SUPPORT

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
    if (!(c.localMask() & (1u << i)) || (!calibrating_ && i == 1 && runtime_.controller.model != Model::G5500)) {
      neutralMask |= 1u << i; continue;
    }
    float buttons = 0, joystick = 0;
    bool buttonNeutral = true, joystickNeutral = true;
    if ((c.local[0]&LOCAL_INPUT_BUTTONS) && negative[i]>=0 && positive[i]>=0) {
      buttons = localButtons_[i].sample(digitalRead(negative[i]) == LOW, digitalRead(positive[i]) == LOW, now);
      buttonNeutral = localButtons_[i].neutral();
      if (c.local[1+i]) buttons = -buttons;
    }
    if ((c.local[0]&LOCAL_INPUT_JOYSTICK) && pins[i]>=0) {
      int samples[5];
      for (int &value : samples) value = analogRead(pins[i]);
      std::sort(samples, samples + 5); localRaw_[i]=samples[2];
      joystick = localJoystick_[i].sample(samples[2]);
      joystickNeutral = localJoystick_[i].neutral(samples[2]);
    }
    const bool neutral = buttonNeutral && joystickNeutral;
    // An opposing button pair also suppresses the joystick until released.
    if (!buttonNeutral && !buttons) joystick = 0;
    values[i] = localCombined_[i].sample(buttons, joystick, neutral);
    if (neutral) neutralMask |= 1u << i;
  }
  // Selection changes, disconnect and remote takeover require a new neutral gesture.
  const auto selection=runtime_.hud.selectionEpoch;
  localInput_.sample(values[0], values[1], neutralMask,
      !calibrating_ && (!runtime_.hud.isHud() || runtime_.hud.manualAvailable()), now, selection);

#else

#endif
}
} // namespace forkrot
