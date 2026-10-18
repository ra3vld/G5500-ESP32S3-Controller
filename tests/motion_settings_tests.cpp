#include "storage.hpp"
#include "motion_json.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace forkrot;
int main(){
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
  assert(api(c,p,s,"/api/target",{{"az","90"}},0).status==200);c.tick(0);assert(c.axes[0].speedOutputPercent==50);
  assert(api(c,p,s,"/api/settings",{{"azMotionVelocity","2.5"},{"azRegulatorMode","cascade"},{"azMotionHoldEnabled","true"}},20).status==200);
  assert(!c.axes[0].direction&&!c.axes[0].tracking&&!c.axes[0].holdArmed);assert(c.motionSettings[0].mode==RegulatorMode::Cascade);
  const auto before=c.motionSettings;
  for(const auto &bad:{Form{{"azRegulatorMode","pid"}},Form{{"azMotionVelocity","nan"}},Form{{"azMotionReady","true"},{"azMotionKickPercent","101"}},Form{{"azMotionPositionTolerance","2"},{"azMotionHoldResumeDegrees","1"}},Form{{"azMotionFeedbackTimeoutMs","100"},{"azMotionVelocityWindowMs","600"}}}) {
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
