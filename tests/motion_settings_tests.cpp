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
  // Main-page speed commands change live setpoints independently of saved tuning.
  {
    Runtime speedRuntime; auto &speed=speedRuntime.controller;
    HardwareConfig hardware;hardware.drive=DRIVE_BTS7960;hardware.simulation=true;speed.configureHardware(hardware);
    speed.feedback(30,60,true,true);speed.motionSettings[0].outputLimitPercent=90;
    const auto savedSpeed=speed.speeds;const auto savedMotion=speed.motionSettings;
    assert(speed.jog(0,1,false,0)==Result::Ok);speed.tick(0);
    const auto direction=speed.axes[0].direction;assert(direction==1);
    assert(speed.setRuntimeSpeed(0,4.5f,19)==Result::Ok);
    assert(speed.runtimeSpeed(0)==4.5f);
    assert(speed.setRuntimeSpeed(0,4.49f,19)==Result::Invalid);
    assert(api(speed,speedRuntime.protocol,speedRuntime.loggerSettings,"/api/speed",{{"axis","az"},{"value","72"}},20).status==200);
    assert(speed.runtimeSpeed(0)==72 && speed.runtimeSpeed(1)==50);
    assert(speed.axes[0].direction==direction && speed.speeds==savedSpeed && speed.motionSettings==savedMotion);
    speed.motionSettings[1].mode=RegulatorMode::Cascade;speed.motionSettings[1].velocity=27.5f;
    assert(speed.setRuntimeSpeed(1,1.375f,39)==Result::Ok);
    assert(speed.runtimeSpeed(1)==1.375f);
    assert(speed.setRuntimeSpeed(1,1.374f,39)==Result::Invalid);
    assert(api(speed,speedRuntime.protocol,speedRuntime.loggerSettings,"/api/speed",{{"axis","el"},{"value","12.75"}},40).status==200);
    assert(speed.runtimeSpeed(1)==12.75f && speed.motionSettings[1].velocity==27.5f && speed.runtimeSpeed(0)==72);
    for(const auto &form:{Form{{"axis","az"},{"value","91"}},Form{{"axis","el"},{"value","28"}},Form{{"axis","az"},{"value","4.49"}},Form{{"axis","el"},{"value","1.374"}},Form{{"axis","az"},{"value","nan"}},Form{{"axis","bad"},{"value","20"}}})
      assert(api(speed,speedRuntime.protocol,speedRuntime.loggerSettings,"/api/speed",form,60).status==400);
    assert(speed.runtimeSpeed(0)==72 && speed.runtimeSpeed(1)==12.75f);
    Storage speedStorage;assert(speedStorage.saveForkSettings(speedRuntime));Runtime reloaded;reloaded.controller.hardware=hardware;speedStorage.loadForkSettings(reloaded);
    assert(reloaded.controller.runtimeSpeed(0)==savedSpeed[0].speedPercent && reloaded.controller.runtimeSpeed(1)==27.5f);
    assert(speedStorage.serviceRuntimeSpeeds(speedRuntime,40039));
    assert(!Preferences::database.count("forkspeed")); // 40 s after the LAST axis change.
    if(MotionControlPeriodMs<20) speed.stopNow(-1,40040);
    assert(speedStorage.serviceRuntimeSpeeds(speedRuntime,40040));
    assert(!speed.runtimeSpeeds.dirty);
    speedStorage.loadForkSettings(reloaded);
    assert(reloaded.controller.runtimeSpeed(0)==72 && reloaded.controller.runtimeSpeed(1)==12.75f);
    assert(reloaded.controller.speeds==savedSpeed && reloaded.controller.motionSettings[1].velocity==27.5f);
    auto adjusted=speed.motionSettings[1];adjusted.velocityKp+=1;
    assert(speed.configureMotion(1,adjusted,40061));
    assert(speed.runtimeSpeed(0)==72 && speed.runtimeSpeed(1)==12.75f);
    const auto status=stateJson(speed,speedRuntime.protocol,speedRuntime.loggerSettings,60);
    assert(status.find("\"speedSetpoint\":72")!=std::string::npos && status.find("\"speedAdjustable\":true")!=std::string::npos);
    assert(api(speed,speedRuntime.protocol,speedRuntime.loggerSettings,"/api/settings",{{"azSpeedPercent","40"}},80).status==200);
    assert(speed.runtimeSpeed(0)==40 && speed.runtimeSpeed(1)==12.75f && !speed.axes[0].direction);
    assert(speedStorage.saveForkSettings(speedRuntime));
    assert(speedStorage.serviceRuntimeSpeeds(speedRuntime,81,true)); // Controlled restart flushes resets too.
    speedStorage.loadForkSettings(reloaded);
    assert(reloaded.controller.runtimeSpeed(0)==40 && reloaded.controller.runtimeSpeed(1)==12.75f);
    speed.motionSettings[1].velocity=.001f;
    assert(api(speed,speedRuntime.protocol,speedRuntime.loggerSettings,"/api/speed",{{"axis","el"},{"value","0.00005"}},80).status==200);
    assert(speed.runtimeSpeed(1)==.001f/20);
    assert(speed.setRuntimeSpeed(1,.000049f,80)==Result::Invalid);
    const auto tinyStatus=stateJson(speed,speedRuntime.protocol,speedRuntime.loggerSettings,80);
    assert(tinyStatus.find("\"speedSetpoint\":5e-05")!=std::string::npos);
    assert(tinyStatus.find("\"speedLimit\":0.001")!=std::string::npos);
    assert(speedStorage.saveForkSettings(speedRuntime));
    Preferences::failedBytesKey="values1";
    assert(!speedStorage.serviceRuntimeSpeeds(speedRuntime,40080));
    assert(speed.runtimeSpeeds.dirty);
    Preferences::failedBytesKey.clear();
    assert(speedStorage.serviceRuntimeSpeeds(speedRuntime,40081));
    assert(speed.runtimeSpeeds.dirty); // A failed write is retried after 40 s.
    assert(speedStorage.serviceRuntimeSpeeds(speedRuntime,80080));
    speedStorage.loadForkSettings(reloaded);
    assert(reloaded.controller.runtimeSpeed(1)==.001f/20);
    const auto opened=Preferences::opened.size();
    assert(speed.setRuntimeSpeed(1,.001f/20,80081)==Result::Ok);
    assert(speedStorage.serviceRuntimeSpeeds(speedRuntime,120081));
    assert(Preferences::opened.size()==opened); // Identical values and clean loops never open NVS.
    assert(speed.setRuntimeSpeed(0,25,UINT32_MAX-100)==Result::Ok);
    assert(speedStorage.serviceRuntimeSpeeds(speedRuntime,39898));
    assert(speed.runtimeSpeeds.dirty);
    assert(speedStorage.serviceRuntimeSpeeds(speedRuntime,39899)); // millis() rollover.
    assert(!speed.runtimeSpeeds.dirty);
    speed.setModel(Model::G800DXA);speedStorage.loadMotionSettings(speedRuntime);
    assert(speed.runtimeSpeed(0)==speed.speeds[0].speedPercent); // No override from another model.
    speed.setModel(Model::G5500);speedStorage.loadMotionSettings(speedRuntime);
    assert(speed.runtimeSpeed(0)==25 && speed.runtimeSpeed(1)==.001f/20);
    auto &sliderRecord=std::get<Preferences::Bytes>(Preferences::database["forkspeed"]["values1"]);
    sliderRecord.back()^=1;speedStorage.loadForkSettings(reloaded);
    assert(reloaded.controller.runtimeSpeed(0)==40); // Damaged override falls back to regulator tuning.
    hardware.simulation=false;hardware.pins.azSpeedEnable=-1;speed.configureHardware(hardware);
    assert(!speed.runtimeSpeedAvailable(0));assert(api(speed,speedRuntime.protocol,speedRuntime.loggerSettings,"/api/speed",{{"axis","az"},{"value","20"}},80).status!=200);
    Preferences::database.clear();
  }
  Preferences::database.clear(); Storage storage; Runtime runtime;
  HardwareConfig hw;hw.drive=DRIVE_BTS7960;hw.simulation=false;runtime.controller.configureHardware(hw);
  runtime.controller.feedback(10,30,true,true);storage.loadForkSettings(runtime);
  assert(runtime.controller.motionSettings[0].ready&&runtime.controller.motionSettings[1].ready);
  assert(runtime.controller.speeds[0].speedPercent==50&&runtime.controller.speeds[1].speedPercent==50);
  assert(runtime.controller.motionSettings[0].mode==RegulatorMode::Simple);
  auto &c=runtime.controller;auto &p=runtime.protocol;auto &s=runtime.loggerSettings;
  assert(api(c,p,s,"/api/target",{{"az","90"}},0).status==200);
  c.stop();
  // The removed checkbox cannot disable default motion through a stale client.
  assert(api(c,p,s,"/api/settings",{{"azMotionReady","false"}},0).status==200);
  assert(c.motionSettings[0].ready);
  assert(api(c,p,s,"/api/settings",{{"azMotionReady","true"},{"azMotionKickEnabled","true"},{"azMotionKickPercent","50"},{"azSpeedPercent","5"}},0).status==200);
  assert(c.motionSettings[0].ready&&c.motionSettings[0].kickEnabled&&c.speedPercent==5);
  assert(api(c,p,s,"/api/target",{{"az","90"}},0).status==200);c.tick(0);assert(c.axes[0].speedOutputPercent==5);
  assert(api(c,p,s,"/api/settings",{{"azMotionVelocity","2.5"},{"azRegulatorMode","cascade"},{"azMotionHoldEnabled","true"},{"azMotionSustainingPercent","16"},{"azMotionPositionTolerance","4"},{"azMotionHoldResumeDegrees","1"},{"azMotionHoldReverseMs","0"}},20).status==200);
  assert(c.motionSettings[0].holdReverseMs==0);
  assert(!c.axes[0].direction&&!c.axes[0].tracking&&!c.axes[0].holdArmed);assert(c.motionSettings[0].mode==RegulatorMode::Cascade);
  const auto before=c.motionSettings;
  for(const auto &bad:{Form{{"azRegulatorMode","pid"}},Form{{"azMotionSustainingPercent","101"}},Form{{"azMotionSustainingPercent","16.5"}},Form{{"azMotionOutputLimitPercent","10"},{"azMotionKickEnabled","false"}},Form{{"azMotionVelocity","nan"}},Form{{"azMotionReady","true"},{"azMotionKickPercent","101"}},Form{{"azMotionHoldResumeDegrees","0"}},Form{{"azMotionHoldReverseMs","-1"}},Form{{"azMotionHoldReverseMs","2001"}},Form{{"azMotionHoldReverseMs","0.5"}},Form{{"azMotionFeedbackTimeoutMs","100"},{"azMotionVelocityWindowMs","600"}}}) {
    assert(api(c,p,s,"/api/settings",bad,20).status==400);assert(c.motionSettings==before);
  }
  assert(storage.saveForkSettings(runtime));
  assert(api(c,p,s,"/api/settings",{{"model","G800DXA"},{"azMotionReady","true"}},20).status==400);
  assert(c.model==Model::G5500);
  c.setModel(Model::G800DXA);storage.loadMotionSettings(runtime);
  assert(c.motionSettings[0].ready&&c.motionSettings[0].mode==RegulatorMode::Simple);
  c.motionSettings[0].ready=true;c.motionSettings[0].velocity=1;assert(storage.saveForkSettings(runtime));
  c.setModel(Model::G5500);storage.loadMotionSettings(runtime);
  assert(c.motionSettings==before&&c.speedPercent==5&&!c.axes[0].holdArmed&&!c.moving());
  assert(storage.saveForkSettings(runtime));
  Runtime restored;restored.controller.hardware=c.hardware;storage.loadForkSettings(restored);
  assert(restored.controller.motionSettings==c.motionSettings);assert(!restored.controller.axes[0].holdArmed&&!restored.controller.moving());
  // Other interfaces/models start independently with their working defaults.
  Runtime other;other.controller.hardware=c.hardware;other.controller.hardware.drive=DRIVE_SEPARATE_PWM;storage.loadForkSettings(other);assert(other.controller.motionSettings[0].ready);
  other.controller.hardware=c.hardware;other.controller.hardware.simulation=true;storage.loadForkSettings(other);assert(other.controller.motionSettings[0].ready);
  // Existing checked/unchecked records both load without a commissioning gate.
  MotionSettings legacy;legacy.ready=false;MotionSettings migrated;
  assert(decodeMotionSettings(encodeMotionSettings(legacy),migrated)&&migrated.ready);
  // Failed verified write is reported and a damaged record cannot enable motion.
  {
    Runtime tuning;auto &tc=tuning.controller;
    tc.hardware.drive=DRIVE_SEPARATE_PWM;tc.hardware.simulation=false;
    tc.feedbackAt(0,90,true,0);tc.feedbackAt(1,90,true,0);
    tc.speeds[0].speedPercent=30;tc.speeds[0].coastDegrees=1;
    tc.motionSettings[0].movementDegrees=.1f;tc.motionSettings[0].kickEnabled=true;
    const auto savedMotion=tc.motionSettings[0];const auto savedSpeed=tc.speeds[0];
    assert(tc.startAutoTune(0,RegulatorMode::Simple,60,50,0)==Result::Ok);
    for(unsigned at=20;at<=700;at+=20){tc.feedbackAt(0,90,true,at);tc.feedbackAt(1,90,true,at);tc.tick(at);}
    assert(tc.autoTune.active() && tc.speeds[0].speedPercent!=savedSpeed.speedPercent);
    assert(storage.saveForkSettings(tuning));
    Runtime loaded;loaded.controller.hardware=tc.hardware;storage.loadForkSettings(loaded);
    assert(loaded.controller.motionSettings[0]==savedMotion && loaded.controller.speeds[0]==savedSpeed);
    assert(!loaded.controller.autoTune.active() && !loaded.controller.moving());
    tc.cancelAutoTune(700);
  }
  Preferences::failedBytesKey="r200"; c.motionSettings[0].velocity=2;assert(!storage.saveForkSettings(runtime));Preferences::failedBytesKey.clear();
  auto &record=std::get<Preferences::Bytes>(Preferences::database["forkmotion"]["r200"]);record.back()^=1;
  storage.loadForkSettings(restored);assert(!restored.controller.motionSettings[0].ready&&!restored.controller.motionSettings[0].holdEnabled);
  char data[4096];JsonWriter j(data,sizeof data);assert(writeMotionConfiguration(j,c));assert(std::string(data).find("\"Velocity\":2")!=std::string::npos);
  c.motionSettings[0].ready=true; c.axes[0].motionError=MotionError::KickTimeout;
  assert(api(c,p,s,"/api/motion/reset",{{"axis","az"}},30).status==200);assert(c.axes[0].motionError==MotionError::None&&!c.axes[0].holdArmed&&!c.axes[0].direction);
  assert(api(c,p,s,"/api/motion/reset",{{"axis","bad"}},30).status==400);
  std::cout<<"PASS: motion API validation/STOP, commissioning, separate profiles, verified NVS, corruption and reset\n";
}
