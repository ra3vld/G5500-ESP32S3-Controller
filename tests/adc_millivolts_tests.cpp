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
    const auto reads=fakeAdc.conversions,waited=fakeHardware.waitedUs;
    assert(hardware.filteredMv(0)==1234&&hardware.filteredMv(1)==2345);
    assert(fakeAdc.conversions-reads==2*MotionAdcSamples);
    assert(fakeHardware.waitedUs-waited==600); // both axes share three PWM-spaced waits
    assert(hardware.sampleCount(0)==1&&hardware.sampleCount(1)==1);
    assert(hardware.sampledAt(0)==hardware.sampledAt(1));
    hardware.filteredMv(0);hardware.filteredMv(1);assert(fakeAdc.conversions-reads==2*MotionAdcSamples);
    const auto sampled=hardware.sampledAt(0);
    fakeHardware.now+=100;fakeAdc.convertOk=false;
    hardware.filteredMv(0);
    assert(!hardware.positionValid(0) && hardware.sampledAt(0)==sampled);
    assert(!hardware.positionValid(1)&&hardware.sampledAt(1)==sampled);
    const auto failedReads=fakeAdc.conversions;
    hardware.filteredMv(0);hardware.filteredMv(1);assert(fakeAdc.conversions==failedReads);
    int raw,mv;assert(!hardware.adcDiagRead(az,raw,mv) && raw==-1 && mv==-1);
    hardware.positionValue(0);
    assert(hardware.adcDiagRead(az,raw,mv) && raw==-1 && mv==-1);
    config.sensors[6]=1;runtime.controller.configureHardware(config);
    hardware.positionDiagRead(0,raw,mv);assert(raw==-1 && mv==-1);
    assert(fakeAdc.creates==1 && fakeAdc.live==1);
  }
  assert(fakeAdc.live==0);
  fakeAdc.convertOk=true;
  runtime.controller.hardware.sensors[6]=runtime.controller.hardware.sensors[7]=1;
  {
    Hardware hardware(runtime);hardware.beginAdc();
    assert(hardware.filteredMv(0)==2066&&hardware.filteredMv(1)==955);
    const auto sampled=hardware.sampledAt(0);
    fakeHardware.now+=runtime.controller.controlPeriodMs();fakeHardware.millivolts[az]=4000;
    hardware.filteredMv(0);hardware.filteredMv(1);
    assert(!hardware.positionValid(0)&&hardware.sampledAt(0)==sampled);
    assert(hardware.positionValid(1)&&hardware.sampleCount(1)==2);
    fakeHardware.now+=runtime.controller.controlPeriodMs();fakeHardware.millivolts[az]=1234;
    hardware.filteredMv(0);hardware.filteredMv(1);
    assert(hardware.positionValid(0)&&hardware.positionValid(1));
    assert(hardware.sampleCount(0)==2&&hardware.sampleCount(1)==3);
  }
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
