#include "device_api_fixture.hpp"
int main() {
  static_assert(!GRAPHICAL_DISPLAY);
  Fixture f;const auto before=Preferences::database;
  assert(f.storage.loadDisplay(f.runtime));
  auto state=f.call("GET","/api/hardware/display");
#if DISPLAY_SUPPORT && DISPLAY_KIND != DISPLAY_NONE
  assert(state.status==200&&state.body.find("\"supported\":false")!=std::string::npos);
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=90").status==409);
#else
  assert(state.status==501&&state.body.find("feature_disabled")!=std::string::npos);
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=90").status==501);
#endif
  assert(Preferences::database==before&&!f.restart.pending());
  std::cout<<"PASS: unavailable display capabilities never write settings or restart\n";
}
