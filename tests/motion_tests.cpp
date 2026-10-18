#include "api.hpp"
#include "motion_json.hpp"
#include "crowpanel_knob.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace forkrot;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::to_string(__LINE__)+": " #x); } while(0)
static Controller ready(bool cascade=false,float position=10) {
  Controller c; c.hardware.simulation=false; c.hardware.drive=DRIVE_BTS7960;
  for(auto &m:c.motionSettings) { m.ready=true; m.mode=cascade?RegulatorMode::Cascade:RegulatorMode::Simple; m.movementDegrees=.02f; m.stoppedVelocity=.02f; }
  c.feedbackAt(0,position,true,0); c.feedbackAt(1,30,true,0); c.setMode(Mode::Auto,0); return c;
}
struct Plant {
  float position=10, velocity=0;
  void step(const Axis &axis) {
    const float u=axis.speedOutputPercent;
    float desired=axis.direction*(u>=5 ? .134f*u : 0);
    if(std::abs(velocity)<.005f && u<50) desired=0;
    velocity+=(desired-velocity)*(.02f/.12f);
    if(std::abs(velocity)<.003f && !desired) velocity=0;
    position+=velocity*.02f;
  }
};
static void records() {
  MotionSettings m; CHECK(m.valid()); const auto b=encodeMotionSettings(m); MotionSettings next; CHECK(decodeMotionSettings(b,next)); CHECK(next==m);
  for(unsigned i=0;i<b.size();++i) { auto bad=b;bad[i]^=0x80;CHECK(!decodeMotionSettings(bad,next)); }
  m.kickPercent=101;CHECK(!m.valid()); m.kickPercent=50;m.holdResumeDegrees=.1f;CHECK(!m.valid());
  Controller c; c.hardware.drive=DRIVE_BTS7960;c.hardware.simulation=false;c.feedback(10,30,true,true);
  CHECK(c.target({20,0,true,false},false,0)==Result::Ok);
  CHECK(c.motionSettings[0].movementDegrees==5&&c.speeds[0].speedPercent==50);
  MotionPid pid;for(int i=0;i<1000;++i) CHECK(pid.step(10,0,0,5,10,0,.02f,.1f,0,5)==5);
  CHECK(std::abs(pid.integral)<.001f); CHECK(pid.step(-1,0,0,5,10,0,.02f,.1f,0,5)==0);
}
static void kickAndStream() {
  auto c=ready(); auto &m=c.motionSettings[0];m.kickEnabled=true;m.kickPercent=50;m.kickMs=500;
  c.speeds[0].speedPercent=5;c.speeds[0].approachSpeedPercent=5;c.speeds[0].coastDegrees=.1f;
  CHECK(c.target({50,0,true,false},true,0)==Result::Ok);c.tick(0);CHECK(c.axes[0].motionPhase==MotionPhase::Kick);CHECK(c.axes[0].speedOutputPercent==50);
  Plant plant;unsigned starts=1;auto phase=c.axes[0].motionPhase;
  for(unsigned now=20;now<=4000;now+=20) {
    plant.step(c.axes[0]);c.feedbackAt(0,plant.position,true,now);c.feedbackAt(1,30,true,now);
    for(unsigned j=0;j<4 && now<=1000;++j) CHECK(c.target({50+float((now/20-1)*4+j+1)*.1f,0,true,false},true,now)==Result::Ok);
    c.tick(now); if(phase!=MotionPhase::Kick&&c.axes[0].motionPhase==MotionPhase::Kick) ++starts;phase=c.axes[0].motionPhase;
    CHECK(c.axes[0].motionError==MotionError::None);
  }
  CHECK(starts==1);CHECK(std::abs(c.axes[0].target-70)<.001f);CHECK(plant.position>11);CHECK(c.axes[0].speedOutputPercent<=5.001f);
  c.stopNow(-1,4000);CHECK(!c.axes[0].direction&&!c.axes[0].holdArmed);
  CHECK(c.target({70,0,true,false},true,4020)==Result::Ok);c.tick(4020);CHECK(c.axes[0].direction);
  auto stuck=ready();stuck.motionSettings[0].kickEnabled=true;stuck.motionSettings[0].kickMs=100;
  stuck.target({90,0,true,false},true,0);stuck.tick(0);
  for(unsigned now=20;now<=120;now+=20) { stuck.feedbackAt(0,10,true,now);stuck.target({90+now*.001f,0,true,false},true,now);stuck.tick(now); }
  CHECK(stuck.axes[0].motionError==MotionError::KickTimeout);CHECK(!stuck.axes[0].direction);
  CHECK(stuck.target({180,0,true,false},true,200)==Result::Unavailable);
  CHECK(stuck.resetMotion(0,200)==Result::Ok);CHECK(stuck.target({180,0,true,false},true,200)==Result::Ok);
}
static void cascadeHold() {
  auto c=ready(true);c.motionSettings[0].kickEnabled=true;c.motionSettings[0].holdEnabled=true;
  c.motionSettings[0].positionTolerance=.15f;c.motionSettings[0].holdResumeDegrees=.4f;
  CHECK(c.target({20,0,true,false},true,0)==Result::Ok);c.tick(0);Plant p;
  bool holding=false;unsigned arrivedAt=0;
  for(unsigned now=20;now<=30000;now+=20) {
    p.step(c.axes[0]);c.feedbackAt(0,p.position,true,now);c.feedbackAt(1,30,true,now);c.tick(now);
    if(c.axes[0].motionError!=MotionError::None) throw std::runtime_error(std::string("cascade ")+motionErrorName(c.axes[0].motionError));
    if(c.axes[0].motionPhase==MotionPhase::Holding) { holding=true;arrivedAt=now;break; }
  }
  std::cout<<"hold position="<<p.position<<" velocity="<<p.velocity<<" output="<<c.axes[0].speedOutputPercent<<"\n";
  CHECK(holding);CHECK(std::abs(p.position-20)<=.15f);CHECK(c.axes[0].holdArmed);
  auto detent=c;detent.axes[0].current=20.1f;
  CrowPanelKnob detentKnob(detent);detentKnob.sample(true,false,0,arrivedAt+100);
  detentKnob.sample(true,false,-1,arrivedAt+140);CHECK(detent.axes[0].target==40);
  CrowPanelKnob knob(c);knob.confirm(arrivedAt+20);CHECK(!c.axes[0].holdArmed&&!c.moving());
  knob.confirm(arrivedAt+40);CHECK(c.axes[0].holdArmed&&c.axes[0].tracking);
  c.feedbackAt(0,p.position+1,true,30020);c.tick(30020);CHECK(c.axes[0].tracking);
  c.stopNow(0,30040);CHECK(!c.axes[0].holdArmed);CHECK(!c.axes[0].direction);
  CHECK(c.target({20,0,true,false},true,30060)==Result::Ok);
}
static void profilesAndNoise() {
  auto c=ready();c.speeds[0].speedPercent=60;c.speeds[0].accelerationMs=1000;c.speeds[0].decelerationMs=1000;
  CHECK(c.jog(0,1,false,0)==Result::Ok);float last=0,lastRate=0;std::array<float,61> rates{};
  for(unsigned at=0;at<=1200;at+=20) {
    c.feedbackAt(0,10,true,at);c.tick(at);const float output=c.axes[0].speedOutputPercent;
    const float rate=(output-last)/.02f;CHECK(output>=last-.001f&&output<=60.001f);CHECK(std::abs(rate-lastRate)<=4.9f);
    rates[at/20]=rate;last=output;lastRate=rate;
  }
  CHECK(rates[5]>rates[0]&&rates[48]<rates[25]);CHECK(std::abs(last-60)<.001f);
  c=ready(true,20);float acceleration=0;
  for(unsigned at=20;at<=1000;at+=20) {
    c.feedbackAt(0,20,true,at);
    for(unsigned n=0;n<4;++n) { const float goal=20+.1f*float((at/20-1)*4+n+1);CHECK(c.target({goal,0,true,false},true,at)==Result::Ok);CHECK(c.axes[0].target==goal); }
    c.tick(at);CHECK(c.axes[0].motionError==MotionError::None);
    CHECK(std::abs(c.axes[0].requestedVelocity)<=3.001f);CHECK(std::abs(c.axes[0].profileAcceleration)<=2.001f);
    CHECK(std::abs(c.axes[0].profileAcceleration-acceleration)<=.161f);acceleration=c.axes[0].profileAcceleration;
  }
  CHECK(std::abs(c.axes[0].target-40)<.001f);
  MotionFeedback quiet,moving;
  for(unsigned at=0;at<=2000;at+=20) {
    const float noise=(at/20)%2?.004f:-.004f;
    quiet.sample(20+noise,true,at,200);
    moving.sample(std::round((20+float(at)/1000)/.05f)*.05f+noise,true,at,200);
  }
  CHECK(std::abs(quiet.velocity)<.03f);CHECK(std::abs(moving.velocity-1)<.2f);
  MotionPid derivative;derivative.step(0,10,0,0,0,2,.02f,.1f,-100,100);
  CHECK(derivative.step(100,10,0,0,0,2,.02f,.1f,-100,100)==0);
  Controller blocked;blocked.hardware.drive=DRIVE_BTS7960;blocked.hardware.simulation=false;
  CHECK(blocked.manualDrive(1,1,1,1,1,1,0,false,0)==Result::Ok);blocked.stop();
  blocked=ready(true);blocked.feedback(10,30,false,true);
  CHECK(blocked.manualDrive(1,1,1,1,1,1,0,false,0)==Result::NotCalibrated);blocked.tick(0);CHECK(!blocked.axes[0].direction);
  auto faults=ready();for(auto &m:faults.motionSettings){m.kickEnabled=true;m.kickMs=100;}
  faults.target({90,90,true,true},false,0);faults.tick(0);
  for(unsigned at=20;at<=100;at+=20){faults.feedbackAt(0,10,true,at);faults.feedbackAt(1,30,true,at);faults.tick(at);}
  CHECK(faults.axes[1].motionError==MotionError::KickTimeout);faults.resetMotion(0,120);
  CHECK(faults.fault=="el_kick_start_timeout");CHECK(faults.target({90,0,true,false},false,120)==Result::Ok);
  CHECK(faults.fault=="el_kick_start_timeout");
}
static void feedbackSafety() {
  for(bool cascade:{false,true}) {
    auto fast=ready(cascade);fast.jog(0,1,false,0);fast.tick(0);
    // Real-drive replay: 3 degrees per fresh 20ms conversion (150 deg/s),
    // while the unused/default cascade set velocity remains only3 deg/s.
    for(unsigned at=20;at<=400;at+=20){fast.feedbackAt(0,10+float(at)*.15f,true,at);fast.tick(at);}
    CHECK(fast.axes[0].calibrated&&fast.axes[0].motionError==MotionError::None);
    CHECK(std::abs(fast.axes[0].measuredVelocity-150)<.1f);
  }
  auto slow=ready();slow.motionSettings[0].movementDegrees=5;
  slow.jog(0,1,false,0);slow.tick(0);
  for(unsigned at=20;at<=4000;at+=20){slow.feedbackAt(0,10+float(at)*.0002f,true,at);slow.tick(at);}
  CHECK(slow.axes[0].motionError==MotionError::None); // 5-degree kick threshold must not declare slow movement stalled
  auto noise=ready();noise.target({90,0,true,false},false,0);noise.tick(0);
  for(unsigned n=0;n<10;++n) noise.feedbackAt(0,40,true,20);
  CHECK(noise.axes[0].motionError==MotionError::None&&noise.axes[0].calibrated);
  CHECK(noise.axes[0].current==10);
  noise.feedbackAt(0,10.02f,true,40);
  CHECK(noise.axes[0].motionError==MotionError::None&&noise.axes[0].current==10.02f);
  for(unsigned stamp=60;stamp<=100;stamp+=20) noise.feedbackAt(0,40,true,stamp);
  CHECK(noise.axes[0].motionError==MotionError::FeedbackInvalid&&!noise.axes[0].direction);

  auto c=ready();c.target({90,0,true,false},false,0);c.tick(0);c.tick(501);CHECK(c.axes[0].motionError==MotionError::FeedbackStale);
  c=ready();c.motionSettings[0].mode=RegulatorMode::Cascade;c.target({90,0,true,false},false,0);c.tick(0);c.feedbackAt(0,NAN,true,20);CHECK(!c.axes[0].direction);CHECK(c.axes[0].motionError==MotionError::FeedbackInvalid);
  MotionFeedback f;for(unsigned n=0;n<40;++n) f.sample(10+n*.02f,true,0xfffffe00u+n*20,200);CHECK(std::abs(f.velocity-1)<.001f);
  const auto velocity=f.velocity;const auto at=f.at;f.sample(200,true,at,200);CHECK(f.at==at&&f.velocity==velocity);
  f.sample(200,false,at+20,200);f.sample(200,true,at+40,200);CHECK(f.velocity==0&&f.count==1);
  c=ready(true,20);c.motionSettings[0].holdEnabled=true;c.target({40,0,true,false},false,0);c.tick(0);
  c.feedbackAt(1,30.02f,true,20);CHECK(c.axes[1].measuredVelocity>0);
  CHECK(c.setRange({50,500},AZ_CUSTOM,20));CHECK(!c.axes[0].holdArmed&&!c.axes[0].direction);
  c.feedbackAt(0,150,true,40);CHECK(c.axes[0].calibrated&&c.axes[0].measuredVelocity==0);
  CHECK(c.axes[1].measuredVelocity>0);CHECK(c.target({160,0,true,false},false,40)==Result::Ok);
  c=ready();c.jog(0,1,false,0);c.tick(0);c.feedbackAt(0,NAN,true,20);
  CHECK(!c.axes[0].direction&&c.axes[0].motionError==MotionError::FeedbackInvalid);
  c=ready();c.feedback(10,30,false,true);CHECK(c.jog(0,1,false,0)==Result::Ok);
  for(unsigned stamp=20;stamp<=2000;stamp+=20){c.feedbackAt(0,10,false,stamp);c.tick(stamp);}
  CHECK(c.axes[0].direction&&c.axes[0].motionError==MotionError::None);
}
int main() { try { records();kickAndStream();cascadeHold();profilesAndNoise();feedbackSafety();std::cout<<"PASS motion: "<<checks<<" checks; Controller bytes="<<sizeof(Controller)<<"\n"; } catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; } }
