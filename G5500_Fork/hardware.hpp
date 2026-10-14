// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <Arduino.h>
#include "board.hpp"
#include "position_filter.hpp"
#include "runtime.hpp"
namespace forkrot {

enum RgbState : uint8_t { RGB_IDLE, RGB_RX, RGB_TX, RGB_HEARTBEAT };

class Hardware {
public:
  explicit Hardware(Runtime &runtime) : runtime_(runtime) {}
  void beginDirections();
  void beginAdc();
  void beginInputs();
  void startHeartbeat() {
    lastHeartbeatMs = millis();
  }
  int adcAvgMv(int pin);
  int filteredMv(unsigned axis);
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
  bool pwmReady = false, pwmFailed = false;
  bool attached_[4] = {};
  PositionFilter filters_[2];
  bool preparePwm(bool enable);
  void outputsOff();
  RgbState rgbState = RGB_IDLE;
  uint32_t rgbUntilMs = 0, lastHeartbeatMs = 0;
  bool pendingTxFlash = false;
};
} // namespace forkrot
