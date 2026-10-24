#include "control.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace forkrot;
static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::to_string(__LINE__)+": " #x); } while(0)

static Controller ready(unsigned axis,unsigned hz,uint32_t epoch,float position=90,bool hold=true) {
  Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
  CHECK(c.setControlFrequency(hz,epoch)==Result::Ok);
  CHECK(c.setMode(Mode::Auto,epoch)==Result::Ok);
  auto &m=c.motionSettings[axis];
  m.mode=RegulatorMode::Cascade;m.holdEnabled=hold;m.kickEnabled=true;
  m.outputLimitPercent=50;m.kickPercent=35;m.movementDegrees=axis?2:3;
  m.velocity=axis?76.13f:88.57f;m.acceleration=m.deceleration=5;m.jerk=axis?20:9.56f;
  m.positionKp=axis?.5f:.48f;m.positionKi=m.positionKd=0;
  m.velocityKp=axis?.12f:.06f;m.velocityKi=axis?1:.4f;m.velocityKd=0;m.feedforward=axis?.1f:.44f;
  m.positionTolerance=4;m.holdResumeDegrees=1;m.stoppedVelocity=axis?3.83f:2.46f;
  m.sustainingPercent=axis?16:20;m.kickMs=axis?400:427;m.kickReleaseMs=20;
  m.velocityWindowMs=240;m.settleMs=300;m.stallMs=20000;
  CHECK(m.valid());
  c.feedbackAt(axis,position,true,epoch);c.feedbackAt(1-axis,90,true,epoch);
  CHECK(c.target({90,90,axis==0,axis==1},false,epoch)==Result::Ok);
  return c;
}

static void smallDisturbance(unsigned axis,unsigned hz,int direction,uint32_t epoch) {
  auto c=ready(axis,hz,epoch);const unsigned period=1000/hz;
  for(unsigned t=0;t<1000;t+=period) {c.feedbackAt(axis,90,true,epoch+t);c.tick(epoch+t);}
  CHECK(c.axes[axis].motionPhase==MotionPhase::Holding);
  // Noise and drift inside the hysteresis must not produce repeated starts,
  // even though the feedback velocity is not necessarily exactly zero.
  for(unsigned t=1000;t<2000;t+=period) {
    c.feedbackAt(axis,90+direction*(.7f+((t/period)%2?.03f:-.03f)),true,epoch+t);c.tick(epoch+t);
    CHECK(c.axes[axis].speedOutputPercent==0 && c.axes[axis].holdArmed);
  }
  bool corrected=false;
  for(unsigned t=2000;t<=2600;t+=period) {
    const float position=90+direction*(.7f+.001f*(t-2000));
    c.feedbackAt(axis,position,true,epoch+t);c.tick(epoch+t);
    const auto &a=c.axes[axis];CHECK(a.motionError==MotionError::None);
    if(std::abs(position-90)<=1)CHECK(a.speedOutputPercent==0);
    else {
      CHECK(a.direction==-direction && a.speedOutputPercent>0);
      CHECK(t<=2300+period); // First accepted sample outside 1 degree.
      corrected=true;break;
    }
  }
  CHECK(corrected);
  c.stopNow(int(axis),epoch+2700);
  c.feedbackAt(axis,90+direction*2.f,true,epoch+2700);c.tick(epoch+2700);
  CHECK(!c.axes[axis].holdArmed && !c.axes[axis].direction && c.axes[axis].speedOutputPercent==0);
}

static void movingOvershoot(unsigned axis,unsigned hz,int direction,uint32_t epoch,bool hold,unsigned pause) {
  auto c=ready(axis,hz,epoch,90-direction*10.f,hold);const unsigned period=1000/hz;
  c.motionSettings[axis].holdReverseMs=pause;
  int off=-1,correction=-1,outside=-1;bool correctedWhileMoving=false,recoveredStart=false;
  for(unsigned t=0;t<=2000;t+=period) {
    const float position=90+direction*(-10+20*std::min(t,1000u)/1000.f);
    c.feedbackAt(axis,position,true,epoch+t);c.tick(epoch+t);const auto &a=c.axes[axis];
    CHECK(a.motionError==MotionError::None);
    if(t>200 && off<0 && !a.direction)off=int(t);
    if(direction*(position-90)>1 && outside<0)outside=int(t);
    if(direction*(position-90)>=0)CHECK(a.direction!=direction || a.speedOutputPercent==0);
    if(a.direction==-direction && a.speedOutputPercent>0 && correction<0) {
      correction=int(t);
      correctedWhileMoving=direction*a.measuredVelocity>c.motionSettings[axis].stoppedVelocity;
      if(correctedWhileMoving)CHECK(a.motionPhase!=MotionPhase::Kick);
    }
    if(t>=1000 && t<=1500 && a.motionPhase==MotionPhase::Kick)recoveredStart=true;
  }
  CHECK(off>=0 && outside>=0);
  if(hold) {
    const int earliest=std::max(off+int(pause),outside);
    CHECK(correction>=earliest);
    CHECK(correction<=earliest+int(period));
    if(pause<=300) {
      CHECK(correctedWhileMoving); // No velocity-window or SettleMs wait.
      CHECK(recoveredStart); // A stuck shaft does not wait for PI windup.
    }
  } else {
    CHECK(correction<0 && c.axes[axis].completed && !c.axes[axis].tracking);
  }
}

static void learnMovingCoast(unsigned axis,unsigned hz,int direction,uint32_t epoch) {
  auto c=ready(axis,hz,epoch);const unsigned period=1000/hz;
  c.axes[axis].direction=direction;c.axes[axis].speedOutputPercent=20;
  for(unsigned t=0;t<=1000;t+=period)c.feedbackAt(axis,90+direction*.02f*t,true,epoch+t);
  c.axes[axis].direction=0;c.axes[axis].speedOutputPercent=0;
  for(unsigned t=period;t<=400;t+=period) {
    const float travel=20*.24f*(1-std::exp(-float(t)/240.f));
    c.feedbackAt(axis,90+direction*(20+travel),true,epoch+1000+t);
  }
  const auto &f=c.motionFeedback(axis);
  CHECK(direction*f.velocity>c.motionSettings[axis].stoppedVelocity);
  CHECK(f.coastSeconds>.2f && f.coastSeconds<.6f);
}

int main() {
  try {
    for(unsigned axis:{0u,1u})for(unsigned hz:{50u,100u,200u})for(int direction:{-1,1})for(uint32_t epoch:{0u,0xfffffe00u}) {
      smallDisturbance(axis,hz,direction,epoch);
      for(unsigned pause:{0u,35u,300u,700u})movingOvershoot(axis,hz,direction,epoch,true,pause);
      movingOvershoot(axis,hz,direction,epoch,false,0); // Disabling servo pause does not alter one-shot reversal.
      learnMovingCoast(axis,hz,direction,epoch);
    }
    std::cout<<"PASS servo holding: "<<checks<<" checks; 1-degree response, noise hysteresis, moving overshoot, optional 0/35/300/700 ms reverse pause, one-shot, STOP, both axes/directions, 50/100/200 Hz, rollover\n";
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
