#include "control.hpp"
#include "motion_json.hpp"
#include "api.hpp"
#include "position_filter.hpp"
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
  float negativeSustaining=0,negativeGain=1,quantum=0,timeConstant=.12f,rippleBelow=0,lowerStart=0,lowerStartBelow=0;
  float dragOutput=0,dragFrom=0,dragTo=0,load=0;
  bool jam=false,medianFeedback=false;
  PositionFilter filter;
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
    const float start=lowerStart>0 && position<lowerStartBelow?lowerStart:a.direction<0?negativeStart:positiveStart;
    float minimum=a.direction<0 && negativeSustaining>0?negativeSustaining:sustaining;
    if(position>=dragFrom && position<=dragTo)minimum=std::max(minimum,dragOutput);
    float desired=a.direction*(u>=minimum?gain*(a.direction<0?negativeGain:1)*std::max(0.f,u-bias-load):0);
    if(u<rippleBelow)desired*=1+.45f*std::sin(float(now)*.009f);
    if(jam || (std::abs(velocity)<.01f && u+.001f<start+load))desired=0;
    velocity+=(desired-velocity)*(.02f/timeConstant);
    if(std::abs(velocity)<.003f && desired==0)velocity=0;
    position+=velocity*.02f;now+=20;
    if(fresh){
      const float measured=medianFeedback?filter.sample(int(std::round(position/quantum)),now)*quantum:
          quantum>0?std::round(position/quantum)*quantum:position;
      c.feedbackAt(axis,measured,true,now);c.feedbackAt(1-axis,90,true,now);
    }
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
  bool sawMinimum=false;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n){r.step();sawMinimum|=r.c.autoTune.phase==AutoTunePhase::Minimum;CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);CHECK(!r.c.axes[1-axis].direction);}
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"tune "<<cascade<<" axis "<<axis<<" phase "<<Controller::autoTunePhaseName(r.c.autoTune.phase)<<" reason "<<r.c.autoTune.reason<<" motion "<<r.c.fault<<" pos "<<r.position<<" low0 "<<r.c.autoTune.measurements[0].lowVelocity<<" high0 "<<r.c.autoTune.measurements[0].highVelocity<<" low1 "<<r.c.autoTune.measurements[1].lowVelocity<<" high1 "<<r.c.autoTune.measurements[1].highVelocity<<" error "<<r.c.autoTune.validationError<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.autoTune.proposedMotion.valid());
  CHECK(sawMinimum);CHECK(r.c.autoTune.minimumConfirm);CHECK(r.c.autoTune.minimumSpeed>0);
  CHECK(r.c.autoTune.validationPass==1);
  CHECK(r.c.motionSettings[axis]==original);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());CHECK(!r.c.axes[axis].holdArmed);
  CHECK(r.c.autoTune.measurements[0].startOutput>20);CHECK(r.c.autoTune.measurements[1].startOutput>20);
  CHECK(r.c.autoTune.proposedMotion.kickPercent<=50);CHECK(!r.c.autoTune.proposedMotion.holdEnabled);
  if(cascade){CHECK(r.c.autoTune.proposedSpeed.speedPercent>=25);CHECK(r.c.autoTune.proposedMotion.velocityKp>0);CHECK(r.c.autoTune.proposedMotion.velocityKi>0);CHECK(r.c.autoTune.proposedMotion.positionKp>0);}
  char out[4096];JsonWriter j(out,sizeof out);CHECK(writeAutoTune(j,r.c,true));CHECK(std::string(j.data()).find("measurements")!=std::string::npos);CHECK(std::string(j.data()).find("peakVelocity")!=std::string::npos);CHECK(std::string(j.data()).find("stoppingDistance")!=std::string::npos);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id+1,r.now)==Result::Unavailable);
  const auto proposed=r.c.autoTune.proposedMotion;CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Ok);
  CHECK(r.c.motionSettings[axis]==proposed);CHECK(!r.c.moving());CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
  if(cascade)CHECK(r.c.speeds[axis]==speed);
  CHECK(r.c.runtimeSpeed(axis)<=r.c.runtimeSpeedLimit(axis));
  // The autotune selects the verified slow point. These load-rejection checks
  // deliberately exercise the separately identified upper working speed.
  CHECK(r.c.setRuntimeSpeed(axis,r.c.runtimeSpeedLimit(axis),r.now)==Result::Ok);
  if(cascade)for(int direction:{-1,1}) {
    r.c.stopNow(-1,r.now);r.position=90;r.velocity=0;r.filter={};r.c.resetFeedback(int(axis),r.now);r.c.feedbackAt(axis,90,true,r.now);
    for(unsigned n=0;n<30;++n)r.step();
    CHECK(r.c.jog(axis,direction,false,r.now)==Result::Ok);r.c.tick(r.now);
    for(unsigned n=0;n<150;++n) {
      r.step();CHECK(r.c.axes[axis].motionError==MotionError::None);
      if(n>25)CHECK(std::abs(r.velocity)>.05f); // sustained rotation, not just a lucky endpoint
    }
    r.load=5;
    for(unsigned n=0;n<100;++n) {
      r.step();CHECK(r.c.axes[axis].motionError==MotionError::None);
      if(n>75)CHECK(std::abs(r.velocity)>.75f*proposed.velocity);
    }
    r.load=0;
    for(unsigned n=0;n<100;++n)r.step();
    CHECK(std::abs(r.velocity)>.75f*proposed.velocity);
    r.c.stopNow(-1,r.now);CHECK(!r.c.moving());
  }

}
static void safety() {
  {
    Rig probe(true);const auto original=probe.c.motionSettings[0];
    const auto speeds=probe.c.speeds;const auto overrides=probe.c.runtimeSpeeds.values;
    CHECK(probe.start()==Result::Ok);
    for(unsigned n=0;n<6000 && probe.c.autoTune.active() && probe.c.autoTune.phase!=AutoTunePhase::Minimum;++n)probe.step();
    CHECK(probe.c.autoTune.phase==AutoTunePhase::Minimum);
    CHECK(probe.c.setControlFrequency(200,probe.now)==Result::Busy);
    CHECK(probe.c.applyAutoTune(probe.c.autoTune.id,probe.now)==Result::Unavailable);
    probe.c.stopNow(-1,probe.now);
    CHECK(probe.c.autoTune.phase==AutoTunePhase::Cancelled);CHECK(!probe.c.moving());
    CHECK(probe.c.motionSettings[0]==original);CHECK(probe.c.speeds==speeds);
    CHECK(probe.c.runtimeSpeeds.values==overrides);
  }
  {
    Rig probe;CHECK(probe.start()==Result::Ok);
    for(unsigned n=0;n<6000 && probe.c.autoTune.active() &&
        !(probe.c.autoTune.phase==AutoTunePhase::Minimum && probe.c.autoTune.minimumSamples>5);++n)probe.step();
    CHECK(probe.c.autoTune.phase==AutoTunePhase::Minimum);
    const float rejected=probe.c.autoTune.minimumCandidate;
    probe.jam=true;probe.velocity=0;
    for(unsigned n=0;n<60 && probe.c.autoTune.phase==AutoTunePhase::Minimum;++n)probe.step();
    CHECK(probe.c.autoTune.phase!=AutoTunePhase::Minimum);
    CHECK(probe.c.autoTune.minimumSpeed==0); // a brief start cannot qualify a paused shaft
    CHECK(probe.c.autoTune.minimumCandidate>rejected || probe.c.autoTune.phase==AutoTunePhase::Failed);
    probe.c.stopNow(-1,probe.now);
  }
  Rig r;const auto m=r.c.motionSettings[0];const auto s=r.c.speeds[0];CHECK(r.start()==Result::Ok);
  for(unsigned n=0;n<65;++n)r.step();
  r.c.stopNow(-1,r.now);
  CHECK(r.c.autoTune.phase==AutoTunePhase::Cancelled);CHECK(!r.c.moving());CHECK(r.c.motionSettings[0]==m);CHECK(r.c.speeds[0]==s);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
  r=Rig();r.jam=true;CHECK(r.start()==Result::Ok);for(unsigned n=0;n<1000 && r.c.autoTune.active();++n)r.step();
  CHECK(r.now<20000);
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
  for(unsigned n=0;n<1000 && r.c.autoTune.active();++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(r.now<12000);CHECK(!r.c.moving());
}
static void highStiction() {
  Rig r;r.positiveStart=r.negativeStart=50;r.sustaining=5;r.gain=.134f;r.bias=0;r.c.speeds[0].speedPercent=5;
  CHECK(r.start()==Result::Ok);
  for(unsigned n=0;n<6000&&r.c.autoTune.active();++n)r.step();
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"high stiction "<<r.c.autoTune.reason<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.autoTune.proposedSpeed.speedPercent>=25);
  CHECK(r.c.autoTune.proposedMotion.kickPercent==50);
}
// Short saved pulses plus delayed, quantized feedback used to reject later
// restarts even after the shaft had travelled tens of degrees during the run.
static void startupConfirmation(unsigned axis,float movement,uint32_t stamp=0) {
  Rig r(false,axis,stamp);r.positiveStart=r.negativeStart=28;r.sustaining=16;
  r.gain=.5f;r.bias=13;r.timeConstant=.24f;r.quantum=.1f;r.medianFeedback=true;
  auto &m=r.c.motionSettings[axis];m.kickEnabled=true;m.kickPercent=34;m.kickMs=300;
  m.movementDegrees=movement;m.stoppedVelocity=4.84f;m.velocityWindowMs=80;
  m.stallMs=20000;m.outputLimitPercent=50;m.positionTolerance=5;m.holdResumeDegrees=5.38f;m.kickReleaseMs=20;
  r.c.speeds[axis].speedPercent=50;r.c.speeds[axis].coastDegrees=1.7f;
  const auto saved=m;const auto speed=r.c.speeds[axis];CHECK(r.start(75)==Result::Ok);
  bool extendedSearch=false,extendedKick=false,confirmedKick=false;
  uint32_t kickAt=stamp;float kickPosition=0;int kickDirection=0;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    const auto phase=r.c.axes[axis].motionPhase;
    const auto tunePhase=r.c.autoTune.phase;
    r.step();const auto &a=r.c.axes[axis];const auto &t=r.c.autoTune;
    CHECK(a.motionError==MotionError::None);CHECK(a.speedOutputPercent<=50.001f);
    CHECK(r.position>=t.lower && r.position<=t.upper);CHECK(!r.c.axes[1-axis].direction);
    if(t.phase==AutoTunePhase::Start && uint32_t(r.now-t.phaseAt)>saved.kickMs)extendedSearch=true;
    if(a.motionPhase==MotionPhase::Kick && phase!=MotionPhase::Kick) {
      kickAt=r.now;kickPosition=a.current;kickDirection=a.direction;
    }
    if(a.motionPhase==MotionPhase::Kick && t.phase!=AutoTunePhase::Validate &&
        uint32_t(r.now-kickAt)>saved.kickMs)extendedKick=true;
    if(phase==MotionPhase::Kick && a.motionPhase==MotionPhase::Run &&
        tunePhase!=AutoTunePhase::Validate) {
      CHECK((a.current-kickPosition)*kickDirection>=movement);confirmedKick=true;
    }
  }
  CHECK(extendedSearch);
  if(movement==1) {CHECK(extendedKick);CHECK(confirmedKick);}
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"startup "<<movement<<" axis "<<axis<<" reason "<<r.c.autoTune.reason<<" time "<<uint32_t(r.now-stamp)<<" candidate "<<r.c.autoTune.minimumCandidate<<" best "<<r.c.autoTune.minimumSpeed<<" output "<<r.c.autoTune.minimumOutput<<"\n";
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.autoTune.validationPass==1);
  for(const auto &p:r.c.autoTune.measurements) {
    CHECK(p.startOutput>=28 && p.startOutput<=35);CHECK(p.lowVelocity>0);CHECK(p.highVelocity>p.lowVelocity);
  }
  CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());CHECK(!r.c.axes[axis].holdArmed);
}
static void startupStallDeadline(bool jam) {
  Rig r;r.jam=jam;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickMs=300;m.stallMs=500;m.movementDegrees=5;
  const auto saved=m;const auto speed=r.c.speeds[0];AutoTuneOptions options;options.searchStartPercent=50;
  CHECK(r.c.startAutoTune(0,RegulatorMode::Simple,60,50,r.now,options)==Result::Ok);
  for(unsigned n=0;n<100 && r.c.autoTune.active();++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(std::string(r.c.autoTune.reason)=="no_start");
  CHECK(uint32_t(r.now-r.c.autoTune.phaseAt)==500);
  if(!jam)CHECK(r.position>90+saved.movementDegrees/4);
  CHECK(m==saved);CHECK(r.c.speeds[0]==speed);CHECK(!r.c.moving());
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
  if(travel<=20) {
    CHECK(r.c.autoTune.phase==AutoTunePhase::Complete ||
          (r.c.autoTune.phase==AutoTunePhase::Failed && std::string(r.c.autoTune.reason)=="insufficient_travel"));
    // A fresh breakaway search can discover a larger stopping reserve before
    // a transfer starts. Reject that short stroke rather than forcing a move.
    if(travel==20)CHECK(relocated || (r.c.autoTune.relocations>0 &&
        r.c.autoTune.phase==AutoTunePhase::Failed && std::string(r.c.autoTune.reason)=="insufficient_travel"));
    CHECK(r.c.motionSettings[axis]==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());return;
  }
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.motionSettings[axis]==saved);CHECK(r.c.speeds[axis]==speed);
  CHECK(r.c.autoTune.proposedSpeed.speedPercent>=18);CHECK(!r.c.moving());
  if(cascade) {
    CHECK(std::abs(r.c.autoTune.proposedMotion.velocity-std::max({r.c.autoTune.measurements[0].lowVelocity,r.c.autoTune.measurements[1].lowVelocity,std::min(r.c.autoTune.measurements[0].highVelocity,r.c.autoTune.measurements[1].highVelocity)}))<.001f);
    CHECK(r.c.autoTune.proposedMotion.positionTolerance==std::max(saved.positionTolerance,r.c.autoTune.proposedMotion.movementDegrees));
  }
}
static void inertialPlateaus(bool median=false) {
  Rig r;r.positiveStart=49;r.negativeStart=28;r.sustaining=17;r.negativeSustaining=18;
  r.gain=1.5f;r.bias=4;r.negativeGain=.8f;r.quantum=.14f;r.timeConstant=.3f;r.medianFeedback=median;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;
  r.c.speeds[0].coastDegrees=1.5f;
  CHECK(r.start(60)==Result::Ok);
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    r.step();CHECK(r.position>=30 && r.position<=150);
    // A decaying kick must never be recorded as a sustainable work plateau.
    for(unsigned d=0;d<2;++d) {
      const auto &p=r.c.autoTune.measurements[d];
      if(p.lowVelocity>0)CHECK(p.lowOutput>=(d?18:17));
    }
  }
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"inertial "<<r.c.autoTune.reason<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);
  CHECK(r.c.autoTune.proposedSpeed.speedPercent>=18);
}
static void marginalWorkOutput() {
  Rig r;r.positiveStart=r.negativeStart=28;r.sustaining=15;r.gain=.7f;r.bias=8;
  r.rippleBelow=19;r.quantum=.05f;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=.5f;m.stoppedVelocity=.2f;m.outputLimitPercent=50;
  CHECK(r.start(60)==Result::Ok);
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n)r.step();
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"marginal "<<r.c.autoTune.reason<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);
  CHECK(r.c.autoTune.proposedSpeed.speedPercent>=19);
}
static void adaptiveProbeStartup(float origin=120) {
  Rig r;r.positiveStart=r.negativeStart=50;r.lowerStart=28;r.lowerStartBelow=100;
  r.sustaining=16;r.negativeSustaining=18;r.gain=3;r.bias=13;r.quantum=.14f;r.medianFeedback=true;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;
  r.position=origin;r.now=20;r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,r.position,true,r.now);
  CHECK(r.start(60)==Result::Ok);bool releasedBelowCap=false,confirmedRestart=false;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    const auto old=r.c.axes[0].motionPhase;const float output=r.c.axes[0].speedOutputPercent;
    r.step();CHECK(r.position>=origin-60 && r.position<=origin+60);
    CHECK(r.c.axes[0].speedOutputPercent<=50.001f);
    if(old==MotionPhase::Kick && r.c.axes[0].motionPhase==MotionPhase::Run && r.c.axes[0].direction &&
        r.c.autoTune.phase!=AutoTunePhase::Validate) {
      const unsigned d=r.c.axes[0].direction>0?0:1;
      CHECK(r.c.autoTune.measurements[d].startOutput+.01f>=output);
      CHECK(r.c.autoTune.measurements[d].startMs>0);confirmedRestart=true;
      if(!d && output<40)releasedBelowCap=true;
    }
  }
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"adaptive startup "<<r.c.autoTune.reason<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(confirmedRestart);
  if(origin==120)CHECK(releasedBelowCap);
}
static void asymmetricCoast() {
  Rig r;r.positiveStart=r.negativeStart=25;r.sustaining=15;r.gain=1;r.bias=7;r.timeConstant=.06f;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=25;m.kickMs=1500;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;m.kickReleaseMs=20;
  r.position=51.38f;r.now=20;r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,r.position,true,r.now);
  CHECK(r.start(40)==Result::Ok);
  auto &t=r.c.autoTune;t.origin=70.14f;t.lower=30.14f;t.upper=110.14f;t.threshold=1;
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Validate;t.direction=-1;
  t.phasePosition=r.position+.88f;t.noiseLow=t.noiseHigh=r.position;t.stoppingDistance=2.81f;
  t.measurements[0]={25,17,20,20.74f,22.56f,.02f,2.81f,301};
  t.measurements[1]={25,15,17,9.3f,12.92f,.24f,.88f,541};
  for(unsigned n=0;n<100 && t.phase==AutoTunePhase::Coast;++n)r.step();
  CHECK(t.phase==AutoTunePhase::Validate);
  for(unsigned n=0;n<6000 && t.active();++n)r.step();
  if(t.phase!=AutoTunePhase::Complete)std::cerr<<"asymmetric coast "<<t.reason<<" error "<<t.validationError<<'\n';
  CHECK(t.phase==AutoTunePhase::Complete);CHECK(std::abs(t.validationError)<=2);
}
static void strongerRestart(bool reposition) {
  Rig r;r.positiveStart=50;r.sustaining=16;r.gain=3;r.bias=13;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;
  const auto saved=m;
  CHECK(r.start()==Result::Ok);
  auto &t=r.c.autoTune;t.phase=reposition?AutoTunePhase::Reposition:AutoTunePhase::Low;
  t.direction=reposition?-1:1;t.searchLevel=28;t.level=17;t.threshold=1;
  t.measurements[0].startOutput=28;t.measurements[0].startMs=250;
  m.kickEnabled=true;m.kickPercent=28;r.c.speeds[0].speedPercent=r.c.speeds[0].approachSpeedPercent=17;
  t.internal=true;CHECK(r.c.jog(0,1,false,r.now)==Result::Ok);t.internal=false;
  bool confirmed=false;
  t.nextPhase=AutoTunePhase::Low;t.nextDirection=-1;t.nextLevel=17;
  for(unsigned n=0;n<600 && t.active();++n) {
    const auto phase=r.c.axes[0].motionPhase;const float output=r.c.axes[0].speedOutputPercent;
    r.step();
    if(r.now<800)CHECK(r.c.axes[0].speedOutputPercent<=28.001f);
    if(phase==MotionPhase::Kick && r.c.axes[0].motionPhase==MotionPhase::Run) {
      CHECK(output>=50);CHECK(t.measurements[0].startOutput>=output);
      CHECK(t.measurements[0].startMs>250);confirmed=true;break;
    }
  }
  CHECK(confirmed);CHECK(t.measurements[1].startOutput==0);
  r.c.cancelAutoTune(r.now);CHECK(!r.c.moving());CHECK(m==saved);
}
// Captured EL: 16% sustains the measurement section but stalls later.
// The measured 17% high plateau provides working-output headroom.
static void workOutputHeadroom() {
  Rig r;r.positiveStart=r.negativeStart=25;r.sustaining=17;r.gain=.7f;r.bias=7;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=25;m.kickMs=1500;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;m.kickReleaseMs=20;
  r.position=78;r.now=20;r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,r.position,true,r.now);
  const auto saved=m;const auto speed=r.c.speeds[0];CHECK(r.start(40)==Result::Ok);
  auto &t=r.c.autoTune;t.origin=93.83f;t.lower=53.83f;t.upper=133.83f;t.threshold=2.54f;
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Validate;t.direction=-1;
  t.phasePosition=r.position+1.08f;t.noiseLow=t.noiseHigh=r.position;t.stoppingDistance=1.08f;
  t.measurements[0]={25,16,17,14.83f,16.04f,.3f,.99f,287};
  t.measurements[1]={25,16,17,8.62f,12.41f,.34f,1.08f,329};
  for(unsigned n=0;n<6000 && t.active();++n)r.step();
  if(t.phase!=AutoTunePhase::Complete)std::cerr<<"work headroom "<<t.reason<<" error "<<t.validationError<<'\n';
  CHECK(t.phase==AutoTunePhase::Complete);CHECK(std::abs(t.validationError)<=2);
  CHECK(m==saved);CHECK(r.c.speeds[0]==speed);CHECK(!r.c.moving());
}
static void repositionDrag(bool jam=false) {
  Rig r;r.positiveStart=r.negativeStart=28;r.sustaining=16;r.gain=3;r.bias=13;
  r.dragFrom=60;r.dragTo=75;r.dragOutput=25;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;
  r.c.speeds[0].approachSpeedPercent=18;const auto saved=m;const auto speed=r.c.speeds[0];
  CHECK(r.start()==Result::Ok);auto &t=r.c.autoTune;
  t.phase=AutoTunePhase::Reposition;t.threshold=1;t.searchLevel=28;t.direction=1;
  t.nextDirection=1;t.nextPhase=AutoTunePhase::Low;t.nextLevel=17;
  m=saved;r.c.speeds[0]=speed;t.internal=true;
  CHECK(r.c.target({40,0,true,false},false,r.now)==Result::Ok);t.internal=false;
  bool raised=false;unsigned kicks=0;
  for(unsigned n=0;n<1000 && t.phase==AutoTunePhase::Reposition;++n) {
    const auto phase=r.c.axes[0].motionPhase;
    if(jam && r.position<80)r.jam=true;
    r.step();raised|=r.c.speeds[0].speedPercent>speed.speedPercent;
    if(t.phase==AutoTunePhase::Reposition && phase!=MotionPhase::Kick && r.c.axes[0].motionPhase==MotionPhase::Kick)++kicks;
    CHECK(r.position>=t.lower && r.position<=t.upper);CHECK(r.c.axes[0].speedOutputPercent<=50.001f);
  }
  CHECK(raised);CHECK(kicks==1);
  if(jam) {
    CHECK(t.phase==AutoTunePhase::Failed);CHECK(r.c.axes[0].motionError==MotionError::None);
    CHECK(std::string(t.reason)=="no_sustained_motion");
    CHECK(r.now<6000);CHECK(!r.c.moving());
  } else {CHECK(t.phase==AutoTunePhase::Low);CHECK(r.position<60);r.c.cancelAutoTune(r.now);}
  CHECK(m==saved);CHECK(r.c.speeds[0]==speed);
}
// Captured AZ: a relocation goal only 2.25 degrees inside the trial let the
// gearbox coast across the lower edge. Exercise the actual transfer and stop.
static void repositionStoppingRoom() {
  Rig r;r.positiveStart=r.negativeStart=28;r.sustaining=16;r.gain=3;r.bias=13;
  r.timeConstant=.45f;r.quantum=.14f;r.medianFeedback=true;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;
  r.c.speeds[0].coastDegrees=1.5f;r.c.speeds[0].approachSpeedPercent=18;
  r.position=142;r.now=20;r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,r.position,true,r.now);
  const auto saved=m;const auto speed=r.c.speeds[0];CHECK(r.start()==Result::Ok);
  auto &t=r.c.autoTune;t.origin=94.79f;t.lower=34.79f;t.upper=154.79f;t.threshold=1;
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Low;t.direction=t.nextDirection=1;
  t.nextLevel=18;t.searchLevel=28;t.relocate=true;t.phasePosition=r.position;t.peakVelocity=60;
  for(unsigned n=0;n<100 && t.phase==AutoTunePhase::Coast;++n)r.step();
  CHECK(t.phase==AutoTunePhase::Reposition);
  for(unsigned n=0;n<2000 && t.phase==AutoTunePhase::Reposition;++n) {
    r.step();CHECK(r.position>=t.lower && r.position<=t.upper);
  }
  CHECK(t.phase==AutoTunePhase::Low);CHECK(r.position<90);
  r.c.cancelAutoTune(r.now);CHECK(m==saved);CHECK(r.c.speeds[0]==speed);CHECK(!r.c.moving());
}
// Captured AZ identification: a high kick is needed, but the last plateau
// ends only seven degrees from the origin. Validate work-speed stopping.
static void validationAfterStrongKick() {
  Rig r;r.positiveStart=46;r.negativeStart=28;r.sustaining=17;r.negativeSustaining=18;
  r.gain=1.5f;r.bias=4;r.negativeGain=.8f;r.quantum=.14f;r.medianFeedback=true;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;m.kickReleaseMs=20;
  r.position=87.4f;r.now=20;r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,r.position,true,r.now);
  CHECK(r.start(60)==Result::Ok);
  auto &t=r.c.autoTune;t.origin=94.72f;t.lower=34.72f;t.upper=154.72f;t.threshold=1;
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Validate;t.direction=-1;
  t.phasePosition=r.position+1.83f;t.noiseLow=t.noiseHigh=r.position;t.stoppingDistance=3.6f;t.peakVelocity=80;
  t.measurements[0]={46,16,17,11.17f,13.09f,.17f,.82f,615};
  t.measurements[1]={28,18,19,20.12f,23.3f,.37f,1.83f,247};
  for(unsigned n=0;n<100 && t.phase==AutoTunePhase::Coast;++n)r.step();
  CHECK(t.phase==AutoTunePhase::Validate);CHECK(t.proposedMotion.kickReleaseMs==20);
  CHECK(std::abs(t.validationTarget-r.position)>20);
  CHECK(t.validationTarget>t.lower && t.validationTarget<t.upper);
  CHECK(t.validationTarget<=129); // Leave room for the observed 80 deg/s startup, not just 23 deg/s work.
  for(unsigned n=0;n<6000 && t.active();++n)r.step();
  if(t.phase!=AutoTunePhase::Complete)std::cerr<<"validation kick "<<t.reason<<" error "<<t.validationError<<'\n';
  // The historical fitted response is not exact for this plant. A stricter
  // two-way validation may reject it; it must never publish an inaccurate fit.
  CHECK(t.phase==AutoTunePhase::Complete || (t.phase==AutoTunePhase::Failed && std::string(t.reason)=="validation_error"));
  if(t.phase==AutoTunePhase::Complete)CHECK(std::abs(t.validationError)<=2);
  else CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);
}
// Captured AZ cascade failure: 48% breakaway, 16..19% moving output and
// 9.58..17.67 deg/s measured plateaus. The old proposal stopped after kick,
// then failed stalled while its slow PI was still below breakaway output.
static void cascadeBreakawayRecovery() {
  Rig r(true);r.positiveStart=r.negativeStart=50;r.sustaining=16;
  r.gain=1.76f;r.bias=8.96f;r.negativeGain=.72727f;r.timeConstant=.15f;
  r.quantum=.14f;r.medianFeedback=true;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;m.kickReleaseMs=20;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=100;m.holdEnabled=true;
  m.velocity=30;m.acceleration=m.deceleration=10;m.jerk=8;
  r.c.speeds[0].speedPercent=20;r.c.speeds[0].approachSpeedPercent=18;r.c.speeds[0].coastDegrees=.5f;
  CHECK(r.c.setRange({-50,190},AZ_CUSTOM));r.position=35.28f;r.now=20;
  r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,r.position,true,r.now);
  const auto original=m;const auto speed=r.c.speeds[0];CHECK(r.start(80)==Result::Ok);
  auto &t=r.c.autoTune;t.origin=84.73f;t.lower=4.73f;t.upper=164.73f;t.threshold=1;
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Validate;t.direction=-1;
  t.phasePosition=r.position+.76f;t.noiseLow=t.noiseHigh=r.position;t.stoppingDistance=1.64f;t.peakVelocity=80;
  t.measurements[0]={48.08f,17,19,14.15f,17.67f,.12f,1.64f,631};
  t.measurements[1]={48,16,18,9.58f,12.14f,.02f,.76f,635};
  for(unsigned n=0;n<5000 && t.active();++n) {
    r.step();CHECK(r.c.axes[0].speedOutputPercent<=50.001f);
    CHECK(r.position>=t.lower && r.position<=t.upper);
    CHECK(!r.c.axes[0].holdArmed);
  }
  if(t.phase!=AutoTunePhase::Complete)std::cerr<<"cascade breakaway "<<t.reason<<'\n';
  CHECK(t.phase==AutoTunePhase::Complete);CHECK(std::abs(t.validationError)<=2);
  CHECK(std::abs(t.proposedMotion.velocity-14.15f)<.001f);
  CHECK(t.proposedMotion.velocity<=original.velocity);CHECK(t.proposedMotion.stallMs==original.stallMs);
  CHECK(m==original);CHECK(r.c.speeds[0]==speed);CHECK(!r.c.moving());
}
static void adaptiveMinimum(bool cascade=false,bool jam=false) {
  Rig r(cascade);r.positiveStart=r.negativeStart=50;r.sustaining=8;r.gain=.8f;r.bias=4;
  auto &m=r.c.motionSettings[0];m.kickMs=1500;m.kickReleaseMs=20;m.velocity=.1f;
  r.c.speeds[0].speedPercent=5;
  const auto original=m;const auto speed=r.c.speeds[0];CHECK(r.start(80)==Result::Ok);
  bool raised=false;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    if(jam && r.c.autoTune.phase==AutoTunePhase::Low)r.jam=true;
    r.step();raised|=r.c.autoTune.minimumLevel>5;
    if(!jam)CHECK(r.c.axes[0].motionError==MotionError::None);
    CHECK(r.c.axes[0].speedOutputPercent<=50.001f);
  }
  CHECK(raised);CHECK(!r.c.moving());CHECK(m==original);CHECK(r.c.speeds[0]==speed);
  if(jam) {
    CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);
  } else {
    if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"adaptive minimum "<<cascade<<" "<<r.c.autoTune.reason<<'\n';
    CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);
    CHECK(r.c.autoTune.proposedSpeed.speedPercent>=9);
    if(cascade)CHECK(r.c.autoTune.proposedMotion.velocity>original.velocity);
  }
}
static void validationRetry() {
  Rig r(true);r.positiveStart=r.negativeStart=50;r.sustaining=8;r.gain=.8f;r.bias=4;
  r.c.motionSettings[0].velocity=.001f;r.c.motionSettings[0].feedforward=0;
  r.c.motionSettings[0].velocityKp=r.c.motionSettings[0].velocityKi=0;
  r.c.motionSettings[0].stallMs=500;
  CHECK(r.start(80)==Result::Ok);
  auto &t=r.c.autoTune;t.phase=AutoTunePhase::Validate;t.validationTarget=110;
  t.measurements[0].lowOutput=t.measurements[1].lowOutput=8;
  t.measurements[0].highOutput=t.measurements[1].highOutput=9;
  t.proposedMotion=r.c.motionSettings[0];t.proposedMotion.mode=RegulatorMode::Cascade;
  r.c.motionSettings[0]=t.proposedMotion;
  t.internal=true;CHECK(r.c.target({110,0,true,false},false,r.now)==Result::Ok);t.internal=false;
  for(unsigned n=0;n<45 && t.phase==AutoTunePhase::Validate;++n)r.step();
  CHECK(t.phase==AutoTunePhase::Coast);CHECK(t.minimumLevel==10);
  CHECK(!r.c.axes[0].direction);CHECK(r.c.axes[0].motionError==MotionError::None);
  CHECK(t.startedAt==0);CHECK(t.measurements[0].lowVelocity==0);
  r.c.cancelAutoTune(r.now);CHECK(!r.c.moving());
}
static void exhaustedMinimum() {
  Rig r;r.c.motionSettings[0].stallMs=500;CHECK(r.start(80)==Result::Ok);
  auto &t=r.c.autoTune;t.phase=AutoTunePhase::Low;t.level=49;t.threshold=.2f;
  r.c.speeds[0].speedPercent=r.c.speeds[0].approachSpeedPercent=49;
  t.internal=true;CHECK(r.c.jog(0,1,false,r.now)==Result::Ok);t.internal=false;r.jam=true;
  for(unsigned n=0;n<100 && t.active();++n)r.step();
  CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="no_sustained_motion");
  CHECK(r.c.axes[0].motionError==MotionError::None);CHECK(!r.c.moving());
}
static void preparation() {
  for(unsigned axis:{0u,1u}) {
    Rig r(true,axis);r.c.motionSettings[axis].holdEnabled=true;
    const auto settings=r.c.motionSettings[axis];const auto speed=r.c.speeds[axis];
    CHECK(r.c.setRange({-50,190},AZ_CUSTOM));
    r.c.elRange={-45,190};
    r.c.feedbackAt(0,90,true,0);r.c.feedbackAt(1,90,true,0);
    Protocol p(r.c);Settings cfg;
    CHECK(api(r.c,p,cfg,"/api/motion/autotune",{{"action","center"},{"axis",axis?"el":"az"},{"rangeRevision",std::to_string(r.c.rangeRevision)}},0).status==200);
    CHECK(r.c.axes[axis].target==(axis?72.5f:70.f));CHECK(r.c.axes[axis].tracking);
    CHECK(!r.c.axes[axis].holdArmed);CHECK(!r.c.axes[1-axis].tracking);
    CHECK(r.c.motionSettings[axis]==settings);CHECK(r.c.speeds[axis]==speed);
    r.c.stopNow(-1,20);CHECK(!r.c.moving());
    CHECK(api(r.c,p,cfg,"/api/motion/autotune",{{"action","center"},{"axis",axis?"el":"az"},{"rangeRevision","0"}},20).status==409);
    CHECK(!r.c.axes[axis].tracking);
  }
  Rig r;CHECK(r.start()==Result::Ok);CHECK(r.c.centerForAutoTune(0,0)==Result::Busy);
  r.c.cancelAutoTune(0);r.c.mode=Mode::Auto;CHECK(r.c.centerForAutoTune(0,0)==Result::Busy);
  r.c.mode=Mode::Manual;r.c.axes[1].motionError=MotionError::Stalled;
  CHECK(r.c.centerForAutoTune(0,0)==Result::Unavailable);CHECK(!r.c.moving());
  r.c.resetMotion(-1,0);r.c.axes[0].calibrated=false;CHECK(r.c.centerForAutoTune(0,0)==Result::NotCalibrated);
  r.c.axes[0].calibrated=true;CHECK(r.c.centerForAutoTune(0,501)==Result::Unavailable);
  CHECK(r.c.centerForAutoTune(2,0)==Result::Unavailable);
  r.c.configurationLocked=true;CHECK(r.c.centerForAutoTune(0,0)==Result::Unavailable);
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
static void runtimeMethodology() {
  Rig r;Protocol p(r.c);Settings settings;
  const auto saved=r.c.motionSettings[0];const auto speed=r.c.speeds[0];
  Form start{{"action","start"},{"axis","az"},{"mode","simple"},{"travel","60"},{"outputLimit","50"},{"id","0"},
      {"tuneSearchStartPercent","15"},{"tuneSearchStepPercent","10"},
      {"tuneWorkingFraction","0.8"},{"tuneLambdaFactor","2"},{"tunePositionGainFactor","0.5"},
      {"tuneStartupObservationMs","700"},{"tunePlateauMinMs","240"},{"tunePlateauTimeoutMs","4000"}};
  // Invalid fields and combinations never consume an id, stop an axis, or mutate saved settings.
  for(const auto &bad:Form{{"tuneSearchStartPercent","51"},{"tuneSearchStepPercent","nan"},
      {"tuneWorkingFraction","1.1"},{"tuneLambdaFactor","0"},{"tunePositionGainFactor","3"},
      {"tuneStartupObservationMs","-1"},{"tunePlateauMinMs","240.5"},
      {"tunePlateauTimeoutMs","600"},{"tuneMisspelled","1"}}) {
    auto input=start;input[bad.first]=bad.second;
    CHECK(api(r.c,p,settings,"/api/motion/autotune",input,0).status==400);
    CHECK(!r.c.autoTune.active());CHECK(r.c.autoTune.id==0);
    CHECK(r.c.motionSettings[0]==saved);CHECK(r.c.speeds[0]==speed);
  }
  auto incompatible=start;incompatible["tunePlateauMinMs"]="1000";incompatible["tunePlateauTimeoutMs"]="1000";
  CHECK(api(r.c,p,settings,"/api/motion/autotune",incompatible,0).status==400);
  CHECK(api(r.c,p,settings,"/api/motion/autotune",start,0).status==200);
  CHECK(r.c.autoTune.options.searchStartPercent==15);CHECK(r.c.autoTune.options.workingFraction==.8f);
  CHECK(api(r.c,p,settings,"/api/motion/autotune",{{"action","cancel"},{"id","999"}},0).status!=200);
  CHECK(r.c.autoTune.active()); // A stale experiment cannot cancel a replacement.
  bool sawStart=false,sawSecond=false;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    r.step();
    if(r.c.autoTune.phase==AutoTunePhase::Start) {
      sawStart|=r.c.autoTune.searchLevel==15;sawSecond|=r.c.autoTune.searchLevel==25;
    }
    CHECK(r.c.axes[0].speedOutputPercent<=50.001f);
  }
  CHECK(sawStart);CHECK(sawSecond);
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);
  CHECK(r.c.autoTune.proposedSpeed.speedPercent>=40);
  CHECK(r.c.motionSettings[0]==saved);CHECK(r.c.speeds[0]==speed);
  char out[4096];JsonWriter j(out,sizeof out);CHECK(writeAutoTune(j,r.c,true));
  CHECK(std::string(j.data()).find("\"tuneSearchStartPercent\":15")!=std::string::npos);
  CHECK(std::string(j.data()).find("\"methodVersion\":2")!=std::string::npos);
  const auto previous=r.c.autoTune.id;
  r.position=90;r.velocity=0;r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,90,true,r.now);
  Form again{{"action","start"},{"axis","az"},{"mode","simple"},{"travel","60"},{"outputLimit","50"},{"id",std::to_string(previous)}};
  CHECK(api(r.c,p,settings,"/api/motion/autotune",again,r.now).status==200);
  CHECK(r.c.autoTune.options.searchStartPercent==5);CHECK(r.c.autoTune.options.lambdaFactor==1);
  CHECK(r.c.cancelAutoTune(r.now)==Result::Ok);
  // Method knobs can shorten probes, never bypass stale feedback or the trial-output cap.
  Rig stale;AutoTuneOptions options;options.searchStartPercent=40;options.plateauTimeoutMs=10000;
  CHECK(stale.c.startAutoTune(0,RegulatorMode::Simple,60,50,0,options)==Result::Ok);
  for(unsigned n=0;n<70 && stale.c.autoTune.active();++n)stale.step(false);
  CHECK(stale.c.autoTune.phase==AutoTunePhase::Failed);CHECK(!stale.c.moving());
  Rig cascade(true);options={};options.lambdaFactor=2;options.positionGainFactor=.5f;
  const float sliderSpeed=cascade.c.runtimeSpeedLimit(0)/20;
  CHECK(cascade.c.setRuntimeSpeed(0,sliderSpeed,0)==Result::Ok);
  CHECK(cascade.c.startAutoTune(0,RegulatorMode::Cascade,60,50,0,options)==Result::Ok);
  for(unsigned n=0;n<6000 && cascade.c.autoTune.active();++n)cascade.step();
  CHECK(cascade.c.autoTune.phase==AutoTunePhase::Complete);
  const auto &t=cascade.c.autoTune;const auto &a=t.measurements[0],&b=t.measurements[1];
  const float gain=std::max((a.highVelocity-a.lowVelocity)/(a.highOutput-a.lowOutput),
      (b.highVelocity-b.lowVelocity)/(b.highOutput-b.lowOutput));
  const float tau=std::clamp(std::max(a.responseSeconds,b.responseSeconds),.05f,5.f);
  const float delay=std::max(.02f,float(t.proposedMotion.velocityWindowMs)/2000);
  const float lambda=std::max(.5f,2*(tau+delay))*options.lambdaFactor;
  CHECK(std::abs(t.proposedMotion.velocityKp-tau/(gain*(lambda+delay)))<.0001f);
  CHECK(std::abs(t.proposedMotion.positionKp-options.positionGainFactor/(4*lambda))<.0001f);
  CHECK(cascade.c.runtimeSpeed(0)==sliderSpeed); // Autotune restores the manual selection.
  Rig longWindow(false,0,0xfffff000u);longWindow.gain=.08f;options={};
  options.searchStartPercent=35;options.plateauMinMs=1000;options.plateauTimeoutMs=6000;
  CHECK(longWindow.c.startAutoTune(0,RegulatorMode::Simple,60,50,longWindow.now,options)==Result::Ok);
  bool decimated=false;
  for(unsigned n=0;n<6000 && longWindow.c.autoTune.active();++n) {
    longWindow.step();const auto &t=longWindow.c.autoTune;
    if((t.phase==AutoTunePhase::Low || t.phase==AutoTunePhase::High) && t.sampleCount>=8) {
      const unsigned newest=(t.sampleNext+t.positions.size()-1)%t.positions.size();
      const unsigned before=(newest+t.positions.size()-1)%t.positions.size();
      decimated|=uint32_t(t.sampleTimes[newest]-t.sampleTimes[before])>=80;
    }
  }
  CHECK(decimated);CHECK(longWindow.c.autoTune.phase==AutoTunePhase::Complete);
}
static void savedKickIsNotMinimum() {
  for(bool cascade:{false,true}) {
    Rig r(cascade);r.positiveStart=12;r.c.motionSettings[0].kickEnabled=true;
    r.c.motionSettings[0].kickPercent=50;r.c.motionSettings[0].kickMs=1500;
    CHECK(r.start()==Result::Ok);
    for(unsigned n=0;n<1000 && r.c.autoTune.active() && r.c.autoTune.phase!=AutoTunePhase::Low;++n)r.step();
    CHECK(r.c.autoTune.phase==AutoTunePhase::Low);
    CHECK(r.c.autoTune.measurements[0].startOutput>=12);
    CHECK(r.c.autoTune.measurements[0].startOutput<35);
    r.c.stopNow(-1,r.now);CHECK(r.c.motionSettings[0].kickPercent==50);
  }
}
// A faster unmeasured request must not overwrite the last measured work point.
static void measuredWorkSurvivesShortStroke() {
  for(int direction:{-1,1})for(bool transferReserve:{false,true}) {
    Rig r(true);CHECK(r.start()==Result::Ok);auto &t=r.c.autoTune;
    const unsigned d=direction>0?0:1;t.direction=direction;t.level=40;t.searchLevel=30;t.threshold=1;
    t.measurements[d]={30,17,40,5,12,.2f,0,500};
    t.confirmedHigh[d]={25,12,.2f};
    if(transferReserve) {
      t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::High;t.nextDirection=direction;
      t.nextLevel=40;t.relocate=true;t.peakVelocity=300;t.phasePosition=r.position;
      for(unsigned n=0;n<40 && t.nextPhase==AutoTunePhase::High;++n)r.step();
    } else {
      r.position=direction>0?149:31;r.now=1000;r.c.feedbackAt(0,r.position,true,r.now);
      t.lastPosition=r.position;t.positionAt=r.now;
      r.c.axes[0].direction=r.c.axes[0].wanted=direction;
      r.c.axes[0].speedOutputPercent=40;t.phase=AutoTunePhase::High;t.relocations=1;
      r.step();
    }
    if(t.phase!=AutoTunePhase::Coast)std::cerr<<"short work "<<direction<<" reserve "<<transferReserve<<" phase "<<Controller::autoTunePhaseName(t.phase)<<" reason "<<t.reason<<" fault "<<r.c.fault<<"\n";
    CHECK(t.phase==AutoTunePhase::Coast);CHECK(!t.relocate);
    CHECK(t.nextPhase==(direction>0?AutoTunePhase::Start:AutoTunePhase::Validate));
    CHECK(t.measurements[d].highOutput==25);CHECK(t.measurements[d].highVelocity==12);
    CHECK(t.measurements[d].startOutput==30);CHECK(!r.c.moving());
    CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);r.c.cancelAutoTune(r.now);
  }
}
static void validationReturnStaysInsideReserve() {
  Rig r(true);CHECK(r.start()==Result::Ok);auto &t=r.c.autoTune;
  r.position=42;r.now=1000;r.c.feedbackAt(0,r.position,true,r.now);
  t.origin=94.72f;t.lower=34.72f;t.upper=154.72f;t.threshold=1;
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Validate;t.direction=-1;
  t.phasePosition=r.position+1.83f;t.noiseLow=t.noiseHigh=r.position;t.stoppingDistance=3.6f;t.peakVelocity=80;
  t.measurements[0]={46,16,17,11.17f,13.09f,.17f,.82f,615};
  t.measurements[1]={28,18,19,20.12f,23.3f,.37f,1.83f,247};
  for(unsigned n=0;n<100 && t.phase==AutoTunePhase::Coast;++n)r.step();
  CHECK(t.phase==AutoTunePhase::Validate);CHECK(t.validationOrigin>r.position);
  CHECK(t.validationTarget>t.validationOrigin);CHECK(t.validationTarget<t.upper);
  r.c.cancelAutoTune(r.now);CHECK(!r.c.moving());
}
// Captured EL: do not count Simple's proposed 17-degree coast again for Cascade.
static void cascadeUsesMeasuredStoppingReserve(float peak) {
  Rig r(true);CHECK(r.c.setRange({-45,190},AZ_CUSTOM));r.c.feedbackAt(0,r.position,true,r.now);
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=58.3f;m.kickMs=873;
  m.movementDegrees=1.22f;m.positionTolerance=5.02f;m.holdResumeDegrees=7.52f;
  m.outputLimitPercent=90;m.acceleration=m.deceleration=17.37f;m.jerk=8;
  m.velocityWindowMs=80;m.kickReleaseMs=20;m.velocity=29.4f;
  r.c.speeds[0].coastDegrees=2.5f;CHECK(r.start(90)==Result::Ok);auto &t=r.c.autoTune;
  r.position=.34f;r.now=1000;r.c.feedbackAt(0,r.position,true,r.now);
  t.origin=70.08f;t.lower=-19.92f;t.upper=160.08f;t.limit=90;t.threshold=1;
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Validate;t.direction=-1;
  t.phasePosition=r.position+24.4f;t.noiseLow=t.noiseHigh=r.position;
  t.lastPosition=r.position;t.positionAt=r.now;t.stoppingDistance=24.4f;t.peakVelocity=peak;
  t.measurements[0]={30,20,25,43.61f,71.29f,.64f,10.35f,747};
  t.measurements[1]={30,20,41,19.92f,81.65f,.02f,24.4f,675};
  for(unsigned n=0;n<100 && t.phase==AutoTunePhase::Coast;++n)r.step();
  if(peak==300) {
    CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="insufficient_travel");
    CHECK(!r.c.moving());CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);return;
  }
  if(t.phase!=AutoTunePhase::Validate)std::cerr<<"cascade reserve phase "<<Controller::autoTunePhaseName(t.phase)<<" reason "<<t.reason<<" peak "<<t.peakVelocity<<" stop "<<t.stoppingDistance<<" fault "<<r.c.fault<<" pos "<<r.position<<" origin "<<t.validationOrigin<<" target "<<t.validationTarget<<"\n";
  CHECK(t.phase==AutoTunePhase::Validate);
  CHECK(t.validationOrigin>t.lower+70);CHECK(t.validationTarget<t.upper-70);
  CHECK(t.validationTarget-t.validationOrigin>2*m.positionTolerance+m.movementDegrees);
  CHECK(t.proposedMotion.kickPercent==30);CHECK(t.proposedMotion.velocity>70);
  CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);r.c.cancelAutoTune(r.now);
}
static void returnMustWork() {
  Rig r(true);const auto original=r.c.motionSettings[0];const auto speed=r.c.speeds[0];
  CHECK(r.start()==Result::Ok);
  for(unsigned n=0;n<6000 && r.c.autoTune.active() && !r.c.autoTune.validationPass;++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Validate);
  CHECK(r.c.autoTune.validationPass==1);
  r.jam=true; // Successful outward approach must not conceal a failed return.
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(!r.c.moving());
  CHECK(r.c.motionSettings[0]==original);CHECK(r.c.speeds[0]==speed);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
}
int main(){try{startupConfirmation(0,1);startupConfirmation(1,1,0xfffff000u);startupConfirmation(0,5);startupConfirmation(1,5);startupStallDeadline(false);startupStallDeadline(true);runtimeMethodology();cascadeUsesMeasuredStoppingReserve(250);cascadeUsesMeasuredStoppingReserve(300);validationReturnStaysInsideReserve();measuredWorkSurvivesShortStroke();returnMustWork();savedKickIsNotMinimum();completed(false,0);completed(true,0);completed(false,1);completed(true,1,0xfffff000u);safety();highStiction();fastBounded(60);fastBounded(20);fastBounded(10);fastBounded(60,true,1);inertialPlateaus();inertialPlateaus(true);marginalWorkOutput();adaptiveProbeStartup();adaptiveProbeStartup(90);strongerRestart(false);strongerRestart(true);validationAfterStrongKick();cascadeBreakawayRecovery();asymmetricCoast();workOutputHeadroom();repositionDrag();repositionDrag(true);repositionStoppingRoom();adaptiveMinimum();adaptiveMinimum(true);adaptiveMinimum(false,true);validationRetry();exhaustedMinimum();preparation();transport();std::cout<<"PASS autotune: "<<checks<<" checks\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
