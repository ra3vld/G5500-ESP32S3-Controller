#include "ethernet.hpp"
#include "network_addresses.hpp"
#include "hardware.hpp"
#include "motion_fixture.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace forkrot;
int main() {
  static_assert(FORK_CLASSIC_ESP32 && ETHERNET_SUPPORTED);
  static_assert(USER_BUTTON_GPIO==-1 && !board::gpio(0) && board::gpio(17));
  static_assert(!board::output(35) && !board::pullup(39) && board::adc(36));
  EthernetAdapter adapter;
  ETH.success=false;assert(!adapter.begin() && !adapter.started());
  ETH.success=true;assert(adapter.begin() && adapter.started());
  assert((ETH.arguments==std::array<int,6>{ETH_PHY_LAN8720,1,23,18,16,ETH_CLOCK_GPIO0_IN}));
  assert(ETH.routePriority==150);
  assert(!ethernetConnected() && uint32_t(ethernetIP())==0);
  ETH.link=true;assert(ethernetLinkUp() && !ethernetConnected()); // cable before DHCP
  ETH.ip=true;assert(ethernetConnected() && uint32_t(ethernetIP())==0x0a01000a);
  ETH.link=false;assert(!ethernetConnected() && uint32_t(ethernetIP())==0); // stale lease after unplug
  ETH.link=true;assert(ethernetConnected()); // reconnect
  ETH.ip=false;assert(!ethernetConnected()); // lease loss while carrier remains
  for(bool simulation:{false,true}) for(int drive:{DRIVE_YAESU,DRIVE_SEPARATE_PWM,DRIVE_BTS7960}) {
    HardwareConfig config;config.simulation=simulation;config.drive=drive;
    assert(!board::validateHardware(config));
    config.bluetooth=true;config.bluetoothMode=BT_ESP32_SPP;assert(!board::validateHardware(config));
    config.bluetoothMode=BT_HC05;assert(!board::validateHardware(config));
    config.bluetooth=false;
    for(int pin:{0,16,18,19,21,22,23,25,26,27}) {
      config.pins.azPositive=pin;
      const auto error=board::validateHardwareDetailed(config);
      if(simulation)assert(!error); // Simulation does not reserve motor GPIO.
      else assert(error && error.pin==pin && std::strcmp(error.owner,"ethernet.rmii")==0);
    }
    config.pins=PinMap{};
    MotionTestRuntime runtime;runtime.controller.configureHardware(config);Hardware motor(runtime);
    fakeHardware={};motor.beginDirections();motor.beginAdc();motor.applyOutputs();
    for(const auto &event:fakeHardware.events) assert(!board::ethernetPin(event.pin));
    if(simulation) {
      assert(fakeHardware.readsRaw==0 && fakeHardware.readsMv==0);
      for(const auto &event:fakeHardware.events) assert(event.operation!="attach" && event.operation!="pwm");
    }
  }
  char addresses[90];
  formatNetworkAddresses(addresses,sizeof addresses,"10.0.2.5","192.168.4.1"," / ","10.0.1.5");
  assert(std::strcmp(addresses,"ETH: 10.0.1.5 / STA: 10.0.2.5 / AP: 192.168.4.1")==0);
  formatNetworkAddresses(addresses,sizeof addresses,"-","-","\n","10.0.1.5");
  assert(std::strcmp(addresses,"ETH: 10.0.1.5")==0);
  char bounded[8]="1234567";
  formatNetworkAddresses(bounded+1,6,"-","-","\n","10.0.1.5");
  assert(bounded[0]=='1' && bounded[6]==0 && bounded[7]==0);
  std::cout << "PASS: WT32 PHY, DHCP/link transitions, Ethernet GPIO ownership, drive and simulation adapters, address formatting\n";
}
