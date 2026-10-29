#include "motion_fixture.hpp"
#include "hardware.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
namespace forkrot::crowpanel { bool beginBus(){return true;} }
int main() {
  static_assert(FORK_CLASSIC_ESP32);
  fakeHardware={};fakeAdc={};
  MotionTestRuntime runtime;
  auto config=runtime.controller.hardware;config.simulation=false;config.bluetooth=false;
  runtime.controller.configureHardware(config);
  const int az=config.pins.azAdc,el=config.pins.elAdc;
  fakeHardware.millivolts[az]=1234;fakeHardware.millivolts[el]=2345;
  {
    Hardware hardware(runtime);hardware.beginAdc();
    assert(fakeAdc.creates==1 && fakeAdc.live==1);
    assert(fakeAdc.config.unit_id==ADC_UNIT_1 && fakeAdc.config.atten==ADC_ATTEN_DB_12);
    assert(fakeAdc.config.bitwidth==ADC_BITWIDTH_12 && fakeAdc.config.default_vref==1100);
    for(unsigned i=0;i<10000;++i) {
      assert(hardware.positionValue(i%2)==(i%2?2345:1234));
      assert(fakeAdc.creates==1 && fakeAdc.live==1);
    }
    hardware.filteredMv(0);const auto sampled=hardware.sampledAt(0);
    fakeHardware.now+=100;fakeAdc.convertOk=false;
    hardware.filteredMv(0);
    assert(!hardware.positionValid(0) && hardware.sampledAt(0)==sampled);
    int raw,mv;hardware.adcDiagRead(az,raw,mv);assert(raw==-1 && mv==-1);
    config.sensors[6]=1;runtime.controller.configureHardware(config);
    hardware.positionDiagRead(0,raw,mv);assert(raw==-1 && mv==-1);
    assert(fakeAdc.creates==1 && fakeAdc.live==1);
  }
  assert(fakeAdc.live==0);
  fakeAdc={};fakeAdc.createOk=false;
  {
    Hardware hardware(runtime);hardware.beginAdc();
    for(unsigned i=0;i<10000;++i)assert(hardware.positionValue(0)==-1);
    assert(fakeAdc.creates==1 && fakeAdc.live==0 && fakeAdc.conversions==0);
    assert(!hardware.positionValid(0));
  }
  assert(fakeAdc.live==0);
  std::cout<<"PASS: classic ADC calibration without eFuse, bounded allocation, failure retry suppression and invalid feedback\n";
}
