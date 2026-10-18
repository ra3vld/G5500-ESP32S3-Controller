// Explicitly commissioned fixtures for pre-existing protocol/UI regressions.
// Migration and motion safety tests construct the production defaults directly.
#pragma once
#include "runtime.hpp"
namespace forkrot {
inline void commissionTestMotion(Controller &c) {
  for(auto &settings:c.motionSettings) settings.ready=true;
}
inline Controller testController() {
  Controller c; commissionTestMotion(c); return c;
}
struct MotionTestRuntime : Runtime {
  MotionTestRuntime() { commissionTestMotion(controller); }
};
}
