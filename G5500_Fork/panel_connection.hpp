// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "panel_view.hpp"

namespace forkrot::panel {
enum class ConnectionWarning { None, WifiLost, WifiWeak, MqttLost };
class ConnectionIndicator {
  bool alternating_=false, mqttPhase_=false;
  uint32_t phaseAt_=0;
public:
  static constexpr int WeakWifiRssi=-80;
  static constexpr uint32_t AlternateMs=2000;
  ConnectionWarning update(const PanelView &v,uint32_t now) {
    const bool mqttLost=v.mqttEnabled&&!v.mqttConnected;
    const bool weak=v.wifiConnected&&v.wifiRssi<=WeakWifiRssi;
    if(weak&&mqttLost) {
      if(!alternating_) {alternating_=true;mqttPhase_=false;phaseAt_=now;}
      const uint32_t steps=uint32_t(now-phaseAt_)/AlternateMs;
      if(steps&1u)mqttPhase_=!mqttPhase_;
      phaseAt_+=steps*AlternateMs;
      return mqttPhase_?ConnectionWarning::MqttLost:ConnectionWarning::WifiWeak;
    }
    alternating_=false;
    if(!v.wifiConnected)return ConnectionWarning::WifiLost;
    if(mqttLost)return ConnectionWarning::MqttLost;
    return weak?ConnectionWarning::WifiWeak:ConnectionWarning::None;
  }
};
}
