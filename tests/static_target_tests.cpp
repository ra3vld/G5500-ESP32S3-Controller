#include "control.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace forkrot;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x))throw std::runtime_error(std::to_string(__LINE__)+": " #x);}while(0)
static Controller ready(unsigned hz,unsigned axis,int direction,uint32_t epoch) {
  Controller c;c.hardware.drive=DRIVE_BTS7960;c.hardware.simulation=false;
  c.setControlFrequency(hz,epoch);c.setMode(Mode::Auto,epoch);
  for(unsigned i=0;i<2;++i)c.feedbackAt(i,90,true,epoch);
  auto &m=c.motionSettings[axis];m.mode=RegulatorMode::Cascade;m.kickEnabled=true;
  m.outputLimitPercent=50;m.kickPercent=35;m.movementDegrees=3;m.velocity=88.57f;
  m.acceleration=m.deceleration=5;m.jerk=9.56f;m.positionKp=.48f;m.positionKi=m.positionKd=0;
  m.velocityKp=.06f;m.velocityKi=.4f;m.velocityKd=0;m.feedforward=.44f;
  m.positionTolerance=3;m.stoppedVelocity=2.46f;m.sustainingPercent=20;
  m.kickMs=2427;m.kickReleaseMs=20;m.velocityWindowMs=240;m.stallMs=2500;
  CHECK(c.target({90+direction*4.f,90+direction*4.f,axis==0,axis==1},false,epoch)==Result::Ok);
  return c;
}
static void repeatedBrakingHasOneDeadline() {
  for(unsigned hz:{50u,100u,200u})for(unsigned axis:{0u,1u})
  for(int direction:{-1,1})for(uint32_t epoch:{0u,0xfffff000u}) {
    auto c=ready(hz,axis,direction,epoch);
    c.motionSettings[axis].kickEnabled=false;
    auto &f=const_cast<MotionFeedback&>(c.motionFeedback(axis));
    f.coastSeconds=2;f.staticCoastFactor={.5f,.5f};
    unsigned runs=0,brakes=0;auto previous=MotionPhase::Idle;bool faulted=false;
    for(unsigned t=0;t<=6000;t+=1000/hz) {
      const float p=90+.9f*std::sin(float(t)*6.2831853f/1600);
      c.feedbackAt(axis,p,true,epoch+t);c.feedbackAt(1-axis,90,true,epoch+t);
      // Duplicates must not turn this fixed coordinate into a moving stream
      // or renew either the static-goal window or its progress deadline.
      if(t%200==0)CHECK(c.target({90+direction*4.f,90+direction*4.f,axis==0,axis==1},false,epoch+t)==Result::Ok);
      c.tick(epoch+t);const auto &a=c.axes[axis];
      CHECK(c.axes[1-axis].speedOutputPercent==0);
      if(a.motionPhase!=previous) {
        runs+=a.motionPhase==MotionPhase::Run;brakes+=a.motionPhase==MotionPhase::Braking;
        previous=a.motionPhase;
      }
      if(a.motionError!=MotionError::None) {
        CHECK(a.motionError==MotionError::Stalled);CHECK(t>=2500 && t<=5900);
        CHECK(!a.direction && !a.tracking && !a.speedOutputPercent);
        CHECK(runs>1 && brakes>1);faulted=true;break;
      }
    }
    CHECK(faulted);
  }
}
static void stationaryNoiseCannotPredictCoast() {
  for(unsigned hz:{50u,100u,200u})for(unsigned axis:{0u,1u})for(int direction:{-1,1}) {
    auto c=ready(hz,axis,direction,0);c.motionSettings[axis].stallMs=20000;
    auto &f=const_cast<MotionFeedback&>(c.motionFeedback(axis));
    f.coastSeconds=2;f.staticCoastFactor={2,2};
    for(unsigned t=0;t<=5000;t+=1000/hz) {
      c.feedbackAt(axis,90+.35f*std::sin(float(t)*6.2831853f/1200),true,t);
      c.feedbackAt(1-axis,90,true,t);c.tick(t);
      CHECK(c.axes[axis].motionError==MotionError::None);
      if(t>=2200) {
        CHECK(std::abs(c.axes[axis].measuredVelocity)<c.motionSettings[axis].stoppedVelocity);
        CHECK(c.axes[axis].motionPhase==MotionPhase::Kick);
        CHECK(c.axes[axis].speedOutputPercent>0);
      }
    }
  }
}
static void unconfirmedKickMustNotCoast() {
  for(unsigned hz:{50u,100u,200u})for(unsigned axis:{0u,1u})
  for(int direction:{-1,1})for(uint32_t epoch:{0u,0xfffff000u}) {
    auto c=ready(hz,axis,direction,epoch);c.motionSettings[axis].stallMs=20000;
    auto &f=const_cast<MotionFeedback&>(c.motionFeedback(axis));
    f.coastSeconds=2;f.staticCoastFactor={.5f,.5f};
    bool apparentMotion=false;
    for(unsigned t=0;t<=6000;t+=1000/hz) {
      // Switching feedback can exceed stoppedVelocity and the coast gate's
      // short-window displacement while never confirming 3 degrees of start.
      const float noise=t<2000 ? 0 : .9f*std::sin(float(t-2000)*6.2831853f/1600);
      c.feedbackAt(axis,90+noise,true,epoch+t);c.feedbackAt(1-axis,90,true,epoch+t);c.tick(epoch+t);
      const auto &a=c.axes[axis];CHECK(a.motionError==MotionError::None);
      if(t>=2000) {
        apparentMotion|=std::abs(a.measuredVelocity)>c.motionSettings[axis].stoppedVelocity;
        CHECK(a.motionPhase==MotionPhase::Kick);CHECK(a.speedOutputPercent>0);
      }
    }
    CHECK(apparentMotion);c.stopNow(-1,epoch+6001);CHECK(!c.moving());
  }
}
static void kickStillStopsAtGoal() {
  for(unsigned axis:{0u,1u})for(int direction:{-1,1})for(float travel:{1.2f,8.f}) {
    auto c=ready(200,axis,direction,0);c.motionSettings[axis].stallMs=20000;
    for(unsigned t=0;t<=2000;t+=5) {
      c.feedbackAt(axis,90,true,t);c.feedbackAt(1-axis,90,true,t);c.tick(t);
    }
    CHECK(c.axes[axis].motionPhase==MotionPhase::Kick);
    c.feedbackAt(axis,90+direction*travel,true,2040);c.feedbackAt(1-axis,90,true,2040);c.tick(2040);
    CHECK(c.axes[axis].motionError==MotionError::None);
    CHECK(!c.axes[axis].direction && !c.axes[axis].speedOutputPercent);
    for(unsigned t=2045;t<=2800;t+=5) {
      c.feedbackAt(axis,90+direction*travel,true,t);c.feedbackAt(1-axis,90,true,t);c.tick(t);
      CHECK(!c.axes[axis].direction && !c.axes[axis].speedOutputPercent);
    }
    CHECK(c.axes[axis].completed);
  }
}
static void shortMoveStartsDespiteSwitchingNoise() {
  for(unsigned hz:{50u,100u,200u})for(unsigned axis:{0u,1u})for(int direction:{-1,1}) {
    auto c=ready(hz,axis,direction,0);c.motionSettings[axis].stallMs=20000;
    auto &f=const_cast<MotionFeedback&>(c.motionFeedback(axis));
    f.coastSeconds=2;f.staticCoastFactor={.5f,.5f};
    float position=90,velocity=0;bool wasPowered=false,completed=false;unsigned poweredAt=0;
    for(unsigned t=0;t<=12000;t+=1000/hz) {
      const auto &a=c.axes[axis];const bool powered=a.direction && a.speedOutputPercent>0;
      if(powered && !wasPowered)poweredAt=t;
      // This plant needs 30% to break away. Sub-threshold PWM injects a
      // repeatable feedback excursion without any mechanical displacement.
      const float wanted=a.speedOutputPercent>=30 ? a.direction*6.f : 0;
      velocity+=(wanted-velocity)*(.001f*(1000/hz)/.1f);
      position+=velocity*.001f*(1000/hz);
      const float noise=powered ? .9f*std::sin(float(t-poweredAt)*6.2831853f/1600) : 0;
      wasPowered=powered;
      c.feedbackAt(axis,position+noise,true,t);c.feedbackAt(1-axis,90,true,t);c.tick(t);
      CHECK(c.axes[axis].motionError==MotionError::None);
      CHECK(c.axes[axis].speedOutputPercent<=c.motionSettings[axis].kickPercent);
      CHECK(!c.axes[1-axis].direction && !c.axes[1-axis].speedOutputPercent);
      if(c.axes[axis].completed) {
        CHECK(direction*(position-90)>=1);
        CHECK(std::abs(c.axes[axis].target-position)<=c.motionSettings[axis].positionTolerance);
        CHECK(!c.axes[axis].direction && !c.axes[axis].speedOutputPercent);
        completed=true;break;
      }
    }
    if(!completed)std::cerr<<"short move hz="<<hz<<" axis="<<axis<<" direction="<<direction<<" position="<<position
      <<" phase="<<motionPhaseName(c.axes[axis].motionPhase)<<" output="<<c.axes[axis].speedOutputPercent
      <<" feedback="<<c.axes[axis].current<<" velocity="<<c.axes[axis].measuredVelocity<<'\n';
    CHECK(completed);
  }
}
static void failedArrivalRetriesRemainBounded() {
  for(int direction:{-1,1}) {
    auto c=ready(200,0,direction,0);c.motionSettings[0].stallMs=8000;
    CHECK(c.target({90+direction*3.6f,0,true,false},false,0)==Result::Ok);
    auto &f=const_cast<MotionFeedback&>(c.motionFeedback(0));f.coastSeconds=2;f.staticCoastFactor={.5f,.5f};
    bool wasPowered=false,faulted=false;unsigned poweredAt=0;float peak=0;
    for(unsigned t=0;t<=15000;t+=5) {
      const auto &a=c.axes[0];const bool powered=a.direction && a.speedOutputPercent>0;
      if(powered && !wasPowered)poweredAt=t;
      const float noise=powered ? .9f*std::sin(float(t-poweredAt)*6.2831853f/1600) : 0;
      wasPowered=powered;
      c.feedbackAt(0,90+noise,true,t);c.feedbackAt(1,90,true,t);c.tick(t);
      peak=std::max(peak,c.axes[0].speedOutputPercent);
      CHECK(peak<=c.motionSettings[0].kickPercent);CHECK(!c.axes[0].completed);
      if(c.axes[0].motionError!=MotionError::None) {
        CHECK(c.axes[0].motionError==MotionError::Stalled);
        CHECK(!c.axes[0].direction && !c.axes[0].speedOutputPercent);faulted=true;break;
      }
    }
    CHECK(faulted);CHECK(peak==c.motionSettings[0].kickPercent);
  }
}
static void lowOutputStillExpires() {
  auto c=ready(200,0,1,0);auto &m=c.motionSettings[0];
  m.kickEnabled=false;m.stallMs=500;m.sustainingPercent=0;m.outputLimitPercent=6;
  for(unsigned t=0;t<=2300;t+=5) {
    c.feedbackAt(0,90,true,t);c.feedbackAt(1,90,true,t);c.tick(t);
    if(t<2300)CHECK(c.axes[0].motionError==MotionError::None);
  }
  CHECK(c.axes[0].motionError==MotionError::Stalled);CHECK(!c.moving());
  CHECK(c.target({100,0,true,false},false,2305)==Result::Ok);
  c.feedbackAt(0,90,true,2305);c.tick(2305);CHECK(c.axes[0].motionError==MotionError::None);
  c.stopNow(-1,2306);CHECK(!c.axes[0].speedOutputPercent && !c.axes[0].tracking);
}
static void stoppedStreamBecomesStatic() {
  auto c=ready(200,0,1,0);auto &m=c.motionSettings[0];
  m.kickEnabled=false;m.stallMs=500;m.sustainingPercent=0;m.outputLimitPercent=6;
  float goal=94;
  for(unsigned t=0;t<=6300;t+=5) {
    if(t%200==0) {
      if(t<=4000)goal=94+t*.001f;
      CHECK(c.target({goal,0,true,false},false,t)==Result::Ok);
    }
    c.feedbackAt(0,90,true,t);c.feedbackAt(1,90,true,t);c.tick(t);
    if(t<6300)CHECK(c.axes[0].motionError==MotionError::None);
  }
  CHECK(c.axes[0].motionError==MotionError::Stalled);CHECK(!c.moving());
}
static void slowProgressAndNewGoal() {
  auto c=ready(200,0,1,0);auto &m=c.motionSettings[0];
  m.kickEnabled=false;m.stallMs=500;m.sustainingPercent=0;m.outputLimitPercent=6;
  CHECK(c.target({100,0,true,false},false,0)==Result::Ok);
  for(unsigned t=0;t<=8000;t+=5) {
    c.feedbackAt(0,90+t*.0005f,true,t);c.feedbackAt(1,90,true,t);c.tick(t);
    CHECK(c.axes[0].motionError==MotionError::None);
  }
  c.stopNow(-1,8001);CHECK(!c.moving());
  c=ready(200,0,1,0);c.motionSettings[0].kickEnabled=false;c.motionSettings[0].stallMs=500;
  c.motionSettings[0].outputLimitPercent=6;c.motionSettings[0].sustainingPercent=0;
  for(unsigned t=0;t<4300;t+=5) {
    if(t==2000)CHECK(c.target({100,0,true,false},false,t)==Result::Ok);
    c.feedbackAt(0,90,true,t);c.feedbackAt(1,90,true,t);c.tick(t);
    CHECK(c.axes[0].motionError==MotionError::None);
  }
  c.feedbackAt(0,90,true,4300);c.tick(4300);
  CHECK(c.axes[0].motionError==MotionError::Stalled);
}
static void learnDirectionalCoast() {
  Controller c;c.hardware.drive=DRIVE_BTS7960;c.hardware.simulation=false;
  auto &m=c.motionSettings[0];m.velocityWindowMs=80;m.stoppedVelocity=.1f;m.movementDegrees=.2f;
  unsigned start=0;
  for(int direction:{1,-1}) {
    c.axes[0].direction=direction;c.axes[0].speedOutputPercent=20;
    for(unsigned t=0;t<=1000;t+=5)c.feedbackAt(0,90+direction*float(t)*.01f,true,start+t);
    c.axes[0].direction=0;c.axes[0].speedOutputPercent=0;
    const float deceleration=direction>0 ? 5.f : 10.f;
    for(unsigned t=5;t<=2500;t+=5) {
      const float elapsed=std::min(t/1000.f,10/deceleration);
      const float travel=10*elapsed-.5f*deceleration*elapsed*elapsed;
      c.feedbackAt(0,90+direction*(10+travel),true,start+1000+t);
    }
    const auto &f=c.motionFeedback(0);const float distance=f.staticCoastDistance(direction*10.f);
    if(!(distance>(direction>0?8.f:4.f) && distance<(direction>0?12.f:6.f)))
      std::cerr<<"learned "<<direction<<" distance="<<distance<<" coast="<<f.coastSeconds<<" velocity="<<f.coastVelocity<<'\n';
    CHECK(distance>(direction>0?8.f:4.f) && distance<(direction>0?12.f:6.f));
    CHECK(std::abs(f.staticCoastDistance(direction*5.f)-distance/4)<.0001f);
    if(direction>0)CHECK(f.staticCoastDistance(-10)==0);
    start+=4000;
  }
  CHECK(c.motionFeedback(0).staticCoastDistance(10)>c.motionFeedback(0).staticCoastDistance(-10));
  c.feedbackAt(0,NAN,false,start);
  CHECK(c.motionFeedback(0).staticCoastDistance(10)==0);
}
static void speedChangesCoastDecision() {
  for(unsigned hz:{50u,100u,200u})for(int direction:{-1,1})for(float speed:{5.f,10.f}) {
    auto c=ready(hz,0,direction,0);auto &m=c.motionSettings[0];
    m.kickEnabled=false;m.stallMs=30000;m.stoppedVelocity=.1f;m.movementDegrees=.2f;
    CHECK(c.target({90+direction*6.f,0,true,false},false,0)==Result::Ok);
    auto &f=const_cast<MotionFeedback&>(c.motionFeedback(0));f.staticCoastFactor={.1f,.1f};
    for(unsigned t=0;t<=2000;t+=1000/hz) {
      c.feedbackAt(0,90-direction*speed*(2000-t)/1000,true,t);c.feedbackAt(1,90,true,t);c.tick(t);
    }
    CHECK(c.axes[0].motionError==MotionError::None);
    CHECK((c.axes[0].motionPhase==MotionPhase::Braking)==(speed==10));
  }
}
int main() {
  try {
    unconfirmedKickMustNotCoast();kickStillStopsAtGoal();shortMoveStartsDespiteSwitchingNoise();
    failedArrivalRetriesRemainBounded();
    repeatedBrakingHasOneDeadline();stationaryNoiseCannotPredictCoast();lowOutputStillExpires();
    stoppedStreamBecomesStatic();slowProgressAndNewGoal();
    learnDirectionalCoast();speedChangesCoastDecision();
    std::cout<<"PASS static target: "<<checks<<" checks\n";
  } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
