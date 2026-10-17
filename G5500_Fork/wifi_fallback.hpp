// G5500 Fork; GPL-3.0.
#pragma once
#include <cstdint>
#include <initializer_list>
namespace forkrot {
// Addresses use the same network-byte representation as uint32_t(IPAddress).
// Pick a /24 that does not overlap even a broad STA route (e.g. 192.168/16).
inline uint32_t serviceApAddress(uint32_t sta, uint32_t mask) {
  constexpr uint32_t apMask = 0x00ffffffu;
  for (uint32_t candidate : {0x0104a8c0u, 0x01ff1facu, 0x01ffff0au})
    if (!sta || !mask || (candidate & mask & apMask) != (sta & mask & apMask))
      return candidate;
  return 0; // No private subnet can avoid a default-only STA mask.
}
enum class WifiAction { None, StartAp, StartSta, StopAp, ReconnectSta };
// A reconnect runs alongside AP for at most eight seconds. Successful STA
// removes AP only in fallback mode; always-on AP stays available with STA.
class WifiFallback {
public:
  static constexpr uint32_t RetryMs = 30000, AttemptMs = 8000;
  void reset(uint32_t now, bool connected) {
    phase_ = connected ? Phase::Online : Phase::Fallback;
    changedMs_ = now;
    apStopAttemptMs_ = now - 1000;
  }
  WifiAction update(uint32_t now, bool configured, bool connected, bool apEnabled,
                    bool keepAp = false) {
    if (connected) {
      phase_ = Phase::Online;
      changedMs_ = now;
      if (!keepAp && apEnabled && uint32_t(now - apStopAttemptMs_) >= 1000) {
        apStopAttemptMs_ = now;
        return WifiAction::StopAp;
      }
      return WifiAction::None;
    }
    if (phase_ == Phase::Online) {
      // Give a brief radio loss a chance to recover on the current interface.
      // Disabling STA here would turn every disconnect into a 30-second outage.
      phase_ = configured ? Phase::Trying : Phase::Fallback;
      changedMs_ = now;
      return configured ? WifiAction::ReconnectSta : WifiAction::StartAp;
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
