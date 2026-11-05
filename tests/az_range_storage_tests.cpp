#include "motion_fixture.hpp"
#include "storage.hpp"
#include "calibration_record.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main() {
  Preferences::database.clear();
  auto &legacy=Preferences::database["g5500"];
  legacy["calfmt"]=uint8_t(4);legacy["azmode"]=uint8_t(45);
  legacy["a45z"]=int32_t(100);legacy["a45f"]=int32_t(2800);
  legacy["a45os"]=true;legacy["a45fs"]=true;
  legacy["ssid"]=String("untouched");
  MotionTestRuntime r;Calibration cal(r.controller);Storage storage;WifiCredentials wifi;
  storage.loadPrefs(r,cal,wifi);assert(cal.activeAzLinearValid());
  assert(legacy.count("azgeom1"));assert(wifi.wifiSsid==String("untouched"));
  // Old firmware kept scalar copies next to the authoritative blob. Reclaim
  // them on load before a replacement blob needs temporary free NVS entries.
  {
    const auto record=std::get<Preferences::Bytes>(legacy["azgeom1"]);
    legacy["calfmt"]=uint8_t(4);legacy["calmode"]=uint8_t(CAL_MULTI_POINT);
    legacy["azmode"]=uint8_t(45);legacy["a45z"]=int32_t(100);legacy["a45f"]=int32_t(2800);
    legacy["a45os"]=true;legacy["a45fs"]=true;legacy["el0mv"]=int32_t(20);
    legacy["pass"]=String("keep-password");legacy["unknown"]=String("keep-custom");
    Preferences::totalEntries=Preferences::usedEntries()+Preferences::reservedEntries+17;
    auto next=cal;assert(next.configureAz(AZ_CUSTOM,-60,450));
    assert(!storage.saveCal(r,next));
    assert(std::get<Preferences::Bytes>(legacy["azgeom1"])==record);
    unsigned writes=0;Preferences::beforeBytesWrite=[&](const char *){++writes;};
    Preferences::failedRemoveKey="a45z";
    storage.loadPrefs(r,cal,wifi);
    assert(cal.activeAzLinearValid() && legacy.count("a45z"));
    Preferences::failedRemoveKey.clear();storage.loadPrefs(r,cal,wifi);
    Preferences::beforeBytesWrite={};assert(writes==0);
    assert(!legacy.count("calfmt") && !legacy.count("a45z") && !legacy.count("el0mv"));
    assert(std::get<Preferences::Bytes>(legacy["azgeom1"])==record);
    assert(std::get<String>(legacy["pass"])==String("keep-password"));
    assert(std::get<String>(legacy["unknown"])==String("keep-custom"));
    assert(storage.saveCal(r,next));
    assert(!legacy.count("calfmt") && !legacy.count("a45z"));
    // A failed write must not succeed merely because readback matches an
    // unchanged old record; no unverified cleanup is allowed either.
    legacy["a45z"]=int32_t(100);Preferences::failedBytesKey="azgeom1";
    assert(!storage.saveCal(r,next));assert(legacy.count("a45z"));
    Preferences::failedBytesKey.clear();Preferences::totalEntries=630;
    legacy["azgeom1"]=record;storage.loadPrefs(r,cal,wifi);
    const auto retained=legacy;legacy.clear();legacy["azgeom1"]=record;
    MotionTestRuntime setup;assert(storage.loadSetup(setup));assert(!setup.setup.required);
    legacy=retained;
  }
  // Upgrade the deployed 384-byte AZ v1 record, retaining both axes' measurements.
  cal.state().elOSet=cal.state().elFSet=true;cal.state().cal.el0=2900;cal.state().cal.elMax=200;
  const auto v1=calibrationRecord<384>(cal.state());
  legacy["azgeom1"]=Preferences::Bytes(v1.begin(),v1.end());
  legacy["a45z"]=int32_t(100);Preferences::failedBytesKey="azgeom1";
  storage.loadPrefs(r,cal,wifi);
  assert(std::get<Preferences::Bytes>(legacy["azgeom1"]).size()==384 && legacy.count("a45z"));
  Preferences::failedBytesKey.clear();
  storage.loadPrefs(r,cal,wifi);
  assert(cal.activeElLinearValid() && cal.state().cal.el0==2900);
  assert(std::get<Preferences::Bytes>(legacy["azgeom1"]).size()==512);
  assert(!legacy.count("a45z"));
  assert(cal.configureEl(EL_CUSTOM,-30,180));assert(storage.saveCal(r,cal));
  storage.loadPrefs(r,cal,wifi);assert(r.controller.elRange.start==-30);
  assert(cal.activeElLinearValid() && cal.mapEl(2900)==-30 && cal.mapEl(200)==150);
  assert(cal.configureEl(EL_CUSTOM,-30,240));assert(storage.saveCal(r,cal));
  storage.loadPrefs(r,cal,wifi);assert(!cal.activeElLinearValid() && cal.state().customEl.count==2);
  assert(cal.configureEl(EL_STANDARD,0,180));assert(cal.activeElLinearValid());
  assert(cal.configureAz(AZ_CUSTOM,-60,450));assert(storage.saveCal(r,cal));
  const auto saved=std::get<Preferences::Bytes>(legacy["azgeom1"]);
  MotionTestRuntime reboot;Calibration loaded(reboot.controller);WifiCredentials credentials;
  storage.loadPrefs(reboot,loaded,credentials);
  assert(reboot.controller.minimum==-60 && reboot.controller.maximum==390);
  assert(loaded.activeAzLinearValid());assert(loaded.mapAz(100)==-60);
  assert(loaded.configureAz(AZ_CUSTOM,50,450));assert(storage.saveCal(reboot,loaded));
  storage.loadPrefs(reboot,loaded,credentials);assert(loaded.mapAz(2800)==500);
  assert(loaded.configureAz(AZ_P36,0,360));assert(storage.saveCal(reboot,loaded));
  storage.loadPrefs(reboot,loaded,credentials);assert(loaded.state().gsAzMode==36);
  assert(loaded.configureAz(AZ_CUSTOM,50,450));assert(loaded.activeAzLinearValid());
  Preferences::failedBytesKey="azgeom1";
  assert(!storage.saveCal(reboot,loaded));Preferences::failedBytesKey.clear();
  storage.loadPrefs(reboot,loaded,credentials);assert(loaded.state().gsAzMode==36);
  // Wrong CRC/length must not quietly reactivate the still-present legacy calibration.
  for (bool truncate : {false,true}) {
    auto corrupt=saved;if(truncate)corrupt.resize(10);else corrupt[30]^=1;
    legacy["a45z"]=int32_t(777);
    legacy["azgeom1"]=corrupt;
    storage.loadPrefs(reboot,loaded,credentials);
    assert(loaded.state().storageFault && !loaded.activeAzLinearValid() && !loaded.activeAzMultiValid());
    assert(reboot.controller.fault=="calibration_storage_invalid");
    assert(!storage.saveCal(reboot,loaded));
    assert(std::get<Preferences::Bytes>(legacy["azgeom1"])==corrupt);
    assert(std::get<int32_t>(legacy["a45z"])==777);
  }
  legacy["azgeom1"]=String("wrong type");
  storage.loadPrefs(reboot,loaded,credentials);
  assert(loaded.state().storageFault && !loaded.activeAzLinearValid());
  legacy["azgeom1"]=Preferences::Bytes{};
  storage.loadPrefs(reboot,loaded,credentials);
  assert(loaded.state().storageFault && !loaded.activeAzLinearValid());
  // An explicit reset is the recovery operation, not a fallback during boot.
  loaded.resetAz(false);assert(storage.saveCal(reboot,loaded));
  storage.loadPrefs(reboot,loaded,credentials);assert(!loaded.state().storageFault && !loaded.activeAzLinearValid());
  // Per-model banks do not overwrite one another.
  legacy["azgeom1"]=saved;
  reboot.controller.model=Model::G800DXA;storage.loadPrefs(reboot,loaded,credentials);
  assert(loaded.state().gsAzMode==45 && !loaded.state().custom.initialized);
  assert(loaded.configureAz(AZ_CUSTOM,50,450));assert(storage.saveCal(reboot,loaded));
  reboot.controller.model=Model::G5500;storage.loadPrefs(reboot,loaded,credentials);
  assert(loaded.state().gsAzMode==AZ_CUSTOM && loaded.activeAzRange().start==-60);
  assert(credentials.wifiSsid==String("untouched"));
  // G-800SA keeps its identity and calibration separate from G-800DXA.
  r.controller.model=Model::G800SA;assert(storage.saveForkSettings(r));
  assert(cal.configureAz(AZ_CUSTOM,-90,450));assert(storage.saveCal(r,cal));
  MotionTestRuntime sa;Calibration saCal(sa.controller);WifiCredentials saWifi;
  storage.loadForkSettings(sa);assert(sa.controller.model==Model::G800SA);
  storage.loadPrefs(sa,saCal,saWifi);assert(sa.controller.minimum==-90&&sa.controller.maximum==360);
  assert(Preferences::database.count("g800sacal"));
  sa.controller.model=Model::G800DXA;storage.loadPrefs(sa,saCal,saWifi);assert(sa.controller.minimum!=-90);
  std::cout<<"PASS: range NVS migration, reboot, per-model banks, failed writes, corruption and explicit recovery\n";
}
