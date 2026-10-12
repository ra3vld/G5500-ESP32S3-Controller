// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#include "hardware.hpp"
#include <cmath>
namespace forkrot {
using namespace board;
int Hardware::adcAvgMv(int pin) {
  uint32_t s = 0;
  for (int i = 0; i < 25; i++) {
    s += analogReadMilliVolts(pin);
    delayMicroseconds(150);
  }
  return s / 25;
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
  bool l = digitalRead(PIN_LEFT);
  bool r = digitalRead(PIN_RIGHT);
  bool u = digitalRead(PIN_UP);
  bool d = digitalRead(PIN_DOWN);

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

void Hardware::applyOutputs() {
  const int neg[2] = {PIN_LEFT, PIN_DOWN}, pos[2] = {PIN_RIGHT, PIN_UP};
  for (unsigned i = 0; i < 2; ++i) {
    int d = runtime_.controller.axes[i].direction;
    if (d <= 0) {
      digitalWrite(pos[i], LOW);
    }
    if (d >= 0) {
      digitalWrite(neg[i], LOW);
    }
    if (d < 0) {
      digitalWrite(neg[i], HIGH);
    }
    if (d > 0) {
      digitalWrite(pos[i], HIGH);
    }
  }
  runtime_.targetAz = runtime_.controller.axes[0].target;
  runtime_.targetEl = runtime_.controller.axes[1].target;
  runtime_.targetValid = runtime_.controller.targetKnown;
  runtime_.azTrack = runtime_.controller.axes[0].tracking;
  runtime_.elTrack = runtime_.controller.axes[1].tracking;
  bool enable =
      runtime_.controller.model == forkrot::Model::G800DXA && runtime_.controller.speedEnabled;
  if (enable && !pwmReady) {
    pwmReady = ledcAttach(PIN_SPEED, 20000, 10);
    if (!pwmReady) {
      runtime_.controller.speedEnabled = false;
      runtime_.controller.fault = "pwm_attach_failed";
    }
  }
  if (pwmReady && !enable) {
    ledcWrite(PIN_SPEED, 0);
    ledcDetach(PIN_SPEED);
    pwmReady = false;
    pinMode(PIN_SPEED, OUTPUT);
    digitalWrite(PIN_SPEED, LOW);
  }
  if (pwmReady) {
    ledcWrite(PIN_SPEED, runtime_.controller.axes[0].direction
                             ? uint32_t(lroundf(runtime_.controller.axes[0].speedOutputPercent *
                                                1023.f / 100.f))
                             : 0);
  }
}
void Hardware::beginDirections() {
  digitalWrite(PIN_LEFT, LOW);
  digitalWrite(PIN_RIGHT, LOW);
  digitalWrite(PIN_UP, LOW);
  digitalWrite(PIN_DOWN, LOW);
  pinMode(PIN_LEFT, OUTPUT);
  pinMode(PIN_RIGHT, OUTPUT);
  pinMode(PIN_UP, OUTPUT);
  pinMode(PIN_DOWN, OUTPUT);
}
void Hardware::beginAdc() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_AZ_ADC, ADC_11db);
  analogSetPinAttenuation(PIN_EL_ADC, ADC_11db);
}
void Hardware::beginInputs() {
  pinMode(PIN_CONFIRM, INPUT_PULLUP);
  pinMode(PIN_SPEED, OUTPUT);
  digitalWrite(PIN_SPEED, LOW);
}

} // namespace forkrot
