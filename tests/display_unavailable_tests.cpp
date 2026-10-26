#include "device_api_fixture.hpp"
int main() {
  static_assert(!GRAPHICAL_DISPLAY);
  Fixture f;const auto before=Preferences::database;
  assert(f.storage.loadDisplay(f.runtime));
  auto state=f.call("GET","/api/hardware/display");
  assert(state.status==200&&state.body.find("\"supported\":false")!=std::string::npos);
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=90").status==409);
  assert(Preferences::database==before&&!f.restart.pending());
  std::cout<<"PASS: unavailable display capabilities never write settings or restart\n";
}
