#include "panel_connection.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace forkrot;
using namespace forkrot::panel;
int main() {
  for(bool wifi:{false,true})for(int rssi:{-81,-80,-79})
    for(bool enabled:{false,true})for(bool connected:{false,true}) {
      PanelView v;v.wifiConnected=wifi;v.wifiRssi=rssi;v.mqttEnabled=enabled;v.mqttConnected=connected;
      ConnectionIndicator indicator;
      const auto expected=!wifi?ConnectionWarning::WifiLost:rssi<=-80?ConnectionWarning::WifiWeak:
          enabled&&!connected?ConnectionWarning::MqttLost:ConnectionWarning::None;
      assert(indicator.update(v,123)==expected);
      const bool alternating=wifi&&rssi<=-80&&enabled&&!connected;
      assert(indicator.update(v,2122)==expected);
      assert(indicator.update(v,2123)==(alternating?ConnectionWarning::MqttLost:expected));
      assert(indicator.update(v,4123)==expected);
  }
  PanelView v;v.wifiConnected=true;v.wifiRssi=-80;v.mqttEnabled=true;
  ConnectionIndicator indicator;
  assert(indicator.update(v,0)==ConnectionWarning::WifiWeak);
  assert(indicator.update(v,2000)==ConnectionWarning::MqttLost);
  assert(indicator.update(v,10000)==ConnectionWarning::MqttLost); // preserve phase across missed updates
  v.wifiConnected=false;assert(indicator.update(v,10001)==ConnectionWarning::WifiLost);
  v.wifiConnected=true;assert(indicator.update(v,11000)==ConnectionWarning::WifiWeak);
  assert(indicator.update(v,13000)==ConnectionWarning::MqttLost);
  v.mqttConnected=true;assert(indicator.update(v,13001)==ConnectionWarning::WifiWeak);
  v.wifiRssi=-79;assert(indicator.update(v,13002)==ConnectionWarning::None);
  v.mqttConnected=false;assert(indicator.update(v,13003)==ConnectionWarning::MqttLost);
  v.mqttEnabled=false;assert(indicator.update(v,13004)==ConnectionWarning::None);
  v.wifiConnected=false;assert(indicator.update(v,13005)==ConnectionWarning::WifiLost);
  v.wifiConnected=true;v.wifiRssi=-80;v.mqttEnabled=true;
  ConnectionIndicator wrap;
  const uint32_t start=std::numeric_limits<uint32_t>::max()-500;
  assert(wrap.update(v,start)==ConnectionWarning::WifiWeak);
  assert(wrap.update(v,start+1999u)==ConnectionWarning::WifiWeak);
  assert(wrap.update(v,start+2000u)==ConnectionWarning::MqttLost);
  assert(wrap.update(v,start+4000u)==ConnectionWarning::WifiWeak);
  PanelView original,changed=original;
  assert(original==changed);changed.mqttEnabled=true;assert(!(original==changed));
  changed=original;changed.mqttConnected=true;assert(!(original==changed));
  std::puts("PASS: MQTT enable/connection, Wi-Fi priority and RSSI boundary, 2s alternation, recovery, missed ticks and clock wrap");
}
