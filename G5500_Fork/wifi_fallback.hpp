// G5500 Fork; GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
enum class WifiAction { None, StartAp, StartSta, StopAp };
// AP is a fallback. A reconnect runs alongside it for at most eight seconds;
// successful STA removes AP, loss of STA restores AP before the retry interval.
class WifiFallback {
public:
  static constexpr uint32_t RetryMs = 30000, AttemptMs = 8000;
  void reset(uint32_t now, bool connected) {
    phase_ = connected ? Phase::Online : Phase::Fallback;
    changedMs_ = now;
    apStopAttemptMs_ = now - 1000;
  }
  WifiAction update(uint32_t now, bool configured, bool connected, bool apEnabled) {
    if (connected) {
      phase_ = Phase::Online;
      changedMs_ = now;
      if (apEnabled && uint32_t(now - apStopAttemptMs_) >= 1000) {
        apStopAttemptMs_ = now;
        return WifiAction::StopAp;
      }
      return WifiAction::None;
    }
    if (phase_ == Phase::Online) {
      phase_ = Phase::Fallback;
      changedMs_ = now;
      return WifiAction::StartAp;
    }
    if (!configured) return WifiAction::None;
    const uint32_t limit = phase_ == Phase::Trying ? AttemptMs : RetryMs;
    if (uint32_t(now - changedMs_) < limit) return WifiAction::None;
    const bool timedOut = phase_ == Phase::Trying;
    phase_ = timedOut ? Phase::Fallback : Phase::Trying;
    changedMs_ = now;
    return timedOut ? WifiAction::StartAp : WifiAction::StartSta;
  }
private:
  enum class Phase { Online, Fallback, Trying };
  Phase phase_ = Phase::Fallback;
  uint32_t changedMs_ = 0, apStopAttemptMs_ = 0;
};
} // namespace forkrot
