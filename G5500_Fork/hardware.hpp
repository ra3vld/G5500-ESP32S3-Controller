// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <Arduino.h>
#include "board.hpp"
#include "position_filter.hpp"
#include "runtime.hpp"
#include "crowpanel_drive.hpp"
#include "rgb_indicator.hpp"
#include "local_input.hpp"
#include "position_sensors.hpp"
#include "adc_millivolts.hpp"
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
  bool calibratingJoystick() const {
#if LOCAL_INPUT_SUPPORT
    return calibrating_;
#else
    return false;
#endif
  }
  int joystickRaw(unsigned axis) const {
#if LOCAL_INPUT_SUPPORT
    return localRaw_[axis];
#else
    (void)axis; return -1;
#endif
  }
  void startHeartbeat() {
    lastHeartbeatMs = millis();
  }
  int adcAvgMv(int pin, int secondPin = -1, int *secondValue = nullptr);
  int positionValue(unsigned axis);
  bool positionValid(unsigned axis) const { return positionValid_[axis]; }
  bool positionDiagRead(unsigned axis,int &raw,int &value,int *millivolts=nullptr);
  int filteredMv(unsigned axis);
  uint32_t sampledAt(unsigned axis) const { return filters_[axis].sampledAt(); }
  uint32_t sampleMaxIntervalMs(unsigned axis) const { return filters_[axis].maxIntervalMs(); }
  uint32_t sampleCount(unsigned axis) const { return filters_[axis].conversions(); }
  bool buttonReleased() const;
  bool adcDiagRead(int pin, int &raw, int &mv);
  void rgbSet(uint8_t r, uint8_t g, uint8_t b);
  void flashRx();
  void flashTx();
  void updateRgb();
  void allStop();
  String motion();
  void controlRotator();
  void applyOutputs();

private:
  void rgbStart(RgbState s);
  Runtime &runtime_;
  RgbIndicator rgb_;

#if LOCAL_INPUT_SUPPORT
  LocalAxisInput localInput_{runtime_.controller, 0};
  bool localInputsStarted_ = false, localSampled_ = false;
  uint32_t localSampleAt_ = 0, calibrationUntil_ = 0;
  bool calibrating_ = false;
  AxisButtons localButtons_[2];
  CombinedLocalAxis localCombined_[2];
  JoystickAxis localJoystick_[2] = {{0,2048,4095,200,50,false},{0,2048,4095,200,50,false}};
  int localRaw_[2] = {-1,-1};
#endif
  bool pwmReady = false, pwmFailed = false, directionsStarted_ = false;
  bool attached_[4] = {};
  int pwmDirection_[2]{};
  PositionFilter filters_[2];
  PositionSensors sensors_;
  AdcMillivolts adc_;
  struct AdcDiagnostic {
    static constexpr uint32_t MaxAgeMs = 2000;
    uint32_t requestedAt = 0, sampledAt = 0, rawSum = 0, mvSum = 0;
    int raw = -1, mv = -1;
    uint8_t count = 0;
    bool requested = false, ready = false;
    bool active(uint32_t now) const { return requested && uint32_t(now-requestedAt)<MaxAgeMs; }
    void sample(int rawValue, int millivolts, uint32_t now) {
      if (rawValue < 0 || millivolts < 0) {
        raw = mv = -1; rawSum = mvSum = count = 0; sampledAt = now; ready = true; requested = false;
      } else {
        rawSum += unsigned(rawValue); mvSum += unsigned(millivolts);
        if (++count == 25) {
          raw = int(rawSum/25); mv = int(mvSum/25);
          rawSum = mvSum = count = 0; sampledAt = now; ready = true; requested = false;
        }
      }
    }
  } adcDiagnostics_[2];
  bool positionValid_[2]{};
  uint32_t positionAttemptAt_[2]{};
  bool positionAttempted_[2]{};
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
