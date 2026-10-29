#include "device_api_fixture.hpp"
struct DisplayStopSink : CommandSink {
  unsigned stops=0;
  Result submit(const ControlCommand &c) override {assert(c.op==ControlOp::Stop && c.axis==-1);++stops;return Result::Ok;}
  bool available() const override {return true;}
  uint32_t requestId() const override {return 0;}
  Result lastResult() const override {return Result::Ok;}
};
int main() {
  static_assert(GRAPHICAL_DISPLAY);
  const char *key=DISPLAY_KIND==1?"pose1":DISPLAY_KIND==3?"pose3":DISPLAY_KIND==4?"pose4":DISPLAY_KIND==6?"pose6":"pose5";
  for(unsigned mask=0;mask<=DisplayAllFields;++mask) {
    DisplayStopSink remote;Fixture f;assert(f.storage.loadDisplay(f.runtime));
    if(f.runtime.hud.isHud())f.runtime.controller.remote=&remote;
    const auto defaults=f.runtime.displaySettings.visible;
    std::string args="token=11&revision=0";
    for(unsigned i=0;i<6;++i)args+='&'+std::string(DisplayFieldNames[i])+'='+((mask&(1u<<i))?"1":"0");
    f.runtime.controller.axes[0].direction=1;f.runtime.controller.axes[0].wanted=1;
    assert(f.call("POST","/api/hardware/display",args).status==200);
    assert(f.restart.pending()==(mask!=defaults));
    if(mask!=defaults) {
      assert(f.runtime.hud.isHud()?remote.stops==1:!f.runtime.controller.moving());
      Runtime restored;assert(f.storage.loadDisplay(restored));
      assert(restored.displaySettings.visible==mask && restored.displaySettings.screen==0 && restored.displaySettings.touch==0);
    }
  }
  DisplayStopSink remote;Fixture f;assert(f.storage.loadDisplay(f.runtime)&&f.runtime.displayStorageOk);
  if(f.runtime.hud.isHud())f.runtime.controller.remote=&remote;
  auto get=f.call("GET","/api/hardware/display");assert(get.status==200&&get.body.find("\"supported\":true")!=std::string::npos);
  for(unsigned i=0;i<6;++i)assert(get.body.find('"'+std::string(DisplayFieldNames[i])+"\":"+((f.runtime.displaySettings.visible&(1u<<i))?"1":"0"))!=std::string::npos);
  if(DISPLAY_KIND==6) {
    assert(get.body.find("\"width\":360")!=std::string::npos);
    assert(get.body.find("\"height\":360")!=std::string::npos);
  }
  const auto original=Preferences::database;
  Preferences::beginSucceeds=false;Runtime unavailable;
  assert(!f.storage.loadDisplay(unavailable)&&!unavailable.displayStorageOk);
  assert(!f.storage.saveDisplay(DisplaySettings{}));Preferences::beginSucceeds=true;
  CommandForm duplicate;assert(!duplicate.parse("screenRotation=90&screenRotation=0"));
  for(const auto bad:{"screenRotation=45","screenRotation=360","screenRotation=-90","touchMirrorX=2","screenMirrorY=yes","unknown=1","showName=2","showHeap=-1","showAxis=yes"}) {
    assert(f.call("POST","/api/hardware/display",std::string("token=11&revision=0&")+bad).status==400);
    assert(Preferences::database==original&&!f.restart.pending());
  }
  assert(f.call("POST","/api/hardware/display","token=12&revision=0&screenRotation=90").status==409);
  assert(f.call("POST","/api/hardware/display","token=11&revision=1&screenRotation=90").status==409);
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=0").status==200&&!f.restart.pending());
  Preferences::failedBytesKey=key;
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&showName=0").status==503);
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=90").status==503);
  assert(!f.runtime.displayRestartPending&&!f.restart.pending()&&Preferences::database==original);
  Preferences::failedBytesKey.clear();
  f.runtime.controller.axes[0].direction=1;f.runtime.controller.axes[0].wanted=1;
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=90&screenMirrorX=1&touchRotation=270&touchMirrorY=1").status==200);
  assert((f.runtime.hud.isHud()?remote.stops>0:!f.runtime.controller.moving())&&f.runtime.controller.configurationLocked&&f.restart.pending());
  assert(f.runtime.displaySettings.screen==0&&f.runtime.displaySettings.touch==0); // Active geometry stays stable until reboot.
  assert(f.call("POST","/api/hardware/display","token=11&revision=0&screenRotation=180").status==409);
  Runtime restored;assert(f.storage.loadDisplay(restored));
  assert(restored.displaySettings.screen==5&&restored.displaySettings.touch==11&&restored.displaySettings.revision==1);
  const auto saved=restored.displaySettings;
  {
    auto legacy=encodeDisplay(restored.displaySettings);legacy[2]=1;legacy[5]=0;
    const auto crc=displayChecksum(legacy);for(unsigned i=0;i<4;++i)legacy[12+i]=uint8_t(crc>>(8*i));
    Preferences prefs;assert(prefs.begin("panel",false));assert(prefs.putBytes(key,legacy.data(),legacy.size())==legacy.size());prefs.end();
    Runtime migrated;assert(f.storage.loadDisplay(migrated));
    assert(migrated.displaySettings.samePose(restored.displaySettings));
    assert(migrated.displaySettings.visible==defaultDisplayFields(DISPLAY_KIND));
    assert(migrated.displaySettings.revision==restored.displaySettings.revision);
  }

  Preferences corrupt;assert(corrupt.begin("panel",false));auto bytes=encodeDisplay(saved);bytes[3]^=1;
  assert(corrupt.putBytes(key,bytes.data(),bytes.size())==bytes.size());corrupt.end();
  Runtime damaged;assert(!f.storage.loadDisplay(damaged)&&!damaged.displayStorageOk&&damaged.displaySettings.screen==0);
  assert(f.runtime.hardwareRecord.generation==1); // Display orientation has its own record.
  std::cout<<"PASS: display API validation, freshness, failed writes, safe restart, active geometry and persistent/corrupt records\n";
}
