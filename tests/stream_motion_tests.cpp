#include "control.hpp"
#include "protocol.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace forkrot;
static void stream(unsigned hz,unsigned spacing,bool jitter,uint32_t epoch,int sign) {
  Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
  assert(c.setControlFrequency(hz,epoch)==Result::Ok);c.setMode(Mode::Auto,epoch);
  const unsigned period=1000/hz;
  for(unsigned i=0;i<2;++i) {
    c.feedbackAt(i,90,true,epoch);
    auto &m=c.motionSettings[i];m.mode=RegulatorMode::Cascade;m.velocity=10;
    m.acceleration=m.deceleration=5;m.jerk=20;m.positionKp=.5f;
    m.positionTolerance=5;m.holdResumeDegrees=6;m.stoppedVelocity=.1f;
    m.kickEnabled=true;m.kickPercent=34;m.sustainingPercent=16;m.movementDegrees=1;m.kickReleaseMs=20;
    m.velocityKp=.18f;m.velocityKi=.5f;m.feedforward=.23f;m.stallMs=20000;
  }
  unsigned next=0,packets=0;float goal=106;
  unsigned starts[2]={};int previous[2]={};float minRequest=100;
  for(unsigned t=0;t<=8000;t+=period) {
    if(t>=next) {
      goal=106+5.f*t/1000;
      assert(c.target({90+sign*(goal-90),90-sign*(goal-90),true,true},true,epoch+t)==Result::Ok);
      next+=spacing+(jitter?(packets++%2?20:-20):0);
    }
    // The shaft catches up with the stream and enters the 5 degree arrival
    // band. A moving target must not keep stopping/restarting there.
    const float travel=t<2400 ? t*.01f : 24+(t-2400)*.005f;
    c.feedbackAt(0,90+sign*travel,true,epoch+t);c.feedbackAt(1,90-sign*travel,true,epoch+t);c.tick(epoch+t);
    for(unsigned i=0;i<2;++i) {
      const auto &a=c.axes[i];assert(a.motionError==MotionError::None);
      if(a.direction && !previous[i])++starts[i];
      previous[i]=a.direction;
      if(t>2600) {
        assert(a.direction==(i?-sign:sign));
        assert(a.motionPhase!=MotionPhase::Braking);
        if(spacing<=400)assert(a.speedOutputPercent>0);
        minRequest=std::min(minRequest,std::abs(a.requestedVelocity));
      }
    }
  }
  // Before a second packet arrives, a standalone first point can finish.
  // Once cadence is known there must be no further restart.
  assert(starts[0]<=(spacing==1500?2u:1u) && starts[1]<=(spacing==1500?2u:1u));assert(minRequest>1);
  // Repeated final packets must not prolong streaming. Finish at the last
  // commanded coordinate; the axes must stop without a new kick.
  for(unsigned t=8000+period;t<=15000;t+=period) {
    const float travel=std::min(goal-90-.2f,52+(t-8000)*.005f);
    c.feedbackAt(0,90+sign*travel,true,epoch+t);c.feedbackAt(1,90-sign*travel,true,epoch+t);
    c.target({90+sign*(goal-90),90-sign*(goal-90),true,true},true,epoch+t);c.tick(epoch+t);
  }
  assert(c.axes[0].completed && c.axes[1].completed);assert(!c.moving());
  c.target({160,20,true,true},true,epoch+15020);c.tick(epoch+15020);
  c.stopNow(-1,epoch+15021);assert(!c.moving());
  assert(!c.axes[0].speedOutputPercent && !c.axes[1].speedOutputPercent);
}
static void kickMustRespectMovingGoal() {
  for(int direction:{-1,1}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.setControlFrequency(200,0);c.setMode(Mode::Auto,0);c.feedbackAt(0,90,true,0);
    auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=10;
    m.positionTolerance=.5f;m.holdResumeDegrees=6;m.movementDegrees=10;
    m.kickEnabled=true;m.kickPercent=45;m.sustainingPercent=17;m.kickMs=1200;
    m.velocityWindowMs=80;m.stallMs=20000;
    for(unsigned t=0;t<=1000;t+=5) {
      if(t%200==0)assert(c.target({90+direction*(2+t*.005f),0,true,false},false,t)==Result::Ok);
      c.feedbackAt(0,90+direction*(t*.01f),true,t);c.tick(t);
      assert(c.axes[0].motionError==MotionError::None);
      if(t>=550)assert(c.axes[0].speedOutputPercent==0);
    }
    c.stopNow(-1,1001);assert(!c.moving());
  }
}
static void boundedLongPeriodPrediction() {
  for(int direction:{-1,1}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.setControlFrequency(200,0);c.setMode(Mode::Auto,0);c.feedbackAt(0,90,true,0);
    auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=10;
    m.positionTolerance=5;m.holdResumeDegrees=6;m.movementDegrees=1;
    m.positionKp=m.positionKi=m.positionKd=m.velocityKp=m.velocityKi=m.velocityKd=0;
    m.feedforward=5;m.acceleration=m.deceleration=10;m.jerk=80;m.stallMs=20000;
    for(unsigned t=0;t<=5900;t+=5) {
      if(t%1500==0)assert(c.target({90+direction*(16+t*.005f),0,true,false},false,t)==Result::Ok);
      const float travel=t<3200?t*.01f:16+t*.005f+6*std::min(1.f,(t-3200)/2000.f);
      c.feedbackAt(0,90+direction*travel,true,t);c.tick(t);
      assert(c.axes[0].motionError==MotionError::None);
      if(t>=1600)assert(c.axes[0].direction==direction);
      if(t>5500)assert(c.axes[0].speedOutputPercent==0);
    }
    c.stopNow(-1,5901);assert(!c.moving());
  }
}
static void cadenceCanSlowTo1500() {
  Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
  c.setControlFrequency(200,0);c.setMode(Mode::Auto,0);c.feedbackAt(0,90,true,0);
  auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=10;
  m.positionTolerance=5;m.holdResumeDegrees=6;m.positionKp=.5f;m.positionKi=m.positionKd=0;
  m.acceleration=m.deceleration=5;m.jerk=20;m.stallMs=20000;
  for(unsigned t=0;t<=8000;t+=5) {
    if((t<=2000 && t%200==0) || (t>2000 && (t-2000)%1500==0))
      assert(c.target({106+t*.005f,0,true,false},false,t)==Result::Ok);
    const float shaft=t<3200?90+t*.01f:106+t*.005f;
    c.feedbackAt(0,shaft,true,t);c.tick(t);
    assert(c.axes[0].motionError==MotionError::None && c.axes[0].direction==1);
    assert(c.axes[0].motionPhase!=MotionPhase::Braking);
  }
  c.stopNow(-1,8001);assert(!c.moving());
}
static void streamAfterFirstArrival() {
  for(unsigned hz:{50u,100u,200u})for(int direction:{-1,1})for(uint32_t epoch:{0u,0xffffff00u}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.setControlFrequency(hz,epoch);c.setMode(Mode::Auto,epoch);c.feedbackAt(0,90,true,epoch);
    auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=10;
    m.positionTolerance=5;m.holdResumeDegrees=6;m.kickEnabled=false;m.stallMs=20000;
    assert(c.target({90,0,true,false},false,epoch)==Result::Ok);
    for(unsigned t=0;t<1500;t+=1000/hz) {c.feedbackAt(0,90,true,epoch+t);c.tick(epoch+t);}
    assert(c.axes[0].completed && !c.moving());
    for(unsigned t=1500;t<=6000;t+=1000/hz) {
      if(t%1500==0) {const auto result=c.target({90+direction*(t*.001f),0,true,false},false,epoch+t);if(result!=Result::Ok)std::cerr<<"arrival stream "<<hz<<" "<<direction<<" "<<epoch<<" "<<t<<" result "<<int(result)<<" fault "<<c.fault<<"\n";assert(result==Result::Ok);}
      c.feedbackAt(0,90+direction*((t-1500)*.001f),true,epoch+t);c.tick(epoch+t);
      assert(c.axes[0].direction==direction && c.axes[0].tracking);
      if(t>3000)assert(direction*c.axes[0].requestedVelocity>.1f);
    }
    c.stopNow(-1,epoch+6001);assert(!c.moving());
    // STOP clears cadence; a fresh in-band standalone command stays stopped.
    assert(c.target({c.axes[0].current,0,true,false},false,epoch+6020)==Result::Ok);
    c.feedbackAt(0,c.axes[0].current,true,epoch+6020);c.tick(epoch+6020);
    assert(!c.axes[0].direction && !c.axes[0].speedOutputPercent);
  }
}
static void feedforwardCruise() {
  for(unsigned hz:{50u,100u,200u}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.setControlFrequency(hz,0);c.setMode(Mode::Auto,0);Protocol pst(c);
    for(unsigned i=0;i<2;++i) {
      c.feedbackAt(i,90,true,0);auto &m=c.motionSettings[i];m.mode=RegulatorMode::Cascade;
      m.velocity=10;m.acceleration=m.deceleration=5;m.jerk=20;m.positionKp=.5f;m.positionKd=0;
      m.positionTolerance=5;m.holdResumeDegrees=6;m.stallMs=20000;
    }
    for(unsigned t=0;t<=10000;t+=1000/hz) {
      if(t%200==0) {
        const auto value=std::to_string(106+t*.005f);
        const auto packet=pst.datagram("<PST><LP>0</LP><AZIMUTH>"+value+"</AZIMUTH><ELEVATION>"+value+"</ELEVATION></PST>",t);
        assert(packet.matched && packet.result==Result::Ok);
      }
      const float shaft=t<3200 ? 90+t*.01f : 106+t*.005f;
      for(unsigned i=0;i<2;++i)c.feedbackAt(i,shaft,true,t);
      c.tick(t);
      if(t>7000)for(const auto &a:c.axes) {
        assert(a.motionError==MotionError::None && a.direction==1);
        // The requested velocity must follow 1 degree / 200 ms rather than
        // continuing at the 10 deg/s work speed and overrunning the stream.
        assert(std::abs(a.requestedVelocity-5)<.15f);
      }
    }
    assert(pst.datagram("<PST><STOP>1</STOP></PST>",10001).result==Result::Ok);
    c.tick(10001);
    assert(!c.moving());
  }
}
static void overspeedMayBrake() {
  for(int direction:{-1,1}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.setControlFrequency(200,0);c.feedbackAt(0,90,true,0);
    auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=10;
    m.acceleration=m.deceleration=100;m.jerk=1000;m.sustainingPercent=16;
    m.velocityKp=2;m.velocityKi=m.velocityKd=0;m.feedforward=1;
    for(unsigned t=0;t<=2000;t+=5) {
      if(t%200==0)assert(c.target({90+direction*(20+t*.02f),0,true,false},false,t)==Result::Ok);
      c.feedbackAt(0,90+direction*float(t)*.02f,true,t);c.tick(t);
    }
    assert(c.axes[0].motionError==MotionError::None && c.axes[0].direction==direction);
    assert(std::abs(c.axes[0].requestedVelocity-direction*10)<.01f);
    assert(c.axes[0].speedOutputPercent==0); // the 16% floor must permit overspeed braking
  }
}
static void feedbackObservationTime() {
  for(unsigned hz:{50u,100u,200u})for(uint32_t epoch:{0u,0xffffff00u}) {
    const auto ready=[&]() {
      Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
      c.setControlFrequency(hz,epoch);c.feedbackAt(0,90,true,epoch);
      assert(c.jog(0,1,false,epoch)==Result::Ok);c.tick(epoch);return c;
    };
    auto c=ready();const unsigned p=1000/hz;
    for(unsigned t=p;t<60;t+=p) {
      c.feedbackAt(0,120,true,epoch+t);c.tick(epoch+t);
      assert(c.axes[0].motionError==MotionError::None && c.axes[0].current==90);
    }
    c.feedbackAt(0,120,true,epoch+60);c.tick(epoch+60);
    assert(c.axes[0].motionError==MotionError::FeedbackInvalid && !c.moving());
    assert(c.motionFeedback(0).invalidEvents==1);
    c=ready();
    for(unsigned t=p;t<=20;t+=p)c.feedbackAt(0,100,true,epoch+t);
    c.feedbackAt(0,90,true,epoch+40);c.tick(epoch+40);
    assert(c.axes[0].motionError==MotionError::None && c.axes[0].calibrated);
    assert(c.motionFeedback(0).invalidEvents==0);
  }
}
static void signedPstRange() {
  Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
  c.azRange={-50,190};c.elRange={-45,190};c.feedbackAt(0,90,true,0);c.feedbackAt(1,90,true,0);
  c.setMode(Mode::Auto,0);Protocol pst(c);
  assert(pst.datagram("<PST><LP>0</LP><AZIMUTH>-40</AZIMUTH><ELEVATION>-40</ELEVATION></PST>",0).result==Result::Ok);
  assert(c.axes[0].target==-40 && c.axes[1].target==-40);
  assert(pst.datagram("<PST><LP>1</LP></PST>",1).result==Result::Ok && c.axes[0].target==140);
  assert(pst.datagram("<PST><LP>0</LP></PST>",2).result==Result::Ok && c.axes[0].target==-40);
  assert(pst.datagram("<PST><AZIMUTH>-51</AZIMUTH></PST>",3).result==Result::Invalid);
  assert(c.axes[0].target==-40);
  c.azRange={-400,400};
  assert(pst.datagram("<PST><AZIMUTH>-360</AZIMUTH></PST>",4).result==Result::Ok && c.axes[0].target==-360);
}
static void burstDelivery(uint32_t epoch) {
  Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
  c.setControlFrequency(200,epoch);c.feedbackAt(0,90,true,epoch);
  auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=10;
  m.positionTolerance=5;m.holdResumeDegrees=6;m.positionKp=.5f;m.positionKd=0;
  m.acceleration=m.deceleration=5;m.jerk=20;m.stallMs=20000;
  for(unsigned t=0;t<=8000;t+=5) {
    if(t%200==0 && (t<3000 || t>=3600)) {
      // Four delayed targets delivered in the very same millisecond.
      if(t==3600)for(unsigned source:{3000u,3200u,3400u})
        assert(c.target({106+source*.005f,0,true,false},false,epoch+t)==Result::Ok);
      assert(c.target({106+t*.005f,0,true,false},false,epoch+t)==Result::Ok);
    }
    const float shaft=t<3200 ? 90+t*.01f : 106+t*.005f;
    c.feedbackAt(0,shaft,true,epoch+t);c.tick(epoch+t);
    assert(c.axes[0].motionError==MotionError::None && c.axes[0].direction==1);
    assert(c.axes[0].motionPhase!=MotionPhase::Braking);
  }
  c.stopNow(-1,epoch+8001);assert(!c.moving());
}
static void movingTargetOverrun() {
  Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
  c.setControlFrequency(200,0);c.feedbackAt(0,90,true,0);
  auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=10;
  m.positionTolerance=5;m.holdResumeDegrees=6;m.positionKp=m.positionKi=m.positionKd=0;
  m.velocityKp=m.velocityKi=m.velocityKd=0;m.feedforward=5;m.stallMs=20000;
  m.acceleration=m.deceleration=10;m.jerk=80;
  for(unsigned t=0;t<=8000;t+=5) {
    if(t%200==0)assert(c.target({106+t*.005f,0,true,false},false,t)==Result::Ok);
    // A disturbance carries the shaft beyond the moving goal while an old
    // positive feedforward still demands torque. Cut PWM without a new kick.
    const float shaft=t<5000 ? 90+t*.01f : 140+(t-5000)*.005f;
    c.feedbackAt(0,shaft,true,t);c.tick(t);
    assert(c.axes[0].motionError==MotionError::None && c.axes[0].direction==1);
    if(t>6000)assert(c.axes[0].speedOutputPercent==0);
  }
}
static void streamRecoversFromStiction() {
  for(int direction:{-1,1}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.setControlFrequency(200,0);c.setMode(Mode::Auto,0);c.feedbackAt(0,90,true,0);
    auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=10;
    m.acceleration=m.deceleration=5;m.jerk=20;m.positionKp=.5f;m.positionKi=m.positionKd=0;
    m.velocityKp=.18f;m.velocityKi=1;m.velocityKd=0;m.feedforward=.23f;
    m.kickEnabled=true;m.kickPercent=45;m.sustainingPercent=17;m.kickMs=1200;
    m.movementDegrees=1;m.velocityWindowMs=80;m.stallMs=20000;
    unsigned kicks=0;MotionPhase phase=MotionPhase::Idle;bool running=false;float previousVelocity=0;
    for(unsigned t=0;t<=8000;t+=5) {
      if(t%200==0)assert(c.target({90+direction*(16+t*.005f),0,true,false},false,t)==Result::Ok);
      // An imposed two-second jam interrupts an otherwise moving shaft.
      const float travel=t<3000?t*.01f:(t<5000?30:30+(t-5000)*.01f);
      c.feedbackAt(0,90+direction*travel,true,t);c.tick(t);const auto &a=c.axes[0];
      assert(a.motionError==MotionError::None && a.direction==direction);
      if(a.motionPhase==MotionPhase::Kick && phase!=MotionPhase::Kick)++kicks;
      if(running)assert(std::abs(a.requestedVelocity-previousVelocity)<=m.acceleration*.005f+.001f);
      if(a.motionPhase==MotionPhase::Run)running=true;
      previousVelocity=a.requestedVelocity;phase=a.motionPhase;
    }
    assert(kicks==2 && c.axes[0].motionPhase==MotionPhase::Run);
    c.stopNow(-1,8001);assert(!c.moving());
  }
}
static void kickCannotSeedFastStreamTrajectory() {
  for(int direction:{-1,1}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.setControlFrequency(200,0);c.feedbackAt(0,90,true,0);c.setMode(Mode::Auto,0);
    auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=80;
    m.acceleration=m.deceleration=5;m.jerk=20;m.positionKp=.5f;m.positionKi=m.positionKd=0;
    m.kickEnabled=true;m.kickPercent=40;m.sustainingPercent=17;m.kickMs=1200;
    m.movementDegrees=1;m.velocityWindowMs=80;m.stallMs=20000;
    bool handedOff=false;
    for(unsigned t=0;t<=2000;t+=5) {
      if(t%200==0)assert(c.target({90+direction*(16+t*.005f),0,true,false},false,t)==Result::Ok);
      const float shaft=90+direction*(t>1000?(t-1000)*.02f:0);
      c.feedbackAt(0,shaft,true,t);c.tick(t);
      const auto &a=c.axes[0];
      if(a.motionPhase==MotionPhase::Run && !handedOff) {
        handedOff=true;
        // Selected maximum 80 is not the velocity of a 5 deg/s stream.
        // Neither a real kick overspeed nor a fresh ADC spike can seed it.
        const float correction=.5f*std::max(0.f,direction*(a.target-shaft)+1);
        assert(std::abs(a.requestedVelocity)<=5+correction+.1f);
      }
    }
    assert(handedOff);c.stopNow(-1,2001);assert(!c.moving());
  }
}
static void switchingNoiseDoesNotEndKick() {
  for(unsigned hz:{50u,100u,200u})for(int direction:{-1,1}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.setControlFrequency(hz,0);c.feedbackAt(0,90,true,0);c.setMode(Mode::Auto,0);
    auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.kickEnabled=true;
    m.kickPercent=34;m.sustainingPercent=17;m.kickMs=1200;m.movementDegrees=1;
    m.velocityWindowMs=80;m.stallMs=20000;
    const unsigned period=1000/hz;
    for(unsigned t=0;t<=900;t+=period) {
      if(t%200==0)assert(c.target({90+direction*(30+t*.005f),0,true,false},false,t)==Result::Ok);
      // Two favourable conversions exceed the movement threshold, but the
      // shaft is still stationary. This used to hand off at the first spike.
      const float noise=(t/period)%7<2 ? 1.4f : 0;
      c.feedbackAt(0,90+direction*noise,true,t);c.tick(t);
      assert(c.axes[0].motionPhase==MotionPhase::Kick);
    }
    assert(c.axes[0].speedOutputPercent>26);
    c.stopNow(-1,901);assert(!c.moving());
  }
}
// Static friction requires >26% to start, but only about 16.3% sustains
// 5 deg/s. Keeping the starting pulse in I or enforcing a 17% running floor
// turns this plant into a repeated stall/fast-catch-up cycle.
static void stictionStream() {
  for(unsigned hz:{50u,100u,200u})for(unsigned axis:{0u,1u})
    for(int direction:{-1,1})for(float tau:{.05f,.1f,.2f}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.azRange=c.elRange={-500,500};c.setControlFrequency(hz,0);
    c.feedbackAt(axis,90,true,0);c.setMode(Mode::Auto,0);
    auto &m=c.motionSettings[axis];m.mode=RegulatorMode::Cascade;m.velocity=81.93f;
    m.acceleration=m.deceleration=5;m.jerk=20;m.positionKp=.5f;m.positionKi=m.positionKd=0;
    m.velocityKp=.18f;m.velocityKi=.5f;m.velocityKd=0;m.feedforward=.23f;
    m.sustainingPercent=17;m.kickPercent=34;m.kickEnabled=true;m.kickMs=1200;m.kickReleaseMs=20;
    m.movementDegrees=1;m.stoppedVelocity=4.84f;m.velocityWindowMs=80;
    m.positionTolerance=5;m.holdResumeDegrees=5.38f;m.stallMs=20000;
    assert(c.setRuntimeSpeed(axis,10.5647f,0)==Result::Ok);
    const unsigned period=1000/hz;const float dt=period/1000.f;
    float position=90,velocity=0,previousOutput=0;
    for(unsigned t=0;t<=20000;t+=period) {
      const auto &a=c.axes[axis];const float u=a.speedOutputPercent;
      float plant=a.direction*std::max(0.f,(u-15.5f)*6.5f);
      if(std::abs(velocity)<.1f && u<26)plant=0;
      velocity+=(plant-velocity)*(dt/tau);position+=velocity*dt;
      if(t%200==0) {
        const float goal=90+direction*(10+t*.005f);
        assert(c.target({goal,goal,axis==0,axis==1},false,t)==Result::Ok);
      }
      c.feedbackAt(axis,std::round(position*20)/20,true,t);c.tick(t);
      assert(a.motionError==MotionError::None && a.direction==direction);
      if(t>4000) {
        assert(std::abs(velocity)<10);
        assert(direction*(position-a.target)<2);
        assert(direction*(a.target-position)<4);
        assert(a.speedOutputPercent-std::max(17.f,previousOutput)<=17.f/1.2f*dt+.001f);
      }
      if(t>18000)assert(direction*velocity>4 && direction*velocity<6);
      previousOutput=a.speedOutputPercent;
    }
    c.stopNow(-1,20001);assert(!c.moving() && !c.axes[axis].speedOutputPercent);
  }
}
int main() {
  kickMustRespectMovingGoal();
  boundedLongPeriodPrediction();
  cadenceCanSlowTo1500();
  streamRecoversFromStiction();
  kickCannotSeedFastStreamTrajectory();
  streamAfterFirstArrival();
  switchingNoiseDoesNotEndKick();
  stictionStream();
  movingTargetOverrun();
  burstDelivery(0);burstDelivery(0xffffff00u);
  signedPstRange();
  feedbackObservationTime();
  overspeedMayBrake();
  feedforwardCruise();
  for(unsigned hz:{50u,100u,200u})for(unsigned spacing:{200u,400u,1000u,1500u})for(bool jitter:{false,true})for(int sign:{-1,1})
    for(uint32_t epoch:{0u,0xffffff00u})stream(hz,spacing,jitter,epoch,sign);
  std::cout<<"PASS streaming: 200/400/1000/1500 ms goals, jitter, both axes/directions, 50/100/200 Hz, rollover, final duplicates, STOP\n";
}
