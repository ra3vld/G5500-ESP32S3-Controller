// G5500 Fork; GPL-3.0. Modified CrowPanel: ADC6, AZ on RX/TX, control on BOOT.
#pragma once
#include "build_config.hpp"
#if CROWPANEL_LOCAL_DRIVE
#include "board.hpp"
#include <Arduino.h>
#include <driver/gpio.h>
#include <algorithm>

namespace forkrot {
// GPIO0 is open-drain, externally pulled up to 3.3 V. HIGH/released is OFF:
// BTS7960 needs an inverter before EN; Yaesu needs an inverting 0..5 V PWM stage.
// BOOT LOW at reset enters the ROM loader; disconnect motor power for that mode.
class CrowPanelDrive {
public:
  bool begin(const HardwareConfig &config) {
    pwm_ = config.drive == DRIVE_BTS7960;
    inverted_ = pwm_ && config.pwmInverted;
    controlOff();
    nativeOff();
    // Detect a held button or an external pull-down before acquiring any PWM.
    if (digitalRead(board::PIN_SPEED) != HIGH) return fail();
    for (unsigned i = pwm_ ? 0 : 2; i < (pwm_ ? 2u : 3u); ++i) {
      attached_[i] = ledcAttach(pins_[i], 20000, 10);
      if (!attached_[i]) {
        fail();
        for (unsigned j=0; j<3; ++j) if (attached_[j]) {
          ledcDetach(pins_[j]); attached_[j] = false;
        }
        controlOff(); nativeOff();
        return false;
      }
      if (i == 2 && gpio_set_direction(static_cast<gpio_num_t>(board::PIN_SPEED), GPIO_MODE_INPUT_OUTPUT_OD) != 0) {
        fail(); ledcDetach(pins_[i]); attached_[i] = false; controlOff(); return false;
      }
      ledcWrite(pins_[i], i == 2 ? 1023u : duty(0));
    }
    ready_ = true;
    return true;
  }
  bool apply(int direction, uint32_t output, bool variableSpeed) {
    if (!ready_ || failed_) return false;
    output = std::min<uint32_t>(output, 1023u);
    if (direction < -1 || direction > 1) return fail();
    if (direction == 0 || (pwm_ && output == 0)) return stop();
    if (pwm_ && !enabled_ && digitalRead(board::PIN_SPEED) != HIGH) return fail();
    if (direction != direction_) { controlOff(); nativeOff(); }
    if (pwm_) {
      ledcWrite(direction > 0 ? board::PIN_LEFT : board::PIN_RIGHT, duty(0));
      ledcWrite(direction > 0 ? board::PIN_RIGHT : board::PIN_LEFT, duty(output));
      digitalWrite(board::PIN_SPEED, LOW);
      enabled_ = true;
    } else {
      digitalWrite(direction > 0 ? board::PIN_LEFT : board::PIN_RIGHT, LOW);
      digitalWrite(direction > 0 ? board::PIN_RIGHT : board::PIN_LEFT, HIGH);
      ledcWrite(board::PIN_SPEED, 1023u - (variableSpeed ? output : 0u));
    }
    direction_ = direction;
    return true;
  }
  bool stop() {
    // Release the active-LOW control BEFORE touching direction/PWM outputs.
    controlOff();
    nativeOff();
    direction_ = 0;
    if (pwm_ && digitalRead(board::PIN_SPEED) != HIGH) return fail();
    return !failed_;
  }
private:
  inline static constexpr int pins_[3] = {board::PIN_LEFT, board::PIN_RIGHT, board::PIN_SPEED};
  bool pwm_ = false, inverted_ = false, enabled_ = false;
  bool attached_[3]{}, ready_ = false, failed_ = false;
  int direction_ = 0;
  uint32_t duty(uint32_t value) const { return inverted_ ? 1023-value : value; }
  void controlOff() {
    if (attached_[2]) ledcWrite(board::PIN_SPEED, 1023);
    else {
      gpio_set_level(static_cast<gpio_num_t>(board::PIN_SPEED), HIGH);
      pinMode(board::PIN_SPEED, OUTPUT_OPEN_DRAIN);
    }
    enabled_ = false;
  }
  void nativeOff() {
    for (unsigned i=0; i<2; ++i) {
      if (attached_[i]) ledcWrite(pins_[i], duty(0));
      else {
        gpio_set_level(static_cast<gpio_num_t>(pins_[i]), inverted_ ? HIGH : LOW);
        pinMode(pins_[i], OUTPUT);
      }
    }
  }
  bool fail() {
    controlOff(); nativeOff();
    ready_ = false; failed_ = true; direction_ = 0;
    return false;
  }
};
} // namespace forkrot
#endif
