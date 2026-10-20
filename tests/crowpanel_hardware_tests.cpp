#include "motion_fixture.hpp"
#include "hardware.hpp"
#include <cassert>
#include <cstdio>
namespace forkrot::crowpanel { bool beginBus() { return true; } }
int main() {
  forkrot::MotionTestRuntime runtime;
  forkrot::Hardware hw(runtime);
  hw.beginDirections(); hw.beginAdc(); hw.beginInputs();
  assert(runtime.controller.driveFault);
  runtime.controller.feedback(20, 30, true, true); // Even saved calibration cannot enable drive.
  assert(runtime.controller.target({100, 90, true, true}, false, 0) == forkrot::Result::Invalid);
  hw.moveDir('L'); hw.moveDir('U'); hw.controlRotator();
  hw.filteredMv(0); hw.filteredMv(1);
  int raw, mv; hw.adcDiagRead(forkrot::board::PIN_AZ_ADC, raw, mv);
  hw.rgbSet(5, 5, 5); hw.allStop();
  assert(fakeHardware.events.empty() && fakeHardware.readsRaw == 0 && fakeHardware.readsMv == 0);
  assert(!runtime.controller.moving() && runtime.controller.fault == "pin_configuration_invalid");
  // Pending targets and local drafts must not be discarded by an idle output pass.
  assert(runtime.controller.target({200, 0, true, false}, true, 0) == forkrot::Result::Pending);
  const auto epoch = runtime.controller.stopEpoch;
  hw.applyOutputs(); hw.applyOutputs();
  assert(runtime.controller.pendingValid && runtime.controller.stopEpoch == epoch);
  std::puts("PASS: CrowPanel has no motor/ADC/RGB GPIO calls; motion blocked; pending target retained");
}
