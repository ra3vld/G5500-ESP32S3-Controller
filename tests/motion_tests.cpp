#include "api.hpp"
#include "motion_json.hpp"
#include "crowpanel_knob.hpp"
#include "position_filter.hpp"
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
  for(int direction:{-1,1}) {
    MotionPid saturated;
    const float lo=direction<0?-1.f:0.f,hi=direction<0?0.f:1.f;
    CHECK(std::abs(saturated.step(float(direction),0,.25f*direction,0,.6f,0,1,.1f,lo,hi)-.85f*direction)<.0001f);
    const float frozen=saturated.integral;
    for(unsigned n=0;n<100;++n) {
      CHECK(saturated.step(float(direction),0,.25f*direction,0,.6f,0,1,.1f,lo,hi)==direction);
      CHECK(saturated.integral==frozen);
    }
    CHECK(std::abs(saturated.step(float(-direction),0,.25f*direction,0,.6f,0,1,.1f,lo,hi))<.85f);
    CHECK(std::abs(saturated.integral)<.0001f);
  }
}
static void kickAndStream() {
  auto c=ready(); auto &m=c.motionSettings[0];m.kickEnabled=true;m.kickPercent=50;m.kickMs=500;
  c.speeds[0].speedPercent=5;c.speeds[0].approachSpeedPercent=5;c.speeds[0].coastDegrees=.1f;
  CHECK(c.target({50,0,true,false},true,0)==Result::Ok);c.tick(0);CHECK(c.axes[0].motionPhase==MotionPhase::Kick);CHECK(c.axes[0].speedOutputPercent==5);
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
  for(unsigned now=20;now<=3000;now+=20) { stuck.feedbackAt(0,10,true,now);stuck.target({90+now*.001f,0,true,false},true,now);stuck.tick(now); }
  CHECK(stuck.axes[0].motionError==MotionError::KickTimeout);CHECK(!stuck.axes[0].direction);
  CHECK(stuck.target({180,0,true,false},true,3020)==Result::Unavailable);
  CHECK(stuck.resetMotion(0,3020)==Result::Ok);CHECK(stuck.target({180,0,true,false},true,3020)==Result::Ok);
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
  for(unsigned at=20;at<=3000;at+=20){faults.feedbackAt(0,10,true,at);faults.feedbackAt(1,30,true,at);faults.tick(at);}
  CHECK(faults.axes[1].motionError==MotionError::KickTimeout);faults.resetMotion(0,3020);
  CHECK(faults.fault=="el_kick_start_timeout");CHECK(faults.target({90,0,true,false},false,3020)==Result::Ok);
  CHECK(faults.fault=="el_kick_start_timeout");
}
static void feedbackSafety() {
  for(int direction:{-1,1}) {
    auto stopped=ready();CHECK(stopped.jog(0,direction,false,0)==Result::Ok);stopped.tick(0);
    for(unsigned at=20;at<=4200;at+=20) {
      stopped.feedbackAt(0,10+((at/20)%2?.14f:-.14f),true,at);stopped.tick(at);
    }
    CHECK(stopped.axes[0].motionError==MotionError::Stalled);
    CHECK(!stopped.axes[0].direction && stopped.axes[0].speedOutputPercent==0);
  }
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
  CHECK(noise.motionFeedback(0).rejectedTotal==1); // duplicate reads are one ADC event
  CHECK(noise.axes[0].current==10);
  noise.feedbackAt(0,10.02f,true,40);
  CHECK(noise.axes[0].motionError==MotionError::None&&noise.axes[0].current==10.02f);
  for(unsigned stamp=60;stamp<=100;stamp+=20) noise.feedbackAt(0,40,true,stamp);
  CHECK(noise.axes[0].motionError==MotionError::FeedbackInvalid&&!noise.axes[0].direction);
  CHECK(noise.motionFeedback(0).invalidEvents==1);
  CHECK(noise.motionFeedback(0).rejectedTotal==4);
  CHECK(noise.motionFeedback(0).rejectedPosition==40);
  CHECK(noise.motionFeedback(0).lastGoodPosition==10.02f);
  CHECK(noise.motionFeedback(0).rejectedAt==100);
  CHECK(noise.motionFeedback(0).rejectedIntervalMs==60);

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
static void lowOutputProgress() {
  for(bool cascade:{false,true}) for(unsigned axis:{0u,1u}) {
    auto c=ready(cascade);auto &m=c.motionSettings[axis];
    m.kickEnabled=false;m.stallMs=500;m.velocity=1;m.feedforward=6;
    m.velocityKp=m.velocityKi=m.velocityKd=0;
    c.speeds[axis].speedPercent=c.speeds[axis].approachSpeedPercent=6;
    c.speeds[axis].accelerationMs=100;
    CHECK(c.jog(axis,1,false,0)==Result::Ok);c.tick(0);
    for(unsigned at=20;at<=4000;at+=20) {
      c.feedbackAt(0,10,true,at);c.feedbackAt(1,30,true,at);c.tick(at);
      CHECK(c.axes[axis].motionError==MotionError::None);
    }
    CHECK(c.axes[axis].direction && c.axes[axis].speedOutputPercent<7);
    // Restoring meaningful output receives its own full timeout.
    m.feedforward=8;c.speeds[axis].speedPercent=7;
    uint32_t first=0;
    for(unsigned at=4020;at<=7000;at+=20) {
      c.feedbackAt(0,10,true,at);c.feedbackAt(1,30,true,at);c.tick(at);
      if(!first && c.axes[axis].speedOutputPercent>=7-1e-4f)first=at;
      if(c.axes[axis].motionError!=MotionError::None) {
        CHECK(first && at-first>=m.stallMs);break;
      }
    }
    CHECK(c.axes[axis].motionError==MotionError::Stalled);
    // Freshness is independent of output and must still stop the drive.
    c=ready(cascade);c.motionSettings[axis].outputLimitPercent=6;
    CHECK(c.jog(axis,1,false,0)==Result::Ok);c.tick(0);c.tick(501);
    CHECK(c.axes[axis].motionError==MotionError::FeedbackStale);
  }
}
static void cascadeKickOverspeed() {
  auto c=ready(true);auto &m=c.motionSettings[0];m.kickEnabled=true;m.kickPercent=50;m.kickReleaseMs=20;
  CHECK(c.target({50,0,true,false},false,0)==Result::Ok);c.tick(0);
  CHECK(c.axes[0].motionPhase==MotionPhase::Kick);
  float previousAcceleration=0;
  for(unsigned at=20;at<=500;at+=20) {
    c.feedbackAt(0,10+float(at)*.01f,true,at);c.tick(at);const auto &a=c.axes[0];
    CHECK(a.measuredVelocity>m.velocity);
    CHECK(std::abs(a.requestedVelocity)<=m.velocity+.001f);
    CHECK(std::abs(a.profileAcceleration)<=m.acceleration+.001f);
    CHECK(std::abs(a.profileAcceleration-previousAcceleration)<=m.jerk*.02f+.001f);
    if(at>20)CHECK(a.speedOutputPercent==0); // Brake after the configured kick handoff.
    CHECK(a.motionError==MotionError::None);previousAcceleration=a.profileAcceleration;
  }
  c.stopNow(-1,520);CHECK(!c.moving());CHECK(!c.axes[0].holdArmed);
}
static void stoppedReversal() {
  for(unsigned axis:{0u,1u}) for(int direction:{-1,1}) for(uint32_t epoch:{0u,0xfffffe00u}) {
    auto c=ready(true,90);c.feedbackAt(0,90,true,epoch);c.feedbackAt(1,90,true,epoch);
    auto &m=c.motionSettings[axis];m.kickEnabled=false;m.velocity=30;
    m.acceleration=m.deceleration=10;m.jerk=8;m.stoppedVelocity=.1f;
    CHECK(c.jog(axis,direction,false,epoch)==Result::Ok);c.tick(epoch);
    // Build a nonzero measured/reference speed, then request a reverse target.
    for(unsigned t=20;t<=500;t+=20) {
      c.feedbackAt(axis,90+direction*float(t)*.02f,true,epoch+t);c.tick(epoch+t);
    }
    const float stoppedAt=90+direction*10.f;
    const Target target{stoppedAt-direction*30.f,stoppedAt-direction*30.f,axis==0,axis==1};
    const auto accepted=c.target(target,false,epoch+500);
    if(accepted!=Result::Ok)std::cerr<<"reverse axis="<<axis<<" direction="<<direction<<" epoch="<<epoch<<" fault="<<c.fault<<" result="<<int(accepted)<<"\n";
    CHECK(accepted==Result::Ok);c.tick(epoch+520);
    CHECK(!c.axes[axis].direction); // cut power before waiting for measured rest
    const uint32_t off=520;
    CHECK(c.axes[axis].speedOutputPercent==0);
    CHECK(c.axes[axis].requestedVelocity==0 && c.axes[axis].profileAcceleration==0);
    for(unsigned t=off+20;t<off+300;t+=20) {
      c.feedbackAt(axis,stoppedAt,true,epoch+t);c.tick(epoch+t);
      CHECK(!c.axes[axis].direction && c.axes[axis].speedOutputPercent==0);
    }
    c.feedbackAt(axis,stoppedAt,true,epoch+off+300);c.tick(epoch+off+300);
    CHECK(c.axes[axis].direction==-direction);
  }
}
static void stallRetry() {
  const auto prepared=[]() { auto c=ready();for(auto &m:c.motionSettings)m.stallMs=2500;return c; };
  const auto stalled=[&](unsigned axis=0) {
    auto c=prepared(); CHECK(c.jog(axis,1,false,0)==Result::Ok);c.tick(0);
    for(unsigned now=20;now<=3200;now+=20) { c.feedbackAt(0,10,true,now);c.feedbackAt(1,30,true,now);c.tick(now); }
    CHECK(c.axes[axis].motionError==MotionError::Stalled);CHECK(!c.axes[axis].direction);return c;
  };
  auto c=stalled(); CHECK(c.target({90,0,true,false},false,3220)==Result::Ok);
  CHECK(c.axes[0].motionError==MotionError::None&&c.fault.empty());c.tick(3220);CHECK(c.axes[0].direction);
  c=prepared();c.target({90,0,true,false},false,0);c.tick(0);
  for(unsigned now=20;now<=3200;now+=20){c.feedbackAt(0,10,true,now);c.feedbackAt(1,30,true,now);c.tick(now);}
  CHECK(c.axes[0].motionError==MotionError::Stalled);CHECK(c.target({90,0,true,false},false,3220)==Result::Ok);
  CHECK(c.axes[0].motionError==MotionError::None&&c.axes[0].tracking);
  c=stalled(); CHECK(c.target({90,0,true,false},true,3220)==Result::Ok);CHECK(c.axes[0].motionError==MotionError::None);
  c=stalled(); CHECK(c.jog(0,-1,false,3220)==Result::Ok);CHECK(c.axes[0].motionError==MotionError::None);
  c=stalled(1);CHECK(c.target({90,0,true,false},false,3220)==Result::Ok);CHECK(c.axes[1].motionError==MotionError::Stalled);
  c=stalled();c.axes[1].motionError=MotionError::KickTimeout;
  CHECK(c.target({90,90,true,true},false,3220)==Result::Unavailable);CHECK(c.axes[0].motionError==MotionError::Stalled);
  c=stalled();CHECK(c.target({9999,0,true,false},false,3220)==Result::Invalid);CHECK(c.axes[0].motionError==MotionError::Stalled);
  c=stalled();c.setMode(Mode::Manual,3220);CHECK(c.target({90,0,true,false},true,3240)==Result::Pending);
  CHECK(c.axes[0].motionError==MotionError::Stalled);CHECK(c.execute(3260)==Result::Ok);CHECK(c.axes[0].motionError==MotionError::None);
  c=stalled();c.setMode(Mode::Manual,3220);const Target points[]={{90,0,true,false},{180,0,true,false}};
  CHECK(c.sequence(points,2,1000,3240)==Result::Pending);CHECK(c.execute(3260)==Result::Ok);
  c.feedbackAt(0,11,true,4260);c.tick(4260);CHECK(c.axes[0].target==180);
  for(auto error:{MotionError::KickTimeout,MotionError::FeedbackStale,MotionError::FeedbackInvalid,MotionError::Planner}) {
    c=prepared();c.axes[0].motionError=error;CHECK(c.target({90,0,true,false},false,0)==Result::Unavailable);
    CHECK(c.jog(0,1,false,0)==Result::Unavailable);CHECK(c.axes[0].motionError==error);
  }
  c=prepared();CHECK(c.manualDrive(1,1,1,1,1,1,0,false,0)==Result::Ok);c.tick(0);
  for(unsigned now=20;now<=3020;now+=20) { c.feedbackAt(0,10,true,now);c.feedbackAt(1,30,true,now);c.manualDrive(1,1,1,now/20+1,1,1,0,false,now);c.tick(now); }
  CHECK(c.axes[0].motionError==MotionError::Stalled);
  CHECK(c.manualDrive(1,1,1,1,1,1,0,false,3040)==Result::Cancelled);CHECK(c.axes[0].motionError==MotionError::Stalled);
  CHECK(c.manualDrive(1,1,1,200,1,1,0,false,3060)!=Result::Ok);CHECK(c.axes[0].motionError==MotionError::Stalled);
  CHECK(c.manualDrive(1,1,2,201,1,1,0,false,3080)==Result::Ok);CHECK(c.axes[0].motionError==MotionError::None);
}
static void nearBandStream(uint32_t epoch=0) {
  auto c=ready(true,90);auto &m=c.motionSettings[0];m.velocity=20;m.positionTolerance=.5f;m.holdResumeDegrees=1;
  m.kickEnabled=true;m.kickPercent=50;m.movementDegrees=1;m.sustainingPercent=16;m.kickReleaseMs=20;
  c.resetFeedback(0,epoch);c.feedbackAt(0,90,true,epoch);
  CHECK(c.target({90.6f,0,true,false},false,epoch)==Result::Ok);c.tick(epoch);
  unsigned kicks=1;auto phase=c.axes[0].motionPhase;
  for(unsigned n=1;n<=200;++n) {
    const unsigned now=n*5;
    if(now%20==0)c.feedbackAt(0,90.4f+float(n)*.1f,true,epoch+now);
    CHECK(c.target({90.6f+float(n)*.1f,0,true,false},false,epoch+now)==Result::Ok);c.tick(epoch+now);
    if(phase!=MotionPhase::Kick&&c.axes[0].motionPhase==MotionPhase::Kick)++kicks;
    phase=c.axes[0].motionPhase;
    CHECK(c.axes[0].direction==1); // overspeed may coast without cancelling the stream/kick
    CHECK(c.axes[0].motionError==MotionError::None);
  }
  CHECK(kicks==1);CHECK(std::abs(c.axes[0].target-110.6f)<.001f);
  // Repeated delivery of the last goal cannot defer settlement forever.
  for(unsigned now=1020;now<=4000;now+=20) {
    c.feedbackAt(0,110.4f,true,epoch+now);c.target({110.6f,0,true,false},false,epoch+now);c.tick(epoch+now);
    if(c.axes[0].completed)break;
  }
  CHECK(c.axes[0].completed && !c.axes[0].direction);
}
static void stationaryNoiseStillKicks() {
  for(unsigned axis:{0u,1u})for(int direction:{-1,1}) {
    auto c=ready(true,90);c.resetFeedback(int(axis),0);c.feedbackAt(axis,90,true,0);
    auto &m=c.motionSettings[axis];m.kickEnabled=true;m.kickPercent=40;m.movementDegrees=1;m.stoppedVelocity=1;m.velocityWindowMs=80;
    for(unsigned at=20;at<=80;at+=20)c.feedbackAt(axis,90+float(at)*.00175f,true,at);
    CHECK(std::abs(c.axes[axis].measuredVelocity)>m.stoppedVelocity);
    CHECK(c.jog(axis,direction,false,80)==Result::Ok);c.tick(80);
    CHECK(c.axes[axis].motionPhase==MotionPhase::Kick && c.axes[axis].speedOutputPercent==5);
    c.stopNow(int(axis),100);CHECK(!c.axes[axis].direction);
  }
}
static void sustainingAndArrival() {
  // A fixture encoded by the previous firmware, not by the current codec.
  const std::string hex="4d4f0101030000000000c842000048420000a040000020400000004000000040000000410000004000000000cdcc4c3e00002041a4709d3f0000000055ceee400000003f0000803fcdcccc3df401000064000000c800000064000000f40100002c010000b80b00003a0b1e6a";
  MotionRecord legacy{};CHECK(legacy.size()*2==hex.size());
  const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
  for(unsigned i=0;i<legacy.size();++i)legacy[i]=uint8_t(16*digit(hex[2*i])+digit(hex[2*i+1]));
  MotionSettings expected;expected.mode=RegulatorMode::Cascade;expected.kickEnabled=true;expected.velocity=2.5f;expected.velocityKi=1.23f;
  MotionSettings restored;CHECK(decodeMotionSettings(legacy,restored));CHECK(restored==expected);CHECK(restored.sustainingPercent==0);
  CHECK(encodeMotionSettings(expected)==legacy);
  expected.sustainingPercent=16;const auto extended=encodeMotionSettings(expected);
  CHECK(extended.size()==legacy.size() && extended[2]==2);
  CHECK(decodeMotionSettings(extended,restored) && restored==expected);
  expected.outputLimitPercent=15;CHECK(!expected.valid());
  expected.mode=RegulatorMode::Simple;expected.kickEnabled=false;CHECK(expected.valid());
  for(unsigned axis:{0u,1u}) for(int direction:{-1,1}) {
    auto c=ready(true,90);c.resetFeedback(int(axis),0);c.feedbackAt(axis,90,true,0);
    auto &m=c.motionSettings[axis];m.kickEnabled=true;m.sustainingPercent=16;m.outputLimitPercent=50;m.kickReleaseMs=20;
    CHECK(c.jog(axis,direction,false,0)==Result::Ok);c.tick(0);
    for(unsigned at=20;at<=500;at+=20) {
      c.feedbackAt(axis,90+direction*float(at)*.01f,true,at);c.tick(at);
      CHECK(c.axes[axis].speedOutputPercent>=16 && c.axes[axis].speedOutputPercent<=50);
    }
    CHECK(c.jog(axis,-direction,false,520)==Result::Ok);
    bool zero=false;
    for(unsigned at=520;at<=800;at+=20) {
      c.feedbackAt(axis,90+direction*5.f,true,at);c.tick(at);
      if(c.axes[axis].motionPhase==MotionPhase::Braking && c.axes[axis].speedOutputPercent==0)zero=true;
    }
    CHECK(zero);c.stopNow(int(axis),820);CHECK(!c.axes[axis].direction && c.axes[axis].speedOutputPercent==0);
    c=ready(true);c.resetFeedback(int(axis),0);c.feedbackAt(axis,10,true,0);
    auto &arrival=c.motionSettings[axis];arrival.kickEnabled=true;arrival.sustainingPercent=16;arrival.positionTolerance=.5f;
    CHECK(c.target({10,10,axis==0,axis==1},false,0)==Result::Ok);c.tick(0);
    for(unsigned at=20;at<=400;at+=20) {
      c.feedbackAt(axis,10+((at/20)%2?.1f:-.1f),true,at);c.tick(at);
      CHECK(!c.axes[axis].direction && c.axes[axis].speedOutputPercent==0);
      CHECK(c.axes[axis].motionError==MotionError::None);
    }
    for(unsigned at=420;at<=1200;at+=20){c.feedbackAt(axis,10,true,at);c.tick(at);}
    CHECK(c.axes[axis].completed && !c.axes[axis].tracking && !c.axes[axis].direction);
  }
}

static void fastHoldingRegression() {
  for(unsigned axis:{0u,1u})for(float tau:{.08f,.16f,.24f}) {
    auto c=ready(true,90);auto &m=c.motionSettings[axis];
    m.holdEnabled=m.kickEnabled=true;m.outputLimitPercent=90;m.kickPercent=58.3f;
    m.movementDegrees=1.22f;m.velocity=29.4f;m.acceleration=m.deceleration=17.37f;m.jerk=8;
    m.positionKp=.5f;m.positionKi=m.positionKd=0;m.velocityKp=.1f;m.velocityKi=1.05f;m.velocityKd=0;
    m.feedforward=1.06f;m.positionTolerance=5.02f;m.holdResumeDegrees=7.52f;m.stoppedVelocity=8.47f;
    m.sustainingPercent=20;m.kickMs=873;m.kickReleaseMs=20;m.velocityWindowMs=80;
    float position=90,velocity=0,maxError=0;PositionFilter filter;bool disturbed=false,heldAgain=false;
    c.feedbackAt(axis,90,true,0);CHECK(c.target({90,90,axis==0,axis==1},false,0)==Result::Ok);
    for(unsigned at=20;at<=15000;at+=20) {
      if(at==1000){position+=12;disturbed=true;}
      const auto &a=c.axes[axis];const float u=a.speedOutputPercent;
      float desired=a.direction*(u>=20?6*(u-15):0);
      if(std::abs(velocity)<.05f && u<38)desired=0;
      velocity+=(desired-velocity)*(.02f/tau);position+=velocity*.02f;
      const float measured=filter.sample(int(std::round(position/.07f)),at)*.07f;
      c.feedbackAt(axis,measured,true,at);c.tick(at);
      CHECK(c.axes[axis].motionError==MotionError::None);
      if(disturbed){maxError=std::max(maxError,std::abs(position-90));if(at>2000&&c.axes[axis].motionPhase==MotionPhase::Holding)heldAgain=true;}
    }
    std::cout<<"fast hold axis="<<axis<<" tau="<<tau<<" maxError="<<maxError<<" final="<<position<<'\n';
    CHECK(maxError<30);CHECK(heldAgain);CHECK(std::abs(position-90)<=m.holdResumeDegrees);
    c.stopNow(-1,15020);CHECK(!c.moving()&&!c.axes[axis].holdArmed);
  }
}


static void boundedKickRise() {
  auto c=ready(true,90);auto &m=c.motionSettings[0];m.kickEnabled=true;m.kickPercent=60;m.kickMs=600;m.sustainingPercent=20;
  CHECK(c.jog(0,1,false,0)==Result::Ok);c.tick(0);float previous=c.axes[0].speedOutputPercent;CHECK(previous==20);
  float peak=previous;
  for(unsigned at=20;at<600;at+=20) {
    c.feedbackAt(0,90,true,at);c.tick(at);const float output=c.axes[0].speedOutputPercent;
    CHECK(output>=previous&&output<=60);CHECK(output-previous<=5.001f);peak=std::max(peak,output);previous=output;
  }
  CHECK(peak==60);
  for(unsigned at=600;at<m.stallMs;at+=20) {
    c.feedbackAt(0,90,true,at);c.tick(at);
    CHECK(c.axes[0].motionError==MotionError::None && c.axes[0].speedOutputPercent==60);
  }
  c.feedbackAt(0,90,true,m.stallMs);c.tick(m.stallMs);CHECK(c.axes[0].motionError==MotionError::KickTimeout);CHECK(!c.moving());
  c=ready(true,90);c.motionSettings[0].kickEnabled=true;c.motionSettings[0].kickMs=20;c.motionSettings[0].kickPercent=50;
  CHECK(c.jog(0,1,false,0)==Result::Ok);c.tick(0);CHECK(c.axes[0].speedOutputPercent==50);
  for(unsigned at=20;at<=3000;at+=20) {c.feedbackAt(0,90,true,at);c.tick(at);}
  CHECK(c.axes[0].motionError==MotionError::KickTimeout&&!c.moving());
}


static void oneShotOvershoot() {
  for(unsigned axis:{0u,1u}) for(int direction:{-1,1}) for(bool hold:{false,true}) for(uint32_t epoch:{0u,0xfffffe00u}) {
    auto c=ready(true,90);c.resetFeedback(int(axis),epoch);c.feedbackAt(axis,90,true,epoch);
    auto &m=c.motionSettings[axis];m.holdEnabled=hold;m.stoppedVelocity=.1f;m.positionTolerance=.25f;m.settleMs=100;
    const float goal=90+direction*10.f;
    CHECK(c.target({goal,goal,axis==0,axis==1},false,epoch)==Result::Ok);c.tick(epoch);
    // Two fresh conversions straddle the entire accepted band. The shaft
    // then coasts further; one-shot completion must wait for actual rest.
    for(unsigned t=20;t<=400;t+=20) {
      c.feedbackAt(axis,90+direction*float(t)*.03f,true,epoch+t);c.tick(epoch+t);
      CHECK(c.axes[axis].motionError==MotionError::None);
      if(t>=360)CHECK(!c.axes[axis].direction);
    }
    CHECK(!c.axes[axis].completed);
    bool reversed=false;
    for(unsigned t=420;t<=1500;t+=20) {
      c.feedbackAt(axis,90+direction*12.f,true,epoch+t);c.tick(epoch+t);
      if(c.axes[axis].direction==-direction)reversed=true;
    }
    CHECK(reversed==hold);
    if(!hold) {
      CHECK(c.axes[axis].completed && !c.axes[axis].tracking);
      // A new goal deliberately behind the shaft remains a valid movement.
      CHECK(c.target({90,90,axis==0,axis==1},false,epoch+1520)==Result::Ok);c.tick(epoch+1520);
      CHECK(c.axes[axis].direction==-direction && c.axes[axis].tracking);
    }
  }
}
static void cascadeAccelerationFeedthrough() {
  auto c=ready(true,90);auto &m=c.motionSettings[0];m.velocity=80;
  m.acceleration=m.deceleration=10;m.jerk=8;m.positionKp=m.positionKi=m.positionKd=0;
  m.velocityKp=m.velocityKi=m.velocityKd=0;m.feedforward=1;
  CHECK(c.target({400,0,true,false},false,0)==Result::Ok);c.tick(0);
  for(unsigned t=20;t<=2000;t+=20) {c.feedbackAt(0,90,true,t);c.tick(t);CHECK(c.axes[0].motionError==MotionError::None);}
  // At 2.02s a jerk-limited profile has reached10deg/s² after1.25s.
  const float duration=2.02f;
  const float expected=.5f*8*1.25f*1.25f+10*(duration-1.25f);
  std::cout<<"profile feedthrough actual="<<c.axes[0].requestedVelocity<<" expected="<<expected<<" accel="<<c.axes[0].profileAcceleration<<"\n";
  CHECK(std::abs(c.axes[0].requestedVelocity-expected)<.03f);
}
static void runtimeSpeedChanges() {
  auto c=ready(true,90);c.azRange={-1000,1000};auto &m=c.motionSettings[0];
  m.velocity=80;m.acceleration=m.deceleration=40;m.jerk=80;m.kickEnabled=false;
  CHECK(c.jog(0,1,false,0)==Result::Ok);c.tick(0);
  float position=90,previousAcceleration=c.axes[0].profileAcceleration;bool decelerated=false;
  for(unsigned t=20;t<=14000;t+=20) {
    position+=c.axes[0].requestedVelocity*.02f;
    c.feedbackAt(0,position,true,t);
    if(t==4000)CHECK(c.setRuntimeSpeed(0,10,t)==Result::Ok);
    if(t==9000)CHECK(c.setRuntimeSpeed(0,60,t)==Result::Ok);
    c.tick(t);
    CHECK(c.axes[0].motionError==MotionError::None && c.axes[0].direction==1);
    CHECK(std::abs(c.axes[0].profileAcceleration)<=40.001f);
    if(std::abs(c.axes[0].profileAcceleration-previousAcceleration)>1.601f)std::cerr<<"runtime jerk t="<<t<<" prev="<<previousAcceleration<<" actual="<<c.axes[0].profileAcceleration<<" v="<<c.axes[0].requestedVelocity<<"\n";
    CHECK(std::abs(c.axes[0].profileAcceleration-previousAcceleration)<=1.601f);
    previousAcceleration=c.axes[0].profileAcceleration;
    if(t==4020)CHECK(c.axes[0].requestedVelocity>10);
    if(t==8500){CHECK(std::abs(c.axes[0].requestedVelocity-10)<.01f);decelerated=true;}
  }
  CHECK(decelerated);CHECK(std::abs(c.axes[0].requestedVelocity-60)<.01f);
  CHECK(m.velocity==80); // Runtime selection does not rewrite saved limits.
  c=ready();c.speeds[0].accelerationMs=c.speeds[0].decelerationMs=500;
  CHECK(c.setRuntimeSpeed(0,20,0)==Result::Ok);CHECK(c.jog(0,1,false,0)==Result::Ok);c.tick(0);
  for(unsigned t=20;t<=2000;t+=20) {
    c.feedbackAt(0,10+t*.001f,true,t);
    if(t==1000)CHECK(c.setRuntimeSpeed(0,50,t)==Result::Ok);
    c.tick(t);CHECK(c.axes[0].motionError==MotionError::None && c.axes[0].direction==1);
    if(t==980)CHECK(std::abs(c.axes[0].speedOutputPercent-20)<.01f);
  }
  CHECK(std::abs(c.axes[0].speedOutputPercent-50)<.01f);
}
static void operationalSpeedProfiles() {
  for(unsigned axis:{0u,1u})for(int direction:{-1,1})for(bool tracking:{false,true}) {
    auto c=ready(true,90);c.azRange={-1000,1000};c.elRange={-1000,1000};
    c.resetFeedback(int(axis),0);c.feedbackAt(axis,90,true,0);
    auto &m=c.motionSettings[axis];m.velocity=80;m.acceleration=m.deceleration=10;m.jerk=8;
    m.positionKp=m.positionKi=m.positionKd=0;m.velocityKp=m.velocityKi=m.velocityKd=0;m.feedforward=1;
    const float goal=90+800.f*direction;
    CHECK(c.setRuntimeSpeed(axis,24,0)==Result::Ok);
    if(tracking)CHECK(c.target({goal,goal,axis==0,axis==1},false,0)==Result::Ok);
    else CHECK(c.jog(axis,direction,false,0)==Result::Ok);
    c.tick(0);float position=90,previousVelocity=c.axes[axis].requestedVelocity,previousAcceleration=c.axes[axis].profileAcceleration;
    for(unsigned t=20;t<=18000;t+=20) {
      position+=c.axes[axis].requestedVelocity*.02f;c.feedbackAt(axis,position,true,t);
      const auto epoch=c.motionEpoch[axis];
      if(t==6000)CHECK(c.setRuntimeSpeed(axis,36,t)==Result::Ok);
      if(t==10000)CHECK(c.setRuntimeSpeed(axis,12,t)==Result::Ok);
      if(t==14000)CHECK(c.setRuntimeSpeed(axis,30,t)==Result::Ok);
      auto duplicate=c;
      if(t==12500)CHECK(c.setRuntimeSpeed(axis,12,t)==Result::Ok);
      c.tick(t);const auto &a=c.axes[axis];
      CHECK(a.motionError==MotionError::None&&a.direction==direction&&a.motionPhase==MotionPhase::Run);
      CHECK(c.motionEpoch[axis]==epoch);CHECK(a.tracking==tracking);
      CHECK(std::abs(a.profileAcceleration)<=10.001f);
      if(std::abs(a.profileAcceleration-previousAcceleration)>.1605f)
        std::cerr<<"operational jerk axis="<<axis<<" dir="<<direction<<" tracking="<<tracking<<" t="<<t<<" previous="<<previousAcceleration<<" actual="<<a.profileAcceleration<<" velocity="<<a.requestedVelocity<<'\n';
      CHECK(std::abs(a.profileAcceleration-previousAcceleration)<=.1605f);
      CHECK(std::abs(a.requestedVelocity-previousVelocity)<=.201f);
      if(t==6000)CHECK(a.profileAcceleration*direction>.15f);
      if(t==10000)CHECK(a.profileAcceleration*direction<-.15f);
      if(t==8460)CHECK(std::abs(a.requestedVelocity-36*direction)<.03f);
      if(t>=10000&&t<14000) {
        CHECK(a.requestedVelocity*direction>=11.99f);
        CHECK(a.requestedVelocity*direction<=previousVelocity*direction+.0005f);
      }
      if(t==13660)CHECK(std::abs(a.requestedVelocity-12*direction)<.03f);
      if(t==12500) {
        duplicate.tick(t);CHECK(a.requestedVelocity==duplicate.axes[axis].requestedVelocity);
        CHECK(a.profileAcceleration==duplicate.axes[axis].profileAcceleration);
        CHECK(a.speedOutputPercent==duplicate.axes[axis].speedOutputPercent);
      }
      previousVelocity=a.requestedVelocity;previousAcceleration=a.profileAcceleration;
    }
    CHECK(std::abs(c.axes[axis].requestedVelocity-30*direction)<.03f);
    if(tracking)CHECK(c.axes[axis].target==goal);
    CHECK(m.velocity==80);c.stopNow(-1,18020);CHECK(!c.moving());
  }
}
// Reproduce a live speed increase while startup overspeed has left the shaft
// ahead of the virtual position profile. An old path used to keep braking for
// another 0.6 s after the selection; only jerk release should delay the turn.
static void startupSpeedSelection() {
  for(unsigned axis:{0u,1u})for(int direction:{-1,1}) {
    auto c=ready(true,90);c.azRange={-1000,1000};c.elRange={-1000,1000};
    c.resetFeedback(int(axis),0);c.feedbackAt(axis,90,true,0);auto &m=c.motionSettings[axis];
    m.kickEnabled=true;m.kickPercent=35;m.kickMs=300;m.kickReleaseMs=20;m.movementDegrees=1;m.sustainingPercent=18;
    m.velocity=40;m.acceleration=m.deceleration=10;m.jerk=8;m.positionKp=.5f;m.positionKi=m.positionKd=0;
    m.velocityKp=.12f;m.velocityKi=.8f;m.velocityKd=0;m.feedforward=.61f;m.velocityWindowMs=80;
    m.positionTolerance=3.59f;m.stoppedVelocity=3.64f;m.outputLimitPercent=50;
    CHECK(c.setRuntimeSpeed(axis,24,0)==Result::Ok);const float goal=90+800.f*direction;
    CHECK(c.target({goal,goal,axis==0,axis==1},false,0)==Result::Ok);c.tick(0);
    float position=90,velocity=0;PositionFilter filter;
    for(unsigned t=20;t<=2000;t+=20) {
      const auto &a=c.axes[axis];const float u=a.speedOutputPercent;
      float wanted=a.direction*(u>=18?6*(u-15):0);if(std::abs(velocity)<.05f&&u<28)wanted=0;
      velocity+=(wanted-velocity)*(.02f/.16f);position+=velocity*.02f;
      c.feedbackAt(axis,filter.sample(int(std::round(position/.07f)),t)*.07f,true,t);
      if(t==600)CHECK(c.setRuntimeSpeed(axis,36,t)==Result::Ok);
      c.tick(t);CHECK(a.motionError==MotionError::None);
      if(t>=600)CHECK(a.motionPhase==MotionPhase::Run&&a.direction==direction&&a.target==goal);
      if(t==1000) { CHECK(a.profileAcceleration*direction>0);CHECK(a.requestedVelocity*direction>23); }
    }
  }
}
static void selectedSpeedNearGoal() {
  for(unsigned axis:{0u,1u})for(int direction:{-1,1}) {
    auto c=ready(true,90);c.azRange={-1000,1000};c.elRange={-1000,1000};
    c.resetFeedback(int(axis),0);c.feedbackAt(axis,90,true,0);auto &m=c.motionSettings[axis];
    m.velocity=40;m.acceleration=m.deceleration=10;m.jerk=8;m.positionKp=m.positionKi=m.positionKd=0;
    m.velocityKp=m.velocityKi=m.velocityKd=0;m.feedforward=1;m.positionTolerance=.2f;
    const float goal=90+40.f*direction;
    CHECK(c.target({goal,goal,axis==0,axis==1},false,0)==Result::Ok);c.tick(0);
    float position=90;bool changed=false;
    for(unsigned t=20;t<=14000;t+=20) {
      position+=c.axes[axis].requestedVelocity*.02f;c.feedbackAt(axis,position,true,t);
      if(!changed&&std::abs(goal-position)<20) {CHECK(c.setRuntimeSpeed(axis,8,t)==Result::Ok);changed=true;}
      c.tick(t);CHECK(c.axes[axis].motionError==MotionError::None);
      CHECK(!c.axes[axis].direction||c.axes[axis].direction==direction);
      if(c.axes[axis].completed)break;
    }
    CHECK(changed&&c.axes[axis].completed);CHECK(std::abs(position-goal)<.3f);
    CHECK(!c.axes[axis].holdArmed&&c.axes[axis].speedOutputPercent==0);
  }
}
static void longCascadeCruise() {
  for(unsigned axis:{0u,1u}) for(int direction:{-1,1}) {
    auto c=ready(true,90);c.azRange={-500,500};c.elRange={-500,500};
    c.resetFeedback(int(axis),0);c.feedbackAt(axis,90,true,0);
    auto &m=c.motionSettings[axis];m.kickEnabled=true;m.kickPercent=35;m.kickMs=1200;m.kickReleaseMs=20;
    m.velocity=60;m.acceleration=m.deceleration=40;m.jerk=80;m.positionKp=.5f;m.positionKi=m.positionKd=0;
    m.velocityKp=.03f;m.velocityKi=.47f;m.velocityKd=0;m.feedforward=.42f;m.sustainingPercent=17;
    m.velocityWindowMs=80;m.stoppedVelocity=1;m.positionTolerance=2;m.movementDegrees=1;
    const float destination=90+direction*400.f;
    CHECK(c.target({destination,destination,axis==0,axis==1},false,0)==Result::Ok);c.tick(0);
    float position=90,velocity=0;PositionFilter filter;unsigned zeroRun=0;float minCruise=1000,maxCruise=0;
    for(unsigned t=20;t<=6000;t+=20) {
      const auto &a=c.axes[axis];const float u=a.speedOutputPercent;
      float goal=a.direction*(u>=17?6*(u-15):0);
      if(std::abs(velocity)<.05f && u<28)goal=0;
      velocity+=(goal-velocity)*(.02f/.16f);position+=velocity*.02f;
      c.feedbackAt(axis,filter.sample(int(std::round(position/.07f)),t)*.07f,true,t);c.tick(t);
      CHECK(c.axes[axis].motionError==MotionError::None);
      if(t>=2500 && t<=4500) {
        minCruise=std::min(minCruise,std::abs(velocity));maxCruise=std::max(maxCruise,std::abs(velocity));
        if(a.motionPhase==MotionPhase::Run && std::abs(velocity)<1)++zeroRun;
      }

    }
    std::cout<<"cruise axis="<<axis<<" direction="<<direction<<" min="<<minCruise<<" max="<<maxCruise<<'\n';
    CHECK(!zeroRun);CHECK(minCruise>35 && maxCruise<90);
  }
}


// A short breakaway pulse is followed by a slow trajectory on this drive.
// Resetting I to zero and seeding velocity from the lagging window used to
// drop PWM below dynamic friction, stop for ~1.5 s, then restart in a burst.
static void slowKickHandoff() {
  for(unsigned axis:{0u,1u})for(int direction:{-1,1})for(bool positioning:{false,true})
    for(float tau:{.08f,.16f,.24f,.36f}) {
    auto c=ready(true,90);c.azRange={-500,500};c.elRange={-500,500};
    c.resetFeedback(int(axis),0);c.feedbackAt(axis,90,true,0);
    auto &m=c.motionSettings[axis];m.kickEnabled=true;m.kickPercent=35;m.kickMs=300;m.kickReleaseMs=20;
    m.velocity=39.51f;m.acceleration=m.deceleration=10;m.jerk=8;
    m.positionKp=.5f;m.positionKi=m.positionKd=0;
    m.velocityKp=.02f;m.velocityKi=.39f;m.velocityKd=0;m.feedforward=.61f;m.sustainingPercent=17;
    m.velocityWindowMs=120;m.stoppedVelocity=3.64f;m.positionTolerance=3.59f;m.movementDegrees=1;
    const float goal=90+direction*400.f;
    if(positioning)CHECK(c.target({goal,goal,axis==0,axis==1},false,0)==Result::Ok);
    else CHECK(c.jog(axis,direction,false,0)==Result::Ok);
    c.tick(0);float position=90,velocity=0;PositionFilter filter;
    unsigned starts=1;auto phase=c.axes[axis].motionPhase;
    float minimum=1000,maximum=0;
    for(unsigned t=20;t<=6000;t+=20) {
      const auto &a=c.axes[axis];const float u=a.speedOutputPercent;
      float desired=a.direction*(u>=23?6*(u-23):0);
      if(std::abs(velocity)<.1f && u<28)desired=0;
      velocity+=(desired-velocity)*(.02f/tau);position+=velocity*.02f;
      c.feedbackAt(axis,filter.sample(int(std::round(position/.07f)),t)*.07f,true,t);c.tick(t);
      CHECK(a.motionError==MotionError::None);
      CHECK(a.direction==direction);
      if(a.motionPhase==MotionPhase::Kick && phase!=MotionPhase::Kick)++starts;
      phase=a.motionPhase;
      if(t>=1000 && t<=3500) {
        minimum=std::min(minimum,std::abs(velocity));maximum=std::max(maximum,std::abs(velocity));
      }
    }
    CHECK(starts==1);CHECK(minimum>2);CHECK(maximum<2*m.velocity);
    c.stopNow(int(axis),6020);CHECK(!c.axes[axis].direction && !c.axes[axis].holdArmed);
  }
}

static void proportionalManualMinimum() {
  for (bool cascade : {false, true}) for (unsigned axis : {0u, 1u})
    for (int direction : {-1, 1}) for (float fraction : {.0001f, .25f, 1.f})
      for (unsigned floor : {0u,12u}) for (bool absolute : {false,true}) {
      auto c=ready(cascade,90);
      auto &m=c.motionSettings[axis];
      m.kickEnabled=false; m.sustainingPercent=floor; m.outputLimitPercent=80;
      m.velocity=80; m.acceleration=m.deceleration=100; m.jerk=1000;
      m.positionKp=m.positionKi=m.positionKd=m.velocityKp=m.velocityKi=m.velocityKd=0;
      m.feedforward=1;
      c.speeds[axis].accelerationMs=c.speeds[axis].decelerationMs=100;
      if (absolute) { // Full-range velocity needs travel beyond the default EL span.
        CHECK(c.setRange({-1000,1000},AZ_CUSTOM,0));
        CHECK(c.setElRange({-1000,1000},EL_CUSTOM,0));
      }
      c.feedbackAt(axis,90,true,1000);
      const float working=cascade?20.f:40.f;
      CHECK(c.setRuntimeSpeed(axis,working,1000)==Result::Ok);
      float position=90; uint32_t serial=0;
      for (uint32_t t=1020;t<=5020;t+=20) {
        c.feedbackAt(axis,position,true,t);
        CHECK(c.manualDrive(0,1,1,++serial,1u<<axis,axis?0:direction*fraction,
                            axis?direction*fraction:0,false,t,100,absolute)==Result::Ok);
        c.tick(t);
        CHECK(c.axes[axis].motionError==MotionError::None);
        position+=cascade?c.axes[axis].requestedVelocity*.02f:direction*.002f;
      }
      const auto &a=c.axes[axis];
      const float minimum=cascade?.05f*working:std::max(.05f*working,float(floor));
      const float expected=absolute?80*fraction:minimum+(working-minimum)*fraction;
      if (std::abs((cascade?std::abs(a.requestedVelocity):a.speedOutputPercent)-expected)>=.002f)
        std::cerr << "manual cascade=" << cascade << " absolute=" << absolute << " axis=" << axis << " direction=" << direction << " fraction=" << fraction << " floor=" << floor << " expected=" << expected << " velocity=" << a.requestedVelocity << " output=" << a.speedOutputPercent << "\n";
      CHECK(std::abs((cascade?std::abs(a.requestedVelocity):a.speedOutputPercent)-expected)<.002f);
      CHECK(((absolute && !cascade) || a.speedOutputPercent>=float(floor)-.002f) && a.speedOutputPercent<=80);
      CHECK(c.manualDrive(0,1,1,++serial,1u<<axis,0,0,false,5040)==Result::Ok);
      c.tick(5040);
      CHECK(!a.direction && !a.wanted && a.speedOutputPercent==0);
    }
  // A selected output below the configured floor cannot defeat that floor,
  // while the hard output cap still takes priority over both settings.
  for (float cap : {80.f,8.f}) {
    auto c=ready(false,90); auto &m=c.motionSettings[0];
    m.kickEnabled=false; m.outputLimitPercent=cap; m.sustainingPercent=12;
    c.speeds[0].accelerationMs=c.speeds[0].decelerationMs=100;
    CHECK(c.setRuntimeSpeed(0,5,0)==Result::Ok);
    for (uint32_t t=20;t<=2000;t+=20) {
      c.feedbackAt(0,90+float(t)*.0001f,true,t);
      CHECK(c.manualDrive(0,1,1,t,1,.0001f,0,false,t)==Result::Ok);
      c.tick(t);
    }
    CHECK(std::abs(c.axes[0].speedOutputPercent-std::min(12.f,cap))<.002f);
    if (cap==80) {
      CHECK(c.target({120,0,true,false},false,2020)==Result::Ok);
      for (uint32_t t=2020;t<=4020;t+=20) {
        c.feedbackAt(0,90+float(t)*.0001f,true,t);
        c.tick(t);
      }
      CHECK(std::abs(c.axes[0].speedOutputPercent-5.f)<.002f); // Positioning keeps its working setting.
    }
  }
}
int main() { try { proportionalManualMinimum();operationalSpeedProfiles();startupSpeedSelection();selectedSpeedNearGoal();slowKickHandoff();oneShotOvershoot();cascadeAccelerationFeedthrough();runtimeSpeedChanges();longCascadeCruise(); boundedKickRise();fastHoldingRegression(); nearBandStream();nearBandStream(0xfffffe00u);stationaryNoiseStillKicks();sustainingAndArrival(); records();stallRetry();kickAndStream();cascadeHold();profilesAndNoise();feedbackSafety();lowOutputProgress();cascadeKickOverspeed();stoppedReversal();std::cout<<"PASS motion: "<<checks<<" checks; Controller bytes="<<sizeof(Controller)<<"\n"; } catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; } }
