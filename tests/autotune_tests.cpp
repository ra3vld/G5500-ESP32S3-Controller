#include "control.hpp"
#include "motion_json.hpp"
#include "api.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace forkrot;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x); } while(0)
struct Rig {
  Controller c;
  uint32_t now=0;
  float position=90,velocity=0;
  float positiveStart=30,negativeStart=35,sustaining=10,gain=.2f,bias=8;
  float negativeSustaining=0,negativeGain=1,quantum=0;
  bool jam=false;
  explicit Rig(bool cascade=false,unsigned axis=0,uint32_t stamp=0):now(stamp),axis(axis) {
    c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.feedbackAt(0,90,true,now);c.feedbackAt(1,90,true,now);
    auto &m=c.motionSettings[axis];m.mode=cascade?RegulatorMode::Cascade:RegulatorMode::Simple;
    m.movementDegrees=.1f;m.stoppedVelocity=.05f;m.outputLimitPercent=60;m.positionTolerance=.5f;
    c.speeds[axis].speedPercent=20;c.speeds[axis].coastDegrees=1;
  }
  unsigned axis;
  void step(bool fresh=true) {
    const auto &a=c.axes[axis];const float u=a.speedOutputPercent;
    const float start=a.direction<0?negativeStart:positiveStart;
    const float minimum=a.direction<0 && negativeSustaining>0?negativeSustaining:sustaining;
    float desired=a.direction*(u>=minimum?gain*(a.direction<0?negativeGain:1)*(u-bias):0);
    if(jam || (std::abs(velocity)<.01f && u+.001f<start))desired=0;
    velocity+=(desired-velocity)*(.02f/.12f);
    if(std::abs(velocity)<.003f && desired==0)velocity=0;
    position+=velocity*.02f;now+=20;
    if(fresh){c.feedbackAt(axis,quantum>0?std::round(position/quantum)*quantum:position,true,now);c.feedbackAt(1-axis,90,true,now);}
    c.tick(now);
  }
  Result start(float travel=60){return c.startAutoTune(axis,c.motionSettings[axis].mode,travel,50,now);}
};
static void completed(bool cascade,unsigned axis,uint32_t stamp=0) {
  Rig r(cascade,axis,stamp);const auto original=r.c.motionSettings[axis];const auto speed=r.c.speeds[axis];
  CHECK(r.start()==Result::Ok);
  CHECK(r.c.target({100,100,true,true},true,r.now)==Result::Busy);
  CHECK(r.c.manualDrive(1,1,1,1,1,1,0,false,r.now)==Result::Busy);
  CHECK(r.c.jog(1-axis,1,false,r.now)==Result::Busy);
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n){r.step();CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);CHECK(!r.c.axes[1-axis].direction);}
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"tune "<<cascade<<" axis "<<axis<<" phase "<<Controller::autoTunePhaseName(r.c.autoTune.phase)<<" reason "<<r.c.autoTune.reason<<" motion "<<r.c.fault<<" pos "<<r.position<<" low0 "<<r.c.autoTune.measurements[0].lowVelocity<<" high0 "<<r.c.autoTune.measurements[0].highVelocity<<" low1 "<<r.c.autoTune.measurements[1].lowVelocity<<" high1 "<<r.c.autoTune.measurements[1].highVelocity<<" error "<<r.c.autoTune.validationError<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.autoTune.proposedMotion.valid());
  CHECK(r.c.motionSettings[axis]==original);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());CHECK(!r.c.axes[axis].holdArmed);
  CHECK(r.c.autoTune.measurements[0].startOutput>20);CHECK(r.c.autoTune.measurements[1].startOutput>20);
  CHECK(r.c.autoTune.proposedMotion.kickPercent<=50);CHECK(!r.c.autoTune.proposedMotion.holdEnabled);
  if(cascade){CHECK(r.c.autoTune.proposedMotion.velocityKp>0);CHECK(r.c.autoTune.proposedMotion.velocityKi>0);CHECK(r.c.autoTune.proposedMotion.positionKp>0);}
  char out[4096];JsonWriter j(out,sizeof out);CHECK(writeAutoTune(j,r.c,true));CHECK(std::string(j.data()).find("measurements")!=std::string::npos);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id+1,r.now)==Result::Unavailable);
  const auto proposed=r.c.autoTune.proposedMotion;CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Ok);
  CHECK(r.c.motionSettings[axis]==proposed);CHECK(!r.c.moving());CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
}
static void safety() {
  Rig r;const auto m=r.c.motionSettings[0];const auto s=r.c.speeds[0];CHECK(r.start()==Result::Ok);
  for(unsigned n=0;n<65;++n)r.step();
  r.c.stopNow(-1,r.now);
  CHECK(r.c.autoTune.phase==AutoTunePhase::Cancelled);CHECK(!r.c.moving());CHECK(r.c.motionSettings[0]==m);CHECK(r.c.speeds[0]==s);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
  r=Rig();r.jam=true;CHECK(r.start()==Result::Ok);for(unsigned n=0;n<300 && r.c.autoTune.active();++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(!r.c.moving());CHECK(r.c.motionSettings[0]==m);
  r=Rig();CHECK(r.start()==Result::Ok);for(unsigned n=0;n<40;++n)r.step();for(unsigned n=0;n<40 && r.c.autoTune.active();++n)r.step(false);
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(!r.c.moving());
  r=Rig();CHECK(r.start()==Result::Ok);r.c.resetFeedback(0,r.now);CHECK(r.c.autoTune.phase==AutoTunePhase::Cancelled);
  r=Rig();CHECK(r.start()==Result::Ok);r.c.setMode(Mode::Auto,r.now);CHECK(r.c.autoTune.phase==AutoTunePhase::Cancelled);
  r=Rig();CHECK(r.start()==Result::Ok);
  for(unsigned at=20;at<=60;at+=20){r.c.feedbackAt(1,120,true,at);r.c.feedbackAt(0,90,true,at);r.c.tick(at);}
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(!r.c.moving());
  r=Rig();r.c.resetFeedback(0,20);r.c.feedbackAt(0,20,true,20);CHECK(r.c.startAutoTune(0,RegulatorMode::Simple,60,50,20)==Result::Invalid);
  r=Rig();CHECK(r.c.startAutoTune(0,RegulatorMode::Simple,60,61,0)==Result::Invalid);
  r.c.motionSettings[0].movementDegrees=5;CHECK(r.c.startAutoTune(0,RegulatorMode::Simple,10,50,0)==Result::Invalid);
  r=Rig();CHECK(r.start()==Result::Ok);r.step();r.now+=300;r.c.feedbackAt(0,90,true,r.now);r.c.tick(r.now);
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(!r.c.moving());
  r=Rig();r.jam=true;r.c.motionSettings[0].kickEnabled=true;r.c.motionSettings[0].kickMs=400;
  CHECK(r.start()==Result::Ok);
  for(unsigned n=0;n<100 && r.c.autoTune.active();++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(r.now<=1020);CHECK(!r.c.moving());
}
static void highStiction() {
  Rig r;r.positiveStart=r.negativeStart=50;r.sustaining=5;r.gain=.134f;r.bias=0;r.c.speeds[0].speedPercent=5;
  CHECK(r.start()==Result::Ok);
  for(unsigned n=0;n<6000&&r.c.autoTune.active();++n)r.step();
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"high stiction "<<r.c.autoTune.reason<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.autoTune.proposedSpeed.speedPercent==5);
  CHECK(r.c.autoTune.proposedMotion.kickPercent==50);
}
static void fastBounded(float travel,bool cascade=false,unsigned axis=0) {
  Rig r(cascade,axis);r.positiveStart=r.negativeStart=28;r.sustaining=16;r.negativeSustaining=18;
  r.gain=3;r.bias=13;r.negativeGain=1.2f;r.quantum=.1f;
  auto &m=r.c.motionSettings[axis];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;
  r.c.speeds[axis].speedPercent=20;r.c.speeds[axis].coastDegrees=1.5f;
  const auto saved=m;const auto speed=r.c.speeds[axis];
  CHECK(r.start(travel)==Result::Ok);bool relocated=false;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    r.step();relocated|=r.c.autoTune.phase==AutoTunePhase::Reposition;
    CHECK(r.position>=90-travel-.2f && r.position<=90+travel+.2f);
    CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
  }
  if(r.c.autoTune.phase!=AutoTunePhase::Complete && travel!=10)std::cerr<<"fast "<<travel<<" cascade "<<cascade<<" reason "<<r.c.autoTune.reason<<" phase "<<Controller::autoTunePhaseName(r.c.autoTune.phase)<<" position "<<r.position<<" level "<<r.c.autoTune.level<<" low0 "<<r.c.autoTune.measurements[0].lowVelocity<<" high0 "<<r.c.autoTune.measurements[0].highVelocity<<" low1 "<<r.c.autoTune.measurements[1].lowVelocity<<" high1 "<<r.c.autoTune.measurements[1].highVelocity<<" error "<<r.c.autoTune.validationError<<'\n';
  if(travel==10) {
    CHECK(r.c.autoTune.phase==AutoTunePhase::Complete ||
          (r.c.autoTune.phase==AutoTunePhase::Failed && std::string(r.c.autoTune.reason)=="insufficient_travel"));
    CHECK(r.c.motionSettings[axis]==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());return;
  }
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.motionSettings[axis]==saved);CHECK(r.c.speeds[axis]==speed);
  CHECK(r.c.autoTune.proposedSpeed.speedPercent>=18);CHECK(!r.c.moving());
  if(cascade) {
    CHECK(r.c.autoTune.proposedMotion.velocity<=saved.velocity);
    CHECK(r.c.autoTune.proposedMotion.positionTolerance>=r.c.autoTune.proposedSpeed.coastDegrees);
  }
  if(travel<=20)CHECK(relocated);
}
static void transport() {
  Rig r;Protocol p(r.c);Settings settings;
  Form start{{"action","start"},{"axis","az"},{"mode","simple"},{"travel","60"},{"outputLimit","50"},{"id","0"}};
  CHECK(api(r.c,p,settings,"/api/motion/autotune",start,0).status==200);
  const auto id=r.c.autoTune.id;CHECK(id!=0);
  CHECK(api(r.c,p,settings,"/api/motion/autotune",start,0).status!=200);
  CHECK(r.c.autoTune.id==id);
  CHECK(api(r.c,p,settings,"/api/settings",{{"speedPercent","90"}},0).status==503);
  CHECK(api(r.c,p,settings,"/api/target",{{"az","100"}},0).status==503);
  const auto json=stateJson(r.c,p,settings,0);
  CHECK(json.find("\"autotune\":{")!=std::string::npos);
  CHECK(json.find("\"speedPercent\":20")!=std::string::npos);
  CHECK(api(r.c,p,settings,"/api/motion/autotune",{{"action","cancel"}},0).status==200);
  CHECK(api(r.c,p,settings,"/api/motion/autotune",start,0).status!=200); // Old POST cannot restart after cancellation.
  start["id"]=std::to_string(id);
  CHECK(api(r.c,p,settings,"/api/motion/autotune",start,0).status==200);
  CHECK(api(r.c,p,settings,"/api/move",{{"d","S"}},0).status==200);
  CHECK(r.c.autoTune.phase==AutoTunePhase::Cancelled);
  CHECK(r.c.speeds[0].speedPercent==20);
}
int main(){try{completed(false,0);completed(true,0);completed(false,1);completed(true,1,0xfffff000u);safety();highStiction();fastBounded(60);fastBounded(20);fastBounded(10);fastBounded(60,true,1);transport();std::cout<<"PASS autotune: "<<checks<<" checks\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
