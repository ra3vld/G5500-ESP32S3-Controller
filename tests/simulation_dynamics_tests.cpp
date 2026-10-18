#include "motion_fixture.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
namespace forkrot::crowpanel {
bool beginBus() {
  return true;
}
} // namespace forkrot::crowpanel
using namespace forkrot;
static bool close(float a, float b) {
  return std::abs(a - b) < .002f;
}
int main() {
  static_assert(DRIVE_SIMULATION);
  Controller c = forkrot::testController();
  c.feedback(100, 90, true, true);
  c.speeds[0] = {80, 20, 10, 1000, 500, 10};
  c.tick(0);
  assert(c.variableSpeed(0)&&c.variableSpeed(1));
  auto jog=[&](unsigned seq,float scale,unsigned at){return c.manualDrive(1,1,1,seq,1,scale,0,false,at);};
  assert(jog(1,.5f,0)==Result::Ok);c.tick(0);assert(c.axes[0].speedOutputPercent>0&&c.axes[0].speedOutputPercent<1);
  for(unsigned at=20;at<=800;at+=20)c.tick(at);
  assert(close(c.axes[0].speedOutputPercent,40)&&c.axes[0].current>100);
  assert(jog(2,.25f,800)==Result::Ok);
  float previous=c.axes[0].speedOutputPercent;
  for(unsigned at=820;at<=1400;at+=20){c.tick(at);assert(c.axes[0].speedOutputPercent<=previous+.002f);previous=c.axes[0].speedOutputPercent;}
  assert(close(c.axes[0].speedOutputPercent,20));
  assert(jog(3,-1,1400)==Result::Ok);unsigned reverseAt=0;
  for(unsigned at=1420;at<=2200;at+=20){c.tick(at);if(c.axes[0].direction==-1){reverseAt=at;break;}}
  assert(reverseAt&&uint32_t(reverseAt-c.axes[0].offAt)>=300);
  const float position=c.axes[0].current;
  for(unsigned at=reverseAt+20;at<=reverseAt+100;at+=20)c.tick(at);
  assert(c.axes[0].current<position);c.stopNow(-1,reverseAt+100);const float stopped=c.axes[0].current;
  c.tick(reverseAt+200);assert(close(c.axes[0].current,stopped));assert(c.speeds[0].speedPercent==80);
  Controller axes=forkrot::testController();axes.feedback(100,90,true,true);
  axes.speeds[0]={80,80,80,100,100,1};axes.speeds[1]={40,40,40,100,100,1};
  for(auto &m:axes.motionSettings){m.kickEnabled=true;m.kickPercent=100;}
  axes.target({200,170,true,true},false,0);
  for(unsigned at=0;at<=1000;at+=20)axes.tick(at);
  assert(axes.axes[0].current-100>axes.axes[1].current-90&&axes.axes[1].current>90);
  assert(close(axes.axes[0].speedOutputPercent,80)&&close(axes.axes[1].speedOutputPercent,40));
  axes.stop();axes.feedback(199,169,true,true);axes.motionSettings[0].kickEnabled=false;
  axes.speeds[0].approachSpeedPercent=10;axes.speeds[0].slowdownDegrees=10;
  axes.target({200,0,true,false},false,1100);axes.tick(1100);assert(axes.axes[0].speedOutputPercent<20);
  for(unsigned at=1120;at<=6000;at+=20)axes.tick(at);
  assert(close(axes.axes[0].current,200)&&!axes.axes[0].tracking);
  // Virtual PWM settings migrate from the legacy AZ defaults and persist per axis.
  Preferences::database.clear();
  Storage storage;
  MotionTestRuntime runtime;
  storage.loadForkSettings(runtime);
  runtime.controller.speeds[0] = {12, 4, 3, 1000, 500, 5};
  runtime.controller.speeds[1] = {35, 7, 4, 1200, 700, 8};
  storage.saveForkSettings(runtime);
  MotionTestRuntime loaded;
  storage.loadForkSettings(loaded);
  assert(loaded.controller.speeds[0].speedPercent == 12 &&
         loaded.controller.speeds[0].startSpeedPercent == 4);
  assert(loaded.controller.speeds[1].speedPercent == 35 &&
         loaded.controller.speeds[1].accelerationMs == 1200);
  auto result = api(loaded.controller, loaded.protocol, loaded.loggerSettings, "/api/settings",
                    {{"azSpeedPercent", "1"}, {"elSpeedPercent", "2"}}, 0);
  assert(result.status == 200);
  Hardware hw(loaded);
  hw.beginDirections();
  hw.beginAdc();
  hw.applyOutputs();
  assert(!fakeHardware.readsRaw && !fakeHardware.readsMv && !fakeHardware.attachCalls);
  std::cout
      << "PASS: simulated independent speeds, acceleration/deceleration/reverse, approach, STOP and per-axis NVS/API; no motor output\n";
}
