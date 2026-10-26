#include "device_api_fixture.hpp"
int main() {
  static_assert(GRAPHICAL_DISPLAY);
  Fixture f;assert(f.storage.loadDisplay(f.runtime)&&f.runtime.displayStorageOk);
  auto get=f.call("GET","/api/hardware/display");assert(get.status==200&&get.body.find("\"supported\":true")!=std::string::npos);
  const auto original=Preferences::database;
  Preferences::beginSucceeds=false;Runtime unavailable;
  assert(!f.storage.loadDisplay(unavailable)&&!unavailable.displayStorageOk);
  assert(!f.storage.saveDisplay(DisplaySettings{}));Preferences::beginSucceeds=true;
  CommandForm duplicate;assert(!duplicate.parse("screenRotation=90&screenRotation=0"));
  for(const auto bad:{"screenRotation=45","screenRotation=360","screenRotation=-90","touchMirrorX=2","screenMirrorY=yes","unknown=1"}) {
    assert(f.call("POST","/api/hardware/display",std::string("token=11&revision=0&")+bad).status==400);
    assert(Preferences::database==original&&!f.restart.pending());
  }
  assert(f.call("POST","/api/hardware/display","token=12&revision=0&screenRotation=90").status==409);
  assert(f.call("POST","/api/hardware/display","token=11&revision=1&screenRotation=90").status==409);
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=0").status==200&&!f.restart.pending());
  Preferences::failedBytesKey="pose1";
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=90").status==503);
  assert(!f.runtime.displayRestartPending&&!f.restart.pending()&&Preferences::database==original);
  Preferences::failedBytesKey.clear();
  f.runtime.controller.axes[0].direction=1;f.runtime.controller.axes[0].wanted=1;
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=90&screenMirrorX=1&touchRotation=270&touchMirrorY=1").status==200);
  assert(!f.runtime.controller.moving()&&f.runtime.controller.configurationLocked&&f.restart.pending());
  assert(f.runtime.displaySettings.screen==0&&f.runtime.displaySettings.touch==0); // Active geometry stays stable until reboot.
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=180").status==409);
  Runtime restored;assert(f.storage.loadDisplay(restored));
  assert(restored.displaySettings.screen==5&&restored.displaySettings.touch==11&&restored.displaySettings.revision==1);
  const auto saved=restored.displaySettings;
  Preferences corrupt;assert(corrupt.begin("panel",false));auto bytes=encodeDisplay(saved);bytes[3]^=1;
  assert(corrupt.putBytes("pose1",bytes.data(),bytes.size())==bytes.size());corrupt.end();
  Runtime damaged;assert(!f.storage.loadDisplay(damaged)&&!damaged.displayStorageOk&&damaged.displaySettings.screen==0);
  assert(f.runtime.hardwareRecord.generation==1); // Display orientation has its own record.
  std::cout<<"PASS: display API validation, freshness, failed writes, safe restart, active geometry and persistent/corrupt records\n";
}
