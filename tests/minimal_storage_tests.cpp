#include "storage.hpp"
#include "motion_json.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace forkrot;
static void settingsStorageFailures() {
  Preferences::database.clear();Storage storage;Runtime r;
  r.controller.hardware.simulation=false;r.controller.hardware.drive=DRIVE_YAESU;
  r.loggerSettings.reportHost="192.168.1.2";
  assert(storage.saveForkSettings(r));
  for(const char *key:{"model","mode","speedon","speed","startspd","approachspd","accelms","decelms","slowdeg","azcoast","elcoast","azdecelf","eldecelf","name","nport","pport","rport","cmdport","rhost","nmanual","pmanual","gmanual","rmanual","azmech"}) {
    Preferences::failedKey=key;assert(!storage.saveForkSettings(r));Preferences::failedKey.clear();
  }
  Preferences::corruptStringKey="name";assert(!storage.saveForkSettings(r));Preferences::corruptStringKey.clear();
  assert(storage.saveForkSettings(r));
  // Empty string writes must create a key, rather than match a default value.
  r.loggerSettings.reportHost.clear();Preferences::database["forkcfg"].erase("rhost");Preferences::failedStringKey="rhost";
  assert(!storage.saveForkSettings(r));Preferences::failedStringKey.clear();assert(storage.saveForkSettings(r));
  r.controller.hardware.drive=DRIVE_BTS7960;
  assert(storage.saveForkSettings(r));
  for(const char *key:{"azwork","elwork","azstart","elstart","azapproach","elapproach","azaccel","elaccel","azdecel","eldecel","azslow","elslow","azcoast","elcoast","azdecelf","eldecelf","controlhz","r200","r201"}) {
    Preferences::failedKey=key;assert(!storage.saveForkSettings(r));Preferences::failedKey.clear();
  }
  for(const char *space:{"forkcfg","forkpwm","forkmotion"}) {
    Preferences::failedBeginNamespace=space;assert(!storage.saveForkSettings(r));Preferences::failedBeginNamespace.clear();
  }
  r.hud.config.role=DeviceRole::Hud;r.loggerSettings.reportHost="192.168.1.3";assert(storage.saveForkSettings(r));
  for(const char *key:{"hudname","nport","nmanual","rhost"}) {
    Preferences::failedKey=key;assert(!storage.saveForkSettings(r));Preferences::failedKey.clear();
  }
  Preferences::failedBeginNamespace="forkcfg";assert(!storage.saveForkSettings(r));Preferences::failedBeginNamespace.clear();
  assert(storage.saveForkSettings(r));Preferences::database.clear();
}
int main(){
  settingsStorageFailures();
  {
    Preferences::database.clear();Storage storage;Runtime r;
    auto &c=r.controller;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;c.speedEnabled=true;
    for(unsigned hz:{50u,100u,200u}) {
      assert(api(c,r.protocol,r.loggerSettings,"/api/settings",{{"controlHz",std::to_string(hz)}},0).status==200);
      assert(c.controlPeriodMs()==1000/hz);
      assert(storage.saveForkSettings(r));Runtime reload;reload.controller.hardware=c.hardware;
      storage.loadForkSettings(reload);assert(reload.controller.controlPeriodMs()==1000/hz);
    }
    const auto saved=c.motionSettings;
    for(const char *bad:{"0","51","150","201","100.5","nan"}) {
      assert(api(c,r.protocol,r.loggerSettings,"/api/settings",{{"controlHz",bad},{"azMotionVelocity","10"}},0).status==400);
      assert(c.controlPeriodMs()==5 && c.motionSettings==saved);
    }
    c.feedbackAt(0,90,true,0);c.feedbackAt(1,90,true,0);
    assert(c.jog(0,1,false,0)==Result::Ok);c.tick(0);
    assert(api(c,r.protocol,r.loggerSettings,"/api/settings",{{"controlHz","50"}},1).status==200);
    assert(!c.moving() && !c.axes[0].tracking && !c.axes[1].holdArmed);
    Preferences::database.clear();
  }
  {
    Preferences::database.clear();Runtime r;Storage storage;
    auto &c=r.controller;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.feedbackAt(0,90,true,0);c.feedbackAt(1,90,true,0);
    assert(c.setRuntimeSpeed(0,20,0)==Result::Ok);
    assert(c.jog(0,1,false,0)==Result::Ok);c.tick(0);assert(c.axes[0].direction==1);
    const auto opened=Preferences::opened.size();
    assert(storage.serviceRuntimeSpeeds(r,40000));
    if(MotionControlPeriodMs<20) {
      assert(c.runtimeSpeeds.dirty && Preferences::opened.size()==opened);
      assert(c.axes[0].direction==1);
      // Forced restart flush is retained even when an axis is active.
      assert(storage.serviceRuntimeSpeeds(r,40001,true));
      assert(!c.runtimeSpeeds.dirty);
      assert(c.setRuntimeSpeed(0,21,40002)==Result::Ok);
      assert(storage.serviceRuntimeSpeeds(r,80002));assert(c.runtimeSpeeds.dirty);
      c.stopNow(-1,80003);
      assert(storage.serviceRuntimeSpeeds(r,80003));assert(!c.runtimeSpeeds.dirty);
    } else assert(!c.runtimeSpeeds.dirty);
    Preferences::database.clear();
  }
  {
    Preferences::database.clear();Runtime r;Storage storage;
    r.controller.hardware.drive=DRIVE_BTS7960;r.controller.hardware.simulation=false;
    auto &m=r.controller.motionSettings[0];m.mode=RegulatorMode::Cascade;m.holdEnabled=true;m.kickEnabled=true;m.positionKp=7;
    assert(storage.saveForkSettings(r));const auto records=Preferences::database;
    Runtime reload;reload.controller.hardware=r.controller.hardware;storage.loadForkSettings(reload);
    assert(reload.controller.motionSettings[0].mode==RegulatorMode::Simple);
    assert(!reload.controller.motionSettings[0].holdEnabled&&!reload.controller.motionSettings[0].kickEnabled);
    assert(Preferences::database==records);
  }
  std::cout << "PASS minimal storage failures, frequency/STOP, deferred speed persistence and legacy tuning load without NVS rewrite\n";
}
