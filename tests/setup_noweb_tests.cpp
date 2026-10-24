#include "storage.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main(){
  static_assert(!WEB_INTERFACE_ENABLED);
  Preferences::database.clear();Storage storage;SetupState progress;
  progress.required=true;progress.step=SetupStep::Calibration;progress.generation=7;
  assert(storage.saveSetup(progress));const auto saved=Preferences::database["setup"]["state1"];
  Runtime runtime;assert(storage.loadSetup(runtime));
  assert(!runtime.setup.required&&!runtime.controller.setupRequired&&runtime.setup.step==SetupStep::Done);
  assert(Preferences::database["setup"]["state1"]==saved);
  assert(!runtime.controller.movementBlocked());
  std::cout<<"PASS: console-only images bypass unfinished web wizard without deleting its saved progress\n";
}
