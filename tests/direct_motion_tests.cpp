#include "control.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace forkrot;
Controller rig(){
 Controller c;auto hw=c.hardware;hw.drive=DRIVE_BTS7960;hw.simulation=0;c.configureHardware(hw);
 c.model=Model::G5500;c.restoreMode(Mode::Auto);c.feedback(90,90,true,true);
 c.speeds[0].speedPercent=30;c.speeds[0].coastDegrees=.5f;
 c.motionSettings[0].outputLimitPercent=40;return c;
}
void feed(Controller &c,float az,uint32_t now){c.feedbackAt(0,az,true,now);c.feedbackAt(1,90,true,now);c.tick(now);}
int main(){
 {auto c=rig();feed(c,90,1000);assert(c.setRuntimeSpeed(0,20,1000)==Result::Ok);
  assert(c.jog(0,1,false,1000)==Result::Ok);feed(c,90,1000);
  const auto settings=c.motionSettings[0];const auto epoch=c.stopEpoch;
  assert(c.configureMotion(0,settings,1001));
  assert(c.stopEpoch==epoch&&c.axes[0].direction==1&&c.axes[0].speedOutputPercent==20);
  auto changed=settings;changed.outputLimitPercent=25;
  assert(c.configureMotion(0,changed,1002));assert(c.stopEpoch!=epoch&&!c.moving());
 }
 {auto c=rig();feed(c,90,1000);assert(c.jog(0,1,false,1000)==Result::Ok);feed(c,90,1000);
  assert(c.axes[0].direction==1&&c.axes[0].speedOutputPercent==30);
  c.stopNow(-1,1001);assert(!c.moving()&&c.axes[0].speedOutputPercent==0);
  assert(c.jog(0,-1,false,1002)==Result::Ok);feed(c,90,1002);assert(!c.axes[0].direction);
  feed(c,90,1299);assert(!c.axes[0].direction);feed(c,90,1330);assert(c.axes[0].direction==-1);
 }
 {auto c=rig();feed(c,90,1000);assert(c.target({91,0,true,false},false,1000)==Result::Ok);feed(c,90,1000);
  assert(c.axes[0].speedOutputPercent==30);feed(c,90.6f,1020);assert(!c.moving()&&c.axes[0].completed);
 }
 {auto c=rig();feed(c,c.azRange.end,1000);assert(c.jog(0,1,false,1000)==Result::Ok);feed(c,c.azRange.end,1000);assert(!c.moving());}
 {auto c=rig();feed(c,90,1000);assert(c.jog(0,1,false,1000)==Result::Ok);feed(c,90,1000);c.tick(1600);
  assert(!c.moving()&&c.axes[0].motionError==MotionError::FeedbackStale);}
 {auto c=rig();feed(c,90,1000);assert(c.jog(0,1,false,1000)==Result::Ok);feed(c,90,1000);
  c.feedbackAt(0,NAN,false,1010);assert(!c.moving()&&c.axes[0].motionError==MotionError::FeedbackInvalid);}
 {auto c=rig();feed(c,90,1000);assert(c.jog(0,1,false,1000)==Result::Ok);
  for(unsigned t=1000;t<=4100;t+=20)feed(c,90,t);
  assert(!c.moving()&&c.axes[0].motionError==MotionError::Stalled);}
 {auto c=rig();feed(c,90,1000);assert(c.startAutoTune(0,RegulatorMode::Cascade,20,30,1000)==Result::Unavailable);assert(!c.autoTune.active());
  auto m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.kickEnabled=true;m.holdEnabled=true;m.kickPercent=30;
  assert(c.configureMotion(0,m,1000));assert(c.motionSettings[0].mode==RegulatorMode::Simple&&!c.motionSettings[0].holdEnabled&&!c.motionSettings[0].kickEnabled);
 }
 {auto c=rig();feed(c,90,1000);
  assert(c.manualDrive(1,2,1,1,1,1,0,false,1000)==Result::Ok);
  feed(c,90,1000);assert(c.axes[0].speedOutputPercent==30);
  for(unsigned t=1020;t<2000;t+=20)feed(c,90,t);
  assert(c.axes[0].direction==1);feed(c,90,2000);assert(!c.moving());
  assert(c.manualDrive(1,2,1,2,1,1,0,false,2001)!=Result::Ok);
 }
 std::cout<<"PASS direct PWM, STOP, reverse interlock, target/limits, feedback/stall and disabled autotune\n";
}
