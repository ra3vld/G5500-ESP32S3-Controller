// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <Arduino.h>
#include "board.hpp"
#include "position_filter.hpp"
#include "runtime.hpp"
#include "crowpanel_drive.hpp"
#include "rgb_indicator.hpp"
#include "local_input.hpp"
namespace forkrot {

enum RgbState : uint8_t { RGB_IDLE, RGB_RX, RGB_TX, RGB_HEARTBEAT };

class Hardware {
public:
  explicit Hardware(Runtime &runtime) : runtime_(runtime) {}
  void beginDirections();
  void beginAdc();
  void beginInputs();
  void serviceLocalInputs();
  bool startJoystickCalibration();
  bool pollJoystickCalibration();
  void cancelJoystickCalibration();
  bool calibratingJoystick() const { return calibrating_; }
  int joystickRaw(unsigned axis) const { return localRaw_[axis]; }
  void startHeartbeat() {
    lastHeartbeatMs = millis();
  }
  int adcAvgMv(int pin);
  int filteredMv(unsigned axis);
  uint32_t sampledAt(unsigned axis) const { return filters_[axis].sampledAt(); }
  uint32_t sampleMaxIntervalMs(unsigned axis) const { return filters_[axis].maxIntervalMs(); }
  uint32_t sampleCount(unsigned axis) const { return filters_[axis].conversions(); }
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
  LocalAxisInput localInput_{runtime_.controller, 0};
  bool localInputsStarted_ = false, localSampled_ = false;
  uint32_t localSampleAt_ = 0, calibrationUntil_ = 0;
  bool calibrating_ = false;
  AxisButtons localButtons_[2];
  JoystickAxis localJoystick_[2] = {{0,2048,4095,200,50,false},{0,2048,4095,200,50,false}};
  int localRaw_[2] = {-1,-1};
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
