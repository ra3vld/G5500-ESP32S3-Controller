// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <Arduino.h>
#include "board.hpp"
#include "position_filter.hpp"
#include "runtime.hpp"
#include "crowpanel_drive.hpp"
#include "rgb_indicator.hpp"
#if LOCAL_INPUT_MODE != LOCAL_INPUT_NONE
#include "local_input.hpp"
#endif
namespace forkrot {

enum RgbState : uint8_t { RGB_IDLE, RGB_RX, RGB_TX, RGB_HEARTBEAT };

class Hardware {
public:
  explicit Hardware(Runtime &runtime) : runtime_(runtime) {}
  void beginDirections();
  void beginAdc();
  void beginInputs();
  void serviceLocalInputs();
  void startHeartbeat() {
    lastHeartbeatMs = millis();
  }
  int adcAvgMv(int pin);
  int filteredMv(unsigned axis);
  uint32_t sampledAt(unsigned axis) const { return filters_[axis].sampledAt(); }
  uint32_t sampleMaxIntervalMs(unsigned axis) const { return filters_[axis].maxIntervalMs(); }
  bool buttonReleased() const;
  void adcDiagRead(int pin, int &raw, int &mv);
  void rgbSet(uint8_t r, uint8_t g, uint8_t b);
  void flashRx();
  void flashTx();
  void updateRgb();
  void stopAzimuth();
  void allStop();
  void moveDir(char d);
  String motion();
  void controlRotator();
  void applyOutputs();

private:
  void rgbStart(RgbState s);
  Runtime &runtime_;
  RgbIndicator rgb_;
#if LOCAL_INPUT_MODE != LOCAL_INPUT_NONE
  static constexpr uint8_t LocalMask = LOCAL_INPUT_MODE == LOCAL_INPUT_BUTTONS
      ? (LOCAL_AZ_NEG_GPIO >= 0 ? 1 : 0) | (LOCAL_EL_NEG_GPIO >= 0 ? 2 : 0)
      : (LOCAL_AZ_ADC_GPIO >= 0 ? 1 : 0) | (LOCAL_EL_ADC_GPIO >= 0 ? 2 : 0);
  LocalAxisInput localInput_{runtime_.controller, LocalMask};
  bool localInputsStarted_ = false, localSampled_ = false;
  uint32_t localSampleAt_ = 0;
#if LOCAL_INPUT_MODE == LOCAL_INPUT_BUTTONS
  AxisButtons localButtons_[2];
#else
  JoystickAxis localJoystick_[2] = {
      {LOCAL_AZ_ADC_MIN, LOCAL_AZ_ADC_CENTER, LOCAL_AZ_ADC_MAX, LOCAL_ADC_DEADZONE, LOCAL_ADC_HYSTERESIS, LOCAL_AZ_INVERT},
      {LOCAL_EL_ADC_MIN, LOCAL_EL_ADC_CENTER, LOCAL_EL_ADC_MAX, LOCAL_ADC_DEADZONE, LOCAL_ADC_HYSTERESIS, LOCAL_EL_INVERT}};
#endif
#endif
  bool pwmReady = false, pwmFailed = false, directionsStarted_ = false;
  bool attached_[4] = {};
  PositionFilter filters_[2];
#if CROWPANEL_LOCAL_DRIVE
  CrowPanelDrive crowpanelDrive_;
  void crowpanelFault();
#endif
  bool preparePwm(bool enable);
  void outputsOff();
  uint32_t motorPwmDuty(uint32_t duty) const;
  int idleOutputLevel(int pin) const;
  RgbState rgbState = RGB_IDLE;
  uint32_t rgbUntilMs = 0, lastHeartbeatMs = 0;
  bool pendingTxFlash = false;
};
} // namespace forkrot
