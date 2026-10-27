#include "ethernet.hpp"
#include "hardware.hpp"
#include "motion_fixture.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace forkrot;
int main() {
  static_assert(FORK_S3 && !FORK_CLASSIC_ESP32 && ETHERNET_SUPPORTED && CAMERA_ENABLED);
  assert(!commandPortAllowed(32769) && !commandPortAllowed(32770) && commandPortAllowed(4535));
  static_assert(board::PIN_AZ_ADC==4 && board::PIN_EL_ADC==5);
  static_assert(board::PIN_LEFT==16 && board::PIN_RIGHT==17 && board::PIN_DOWN==43 && board::PIN_UP==44);
  static_assert(board::PIN_SPEED==6 && board::PIN_EL_SPEED==7 && board::PIN_UART_RX==-1 && board::PIN_UART_TX==-1);
  EthernetAdapter adapter;
  ETH.success=false;assert(!adapter.begin());ETH.success=true;assert(adapter.begin());
  assert((ETH.spiArguments==std::array<int,10>{ETH_PHY_W5500,1,14,10,9,SPI2_HOST,13,12,11,20}));
  assert(ETH.routePriority==150);
  ETH.link=true;assert(ethernetLinkUp() && !ethernetConnected());
  ETH.ip=true;assert(ethernetConnected());ETH.link=false;assert(!ethernetConnected());
  for(bool simulated:{false,true}) for(int drive:{DRIVE_YAESU,DRIVE_SEPARATE_PWM,DRIVE_BTS7960}) {
    HardwareConfig c;c.simulation=simulated;c.drive=drive;
    assert(!board::validateHardware(c));
    for(int pin=0;pin<=48;++pin) {
      if(!board::ethernetPin(pin) && !board::cameraPin(pin))continue;
      auto invalid=c;invalid.pins.azPositive=pin;
      const auto error=board::validateHardwareDetailed(invalid);
      if(simulated)assert(!error); // Motor GPIO are inactive in simulation.
      else {
        assert(error && error.pin==pin);
        assert(std::strcmp(error.owner,board::ethernetPin(pin)?"ethernet.spi":"camera")==0);
      }
    }
    MotionTestRuntime runtime;runtime.controller.configureHardware(c);Hardware hw(runtime);
    fakeHardware={};hw.beginDirections();hw.beginAdc();hw.applyOutputs();
    for(const auto &event:fakeHardware.events) {
      assert(!board::cameraPin(event.pin) && !board::ethernetPin(event.pin));
      if(simulated)assert(event.operation!="attach" && event.operation!="pwm");
    }
    if(drive==DRIVE_BTS7960 && !simulated) {
      unsigned channels=0;
      for(const auto &event:fakeHardware.events)if(event.operation=="attach")++channels;
      assert(channels==4);
    }
  }
  std::cout << "PASS: Waveshare W5500, camera GPIO ownership, ADC1, dual-axis PWM and simulation\n";
}
