// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#include "hardware.hpp"
#include <cmath>
#include <driver/gpio.h>
namespace forkrot {
using namespace board;
namespace {
// Arduino 3.x digitalWrite requires an existing GPIO owner. Set the GPIO latch
// directly before enabling output, and independently of LEDC ownership.
void preloadLevel(int pin, int level) { gpio_set_level(static_cast<gpio_num_t>(pin), level); }
}

namespace {
uint32_t motorPwmDuty(uint32_t logicalDuty) {
#if DRIVE_INTERFACE == DRIVE_BTS7960 && BTS7960_PWM_INVERTED
  return 1023u - logicalDuty;
#else
  return logicalDuty;
#endif
}
int idleOutputLevel(int pin) {
#if DRIVE_INTERFACE == DRIVE_BTS7960 && BTS7960_PWM_INVERTED
  if (pin == PIN_LEFT || pin == PIN_RIGHT || pin == PIN_UP || pin == PIN_DOWN) return HIGH;
#else
  (void)pin;
#endif
  return LOW;
}
}
int Hardware::adcAvgMv(int pin) {
  uint32_t s = 0;
  for (int i = 0; i < 25; i++) {
    s += analogReadMilliVolts(pin);
    delayMicroseconds(150);
  }
  return s / 25;
}

int Hardware::filteredMv(unsigned axis) {
  auto &filter = filters_[axis];
  const uint32_t now = millis();
  return filter.due(now) ? filter.sample(adcAvgMv(axis ? PIN_EL_ADC : PIN_AZ_ADC), now) : filter.value();
}
bool Hardware::buttonReleased() const {
  if (PIN_CONFIRM < 0) return true;
  return digitalRead(PIN_CONFIRM) == (USER_BUTTON_ACTIVE_LOW ? HIGH : LOW);
}
void Hardware::adcDiagRead(int pin, int &raw, int &mv) {
  uint32_t sr = 0, sm = 0;
  for (int i = 0; i < 25; i++) {
    sr += analogRead(pin);
    sm += analogReadMilliVolts(pin);
    delayMicroseconds(150);
  }
  raw = (int)(sr / 25);
  mv = (int)(sm / 25);
}

void Hardware::rgbSet(uint8_t r, uint8_t g, uint8_t b) {
  if (PIN_LED >= 0) digitalWrite(PIN_LED, (r || g || b) ? HIGH : LOW);
  if (PIN_RGB >= 0) {
    rgbLedWrite(PIN_RGB, r, g, b);
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
#if DRIVE_INTERFACE == DRIVE_BTS7960
  // Disable both bridges before changing PWM or detaching LEDC.
  preloadLevel(PIN_SPEED, LOW);
  preloadLevel(PIN_EL_SPEED, LOW);
#endif
  const int pins[] = {PIN_LEFT, PIN_RIGHT, PIN_DOWN, PIN_UP, PIN_SPEED,
#if DRIVE_INTERFACE != DRIVE_YAESU
    PIN_EL_SPEED,
#endif
  };
  const int pwm[] = {
#if DRIVE_INTERFACE == DRIVE_BTS7960
    PIN_LEFT, PIN_RIGHT, PIN_DOWN, PIN_UP
#else
    PIN_SPEED, PIN_EL_SPEED, -1, -1
#endif
  };
  for (unsigned i=0; i<4; ++i) if (attached_[i]) ledcWrite(pwm[i], motorPwmDuty(0));
  for (int pin : pins) if (pin >= 0) preloadLevel(pin, idleOutputLevel(pin));
}
bool Hardware::preparePwm(bool enable) {
  const int pins[] = {
#if DRIVE_INTERFACE == DRIVE_BTS7960
    PIN_LEFT, PIN_RIGHT, PIN_DOWN, PIN_UP
#else
    PIN_SPEED, PIN_EL_SPEED, -1, -1
#endif
  };
  if (pwmFailed) return false;
  if (enable && !pwmReady) {
    outputsOff();
    const unsigned count = DRIVE_INTERFACE == DRIVE_BTS7960 ? 4 : DRIVE_INTERFACE == DRIVE_SEPARATE_PWM ? 2 : 1;
    for (unsigned i=0; i<count; ++i) {
      attached_[i] = ledcAttach(pins[i], DRIVE_INTERFACE == DRIVE_YAESU ? 20000 : 1000, 10);
      if (!attached_[i]) {
        outputsOff();
        for (unsigned j=0; j<=i; ++j) {
          if (attached_[j]) ledcDetach(pins[j]);
          attached_[j] = false;
          preloadLevel(pins[j], idleOutputLevel(pins[j]));
          pinMode(pins[j], OUTPUT);
        }
        pwmFailed = true;
        runtime_.controller.stop();
        runtime_.controller.driveFault = true;
        runtime_.controller.tick(millis());
        runtime_.controller.fault = "pwm_attach_failed";
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
  const bool enable = DRIVE_INTERFACE != DRIVE_YAESU || runtime_.controller.variableSpeed(0);
  const bool ready = preparePwm(enable);
  const int neg[2] = {PIN_LEFT, PIN_DOWN}, pos[2] = {PIN_RIGHT, PIN_UP};
  const int speed[2] = {PIN_SPEED, PIN_EL_SPEED};
#if DRIVE_INTERFACE == DRIVE_BTS7960
  // Drop every stopping bridge before the first PWM update on either axis.
  for (unsigned i=0; i<2; ++i) {
    if (!ready || !runtime_.controller.axes[i].direction || runtime_.controller.axes[i].speedOutputPercent <= 0)
      digitalWrite(speed[i], LOW);
  }
#endif
  for (unsigned i=0; i<2; ++i) {
    const auto &axis = runtime_.controller.axes[i];
    const int d = ready ? axis.direction : 0;
    const uint32_t duty = d ? uint32_t(lroundf(axis.speedOutputPercent*1023.f/100.f)) : 0;
#if DRIVE_INTERFACE == DRIVE_BTS7960
    if (!d || !duty) digitalWrite(speed[i], LOW);
    if (ready) {
      // Logical LOW means full GPIO duty for an inverting optocoupler.
      ledcWrite(d > 0 ? neg[i] : pos[i], motorPwmDuty(0));
      ledcWrite(d > 0 ? pos[i] : neg[i], motorPwmDuty(duty));
    }
    digitalWrite(speed[i], d && duty ? HIGH : LOW);
#else
    if (d <= 0) digitalWrite(pos[i], LOW);
    if (d >= 0) digitalWrite(neg[i], LOW);
    if (d < 0) digitalWrite(neg[i], HIGH);
    if (d > 0) digitalWrite(pos[i], HIGH);
    if (pwmReady && (DRIVE_INTERFACE == DRIVE_SEPARATE_PWM || i == 0)) ledcWrite(speed[i], duty);
#endif
  }
  runtime_.targetAz = runtime_.controller.axes[0].target;
  runtime_.targetEl = runtime_.controller.axes[1].target;
  runtime_.targetValid = runtime_.controller.targetKnown;
  runtime_.azTrack = runtime_.controller.axes[0].tracking;
  runtime_.elTrack = runtime_.controller.axes[1].tracking;
}
void Hardware::beginDirections() {
#if DRIVE_INTERFACE == DRIVE_BTS7960
  preloadLevel(PIN_SPEED, LOW); pinMode(PIN_SPEED, OUTPUT);
  preloadLevel(PIN_EL_SPEED, LOW); pinMode(PIN_EL_SPEED, OUTPUT);
#endif
  const int pins[] = {PIN_LEFT, PIN_RIGHT, PIN_UP, PIN_DOWN, PIN_SPEED, PIN_LED,
#if DRIVE_INTERFACE != DRIVE_YAESU
    PIN_EL_SPEED,
#endif
  };
  for (int pin : pins) if (pin >= 0) { preloadLevel(pin, idleOutputLevel(pin)); pinMode(pin, OUTPUT); }
}
void Hardware::beginAdc() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_AZ_ADC, ADC_11db);
  analogSetPinAttenuation(PIN_EL_ADC, ADC_11db);
}
void Hardware::beginInputs() {
  if (PIN_CONFIRM >= 0) pinMode(PIN_CONFIRM, USER_BUTTON_PULLUP ? INPUT_PULLUP : INPUT);
}
} // namespace forkrot
