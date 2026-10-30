#include "motion_fixture.hpp"
#include "hardware.hpp"
#include <cassert>
#include <cstdio>
using namespace forkrot;
namespace forkrot::crowpanel { bool beginBus(){return true;} }
int main() {
  MotionTestRuntime runtime;
  Hardware h(runtime);h.beginAdc();
  const auto &pins=runtime.controller.hardware.pins;
  fakeHardware.millivolts[pins.azAdc]=1234;fakeHardware.millivolts[pins.elAdc]=2345;
  h.positionValue(0);h.positionValue(1);
  const auto idleRaw=fakeHardware.readsRaw;
  int raw,mv;
  auto request=[&](unsigned axis) {
    const auto rawReads=fakeHardware.readsRaw,mvReads=fakeHardware.readsMv,waited=fakeHardware.waitedUs;
    const bool ready=h.positionDiagRead(axis,raw,mv);
    assert(fakeHardware.readsRaw==rawReads && fakeHardware.readsMv==mvReads && fakeHardware.waitedUs==waited);
    return ready;
  };
  assert(!request(0) && raw==-1 && mv==-1);
  assert(!request(1));
  for(unsigned i=0;i<6;++i) {
    fakeHardware.now+=20;h.positionValue(0);h.positionValue(1);
    assert(!request(0) && !request(1));
  }
  fakeHardware.now+=20;h.positionValue(0);h.positionValue(1);
  assert(fakeHardware.readsRaw==idleRaw+50);
  for(unsigned i=0;i<10;++i){fakeHardware.now+=20;h.positionValue(0);h.positionValue(1);}
  assert(fakeHardware.readsRaw==idleRaw+50); // Stop after exactly one requested batch.
  assert(request(0) && raw==1234 && mv==1234);
  assert(request(1) && raw==2345 && mv==2345);
  assert(fakeHardware.readsRaw==idleRaw+50);
  // Capture stops when the diagnostic reader disappears, and stale data is
  // never returned as a fresh measurement after reopening the page.
  fakeHardware.now+=2001;
  const auto stoppedRaw=fakeHardware.readsRaw;
  h.positionValue(0);assert(fakeHardware.readsRaw==stoppedRaw);
  assert(!request(0));
  fakeHardware.millivolts[pins.azAdc]=900;
  for(unsigned i=0;i<7;++i){fakeHardware.now+=20;h.positionValue(0);}
  assert(request(0) && mv==900);
  h.beginAdc();assert(!request(0));
  // Unsigned ages also work across millis() wrap.
  fakeHardware.now=UINT32_MAX-50;h.beginAdc();assert(!request(0));
  for(unsigned i=0;i<7;++i){fakeHardware.now+=20;h.positionValue(0);}
  assert(request(0) && mv==900);
  std::puts("PASS: ADC requests perform no conversions/waits; background averages, independent axes, expiry, reinit and millis wrap");
}
