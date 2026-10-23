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
  float sensorNoise=0;
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
  unsigned periodMs=20;
  void step(bool fresh=true) {
    const auto &a=c.axes[axis];const float u=a.speedOutputPercent;
    const float start=lowerStart>0 && position<lowerStartBelow?lowerStart:a.direction<0?negativeStart:positiveStart;
    float minimum=a.direction<0 && negativeSustaining>0?negativeSustaining:sustaining;
    if(position>=dragFrom && position<=dragTo)minimum=std::max(minimum,dragOutput);
    float desired=a.direction*(u>=minimum?gain*(a.direction<0?negativeGain:1)*std::max(0.f,u-bias-load):0);
    if(u<rippleBelow)desired*=1+.45f*std::sin(float(now)*.009f);
    if(jam || (std::abs(velocity)<.01f && u+.001f<start+load))desired=0;
    const float dt=float(periodMs)/1000;
    velocity+=(desired-velocity)*(dt/timeConstant);
    if(std::abs(velocity)<.003f && desired==0)velocity=0;
    position+=velocity*dt;now+=periodMs;
    if(fresh){
      const float sensed=position+(sensorNoise? sensorNoise*std::sin(6.2831853f*5*float(now%1000)/1000):0);
      const float measured=medianFeedback?filter.sample(int(std::round(sensed/quantum)),now)*quantum:
          quantum>0?std::round(sensed/quantum)*quantum:sensed;
      c.feedbackAt(axis,measured,true,now);c.feedbackAt(1-axis,90,true,now);
    }
    c.tick(now);
  }
  Result start(float travel=60){return c.startAutoTune(axis,c.motionSettings[axis].mode,travel,50,now);}
};
static void fullCalibratedTravel(unsigned axis) {
  Rig r(true,axis);CHECK(axis==0?r.c.setRange({-50,190},AZ_CUSTOM):r.c.setElRange({-45,190},EL_CUSTOM));
  r.position=axis?72.5f:70.f;r.c.resetFeedback(axis,0);r.c.feedbackAt(axis,r.position,true,0);
  const auto original=r.c.motionSettings[axis];CHECK(r.start(110)==Result::Ok);
  CHECK(r.c.autoTune.lower==r.position-110);CHECK(r.c.autoTune.upper==r.position+110);
  CHECK(r.c.cancelAutoTune(r.now)==Result::Ok);CHECK(r.c.motionSettings[axis]==original);
  CHECK(r.start(axis?116:119)==Result::Invalid);CHECK(r.start(361)==Result::Invalid);CHECK(!r.c.moving());
}
static void noisyStopKeepsFeedbackWindow(unsigned axis,unsigned hz) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  r.gain=.7f;r.sensorNoise=.14f;
  auto &m=r.c.motionSettings[axis];m.velocityWindowMs=240;m.movementDegrees=1;
  m.positionTolerance=1;m.stoppedVelocity=1;
  const auto saved=m;const auto speed=r.c.speeds[axis];
  for(unsigned n=0;n<1000/r.periodMs;++n)r.step();
  AutoTuneOptions options;options.stages=TuneModel;
  CHECK(r.c.startAutoTune(axis,m.mode,60,50,r.now,options)==Result::Ok);
  for(unsigned n=0;n<130000/r.periodMs && r.c.autoTune.active();++n) {
    r.step();const auto &t=r.c.autoTune;
    CHECK(r.position>=t.lower && r.position<=t.upper);CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
  }
  const auto &t=r.c.autoTune;
  if(t.phase!=AutoTunePhase::Complete)std::cerr<<"noisy stop "<<hz<<" "<<t.reason<<" threshold "<<t.threshold<<" position "<<r.position<<" error "<<t.validationError<<'\n';
  CHECK(t.phase==AutoTunePhase::Complete);CHECK(t.validationPass==1);
  CHECK(t.shortNoiseVelocity>t.noiseVelocity);
  CHECK(t.proposedMotion.velocityWindowMs==saved.velocityWindowMs);
  CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());
}
// Exact measured plateaus/coast/settings from the failed device run. The
// historical peak made the former two-sided central band negative in width.
static void capturedTravelFailure(unsigned axis,int direction,unsigned hz,bool narrowBand=false) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  CHECK(axis==0?r.c.setRange({-50,190},AZ_CUSTOM):r.c.setElRange({-50,190},EL_CUSTOM));r.position=74.73f;r.c.feedbackAt(axis,r.position,true,r.now);
  auto &m=r.c.motionSettings[axis];m.velocityWindowMs=240;m.movementDegrees=3;
  m.positionTolerance=2;m.holdResumeDegrees=5.38f;m.stoppedVelocity=4.84f;m.velocity=81.93f;m.acceleration=m.deceleration=5;
  m.kickEnabled=true;m.kickPercent=45;m.kickMs=1200;m.kickReleaseMs=20;m.sustainingPercent=17;
  const auto saved=m;CHECK(r.start()==Result::Ok);auto &t=r.c.autoTune;
  r.position=narrowBand?74.73f-direction*.01f:direction>0?20:129.46f;r.now=1000;
  r.c.resetFeedback(axis,r.now);r.c.feedbackAt(axis,r.position,true,r.now);
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Validate;t.direction=-direction;
  t.phasePosition=r.position;t.lastPosition=r.position;t.positionAt=r.now;t.threshold=1;
  t.noiseLow=t.noiseHigh=r.position;t.peakVelocity=narrowBand?140.f:209.41f;t.stoppingDistance=9.16f;t.searchLevel=35;
  t.measurements[0]={35,17,29,13.45f,65.97f,.03f,9.16f,1001};
  t.measurements[1]={35,17,24,12.06f,45.22f,.08f,3.66f,303};
  bool prepared=false;
  for(unsigned n=0;n<45000/r.periodMs && t.active() && t.phase!=AutoTunePhase::Validate;++n) {
    r.step();prepared|=t.phase==AutoTunePhase::Reposition;
    CHECK(r.position>=t.lower && r.position<=t.upper);
  }
  CHECK(prepared==narrowBand);
  CHECK(t.phase==AutoTunePhase::Validate);CHECK(t.validationDirectional);CHECK(t.direction==direction);
  CHECK(t.direction*(t.validationTarget-t.validationOrigin)>=9);
  CHECK(t.validationTarget>t.lower && t.validationTarget<t.upper);
  CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);
  r.c.cancelAutoTune(r.now);CHECK(m==saved);CHECK(!r.c.moving());
}
// Replay the geometry of the reported 209.41 deg/s startup spike with a
// deterministic motor: overlapping destination reserves must not discard an
// otherwise usable 120-degree stroke before validation/tracking even starts.
static void overlappingReserves(unsigned axis,unsigned hz,unsigned interrupt=0) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  r.gain=.7f;r.timeConstant=.06f;auto &m=r.c.motionSettings[axis];
  m.velocityWindowMs=240;m.movementDegrees=1;m.positionTolerance=1;m.stoppedVelocity=1;
  const auto saved=m;const auto speed=r.c.speeds[axis];
  AutoTuneOptions options;options.stages=TuneModel|TuneSlow|TuneFast;
  CHECK(r.c.startAutoTune(axis,m.mode,60,50,r.now,options)==Result::Ok);
  auto &t=r.c.autoTune;bool injected=false,sawTransfer=false;unsigned directions=0,cases=0;
  for(unsigned n=0;n<481000/r.periodMs && t.active();++n) {
    if(!injected && t.phase==AutoTunePhase::Coast && t.nextPhase==AutoTunePhase::Validate &&
        t.measurements[1].highVelocity>0) {t.peakVelocity=209.41f;injected=true;}
    r.step();
    CHECK(r.position>=t.lower && r.position<=t.upper);
    CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
    if(t.phase==AutoTunePhase::Validate && t.validationDirectional) {
      directions|=t.direction>0?1:2;
      CHECK(t.direction*(t.validationTarget-t.validationOrigin)>=5-.001f);
      const float reserve=std::max({2.f,t.originalSpeed.coastDegrees,1.2f*t.stoppingDistance})+
          t.originalMotion.movementDegrees+std::max({t.peakVelocity,t.measurements[0].highVelocity,t.measurements[1].highVelocity})*
          (.15f+float(t.originalMotion.velocityWindowMs)/2000);
      CHECK(t.direction>0?t.validationTarget<=t.upper-reserve+.001f:t.validationTarget>=t.lower+reserve-.001f);
      CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);
    }
    if(injected && t.phase==AutoTunePhase::Reposition && t.nextPhase==AutoTunePhase::Validate) {
      sawTransfer=true;
      CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);
      if(interrupt==1) {r.c.stopNow(-1,r.now);break;}
      if(interrupt==2) {for(unsigned i=0;i<1100/r.periodMs && t.active();++i)r.step(false);break;}
    }
    if(t.phase==AutoTunePhase::Tracking)cases|=1u<<t.trackingCase;
  }
  if(!interrupt && t.phase!=AutoTunePhase::Complete)std::cerr<<"overlap "<<axis<<" "<<hz<<" reason "<<t.reason<<" phase "<<Controller::autoTunePhaseName(t.phase)<<" failure "<<Controller::autoTunePhaseName(t.failure.phase)<<" position "<<r.position<<" pass "<<t.validationPass<<" error "<<t.validationError<<" case "<<t.trackingCase<<'\n';
  if(!interrupt && t.phase!=AutoTunePhase::Complete)for(const auto &v:t.trackingResults)std::cerr<<"overlap candidate reason "<<v.reason<<" done "<<v.completed<<" peak "<<v.peakError<<" overshoot "<<v.overshoot<<" fast "<<t.trackingFast<<" slow "<<t.trackingSlow<<'\n';
  CHECK(injected);CHECK(sawTransfer);
  if(interrupt) {
    CHECK(t.phase==(interrupt==1?AutoTunePhase::Cancelled:AutoTunePhase::Failed));
    if(interrupt==2)CHECK(r.c.axes[axis].motionError==MotionError::FeedbackStale);
    CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);
  } else {
    CHECK(t.phase==AutoTunePhase::Complete);CHECK(t.proposalValidated);CHECK(t.trackingVerified);
    CHECK(t.validationPass==1);CHECK(directions==3);CHECK(cases==255);
  }
  CHECK(!r.c.moving());CHECK(r.c.axes[axis].speedOutputPercent==0);
  CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);
}
static void retryShortPreparation(unsigned axis,int direction,unsigned hz,unsigned mode=0,bool validation=false) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  CHECK(axis==0?r.c.setRange({-50,190},AZ_CUSTOM):r.c.setElRange({-50,190},EL_CUSTOM));
  r.position=68.47f;r.c.resetFeedback(axis,r.now);r.c.feedbackAt(axis,r.position,true,r.now);
  auto &m=r.c.motionSettings[axis];m.velocityWindowMs=240;m.movementDegrees=3;
  m.positionTolerance=2;m.holdResumeDegrees=5.38f;m.stoppedVelocity=4.84f;
  m.kickEnabled=true;m.kickPercent=45;m.kickMs=1200;m.kickReleaseMs=20;
  m.acceleration=m.deceleration=5;m.velocity=81.93f;m.jerk=20;m.stallMs=20000;
  const auto saved=m;const auto speed=r.c.speeds[axis];CHECK(r.start()==Result::Ok);
  auto &t=r.c.autoTune;t.threshold=1.92f;t.stoppingDistance=11.68f;t.peakVelocity=173.03f;t.searchLevel=35;
  t.measurements[0]={35,16,32,12.34f,73.64f,.02f,11.68f,1186};
  t.measurements[1]={35,16,27,10.83f,56.47f,.06f,6.72f,1169};
  t.proposedMotion=saved;t.proposedMotion.positionTolerance=3;t.proposedMotion.stoppedVelocity=t.threshold;
  t.proposedMotion.kickPercent=35;t.proposedMotion.sustainingPercent=16;
  t.proposedMotion.velocity=37.31f;t.proposedMotion.outputLimitPercent=50;
  t.trackingBase=t.proposedMotion;t.modelReady=t.proposalValidated=true;
  t.trackingFast=4.91f;t.trackingSlow=2.45f;t.trackingPeriodMs=200;t.trackingCase=4;
  t.trackingPlanned=true;t.trackingDurationMs.fill(6000);t.trackingWarmupMs.fill(1500);t.trackingReserve=63.7341f;
  t.trackingDirection=direction;t.direction=direction;
  t.trackingResults[0].completed=4;t.trackingResults[0].score=37.81f;
  const auto next=validation?AutoTunePhase::Validate:AutoTunePhase::Tracking;
  t.validationDirectional=true;t.validationDirection=direction;
  t.phase=AutoTunePhase::Reposition;t.nextPhase=next;t.nextDirection=direction;
  t.preparationAttempts=mode==2?7:1;
  const float position=validation?57.02f:36.56f;
  r.position=direction>0?position:t.lower+t.upper-position;r.now=mode==5?65000:1000;
  if(mode==5){t.preparationAt=1000;t.preparationRequired=29.46f;t.preparationAvailable=28.18f;}
  r.c.feedbackAt(axis,r.position,true,r.now);
  t.lastPosition=r.position;t.positionAt=r.now;t.phasePosition=r.position;t.phaseAt=r.now;t.startedAt=r.now;
  r.c.axes[axis].target=direction>0?25.49f:t.lower+t.upper-25.49f;
  r.gain=3;r.bias=14;r.sustaining=16;r.timeConstant=.1f;
  unsigned attempts=t.preparationAttempts;bool retry=false;
  for(unsigned n=0;n<40000/r.periodMs && t.active() && t.phase!=next;++n) {
    r.step();attempts=std::max(attempts,t.preparationAttempts);
    CHECK(r.position>=t.lower && r.position<=t.upper);CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
    CHECK(t.trackingCandidate==0);CHECK(t.trackingCase==4);CHECK(t.trackingResults[0].completed==4);
    if(t.phase==AutoTunePhase::Coast&&t.relocate&&t.nextPhase==next){
      retry=true;
      if(mode==1){r.c.stopNow(-1,r.now);break;}
      if(mode==3){for(unsigned i=0;i<1100/r.periodMs && t.active();++i)r.step(false);break;}
    }
  }
  if(!mode && t.phase!=next)std::cerr<<"retry preparation "<<axis<<" "<<direction<<" "<<hz<<" reason "<<t.reason<<" pos "<<r.position<<" attempts "<<attempts<<'\n';
  if(mode==5){
    CHECK(!retry);CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="reposition_timeout");
    char out[4096];JsonWriter j(out,sizeof out);CHECK(writeAutoTune(j,r.c,true));
    CHECK(std::string(j.data()).find("\"requiredTravel\":29.46")!=std::string::npos);
    CHECK(std::string(j.data()).find("\"preparationLimit\"")==std::string::npos);
  }
  else if(mode==1){CHECK(retry);CHECK(t.phase==AutoTunePhase::Cancelled);}
  else if(mode==3){CHECK(retry);CHECK(t.phase==AutoTunePhase::Failed);CHECK(r.c.axes[axis].motionError==MotionError::FeedbackStale);}
  else {
    CHECK(retry);CHECK(attempts>=(mode==2?8u:2u));
    CHECK(t.phase==next);CHECK(t.direction==direction);CHECK(t.preparationAttempts==0);
    CHECK(t.preparationRequired==0);CHECK(t.preparationAvailable==0);
    CHECK(t.trackingResults[0].score==37.81f);CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);
    r.c.cancelAutoTune(r.now);
  }
  CHECK(!r.c.moving());CHECK(r.c.motionSettings[axis]==saved);CHECK(r.c.speeds[axis]==speed);
}
// Captured device failure: the fourth relocation occurred during minimum,
// with a valid model and one already measured direction. This is preparation,
// not evidence against the candidate minimum or the identified regulator.
static void capturedMinimumRecovery(unsigned axis,int direction,unsigned hz,unsigned interrupt=0) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  r.position=73.36f;r.c.resetFeedback(axis,r.now);r.c.feedbackAt(axis,r.position,true,r.now);
  auto &m=r.c.motionSettings[axis];m.movementDegrees=3;m.positionTolerance=2;m.holdResumeDegrees=5.38f;m.velocityWindowMs=240;
  m.kickEnabled=true;m.kickPercent=45;m.kickMs=1200;m.kickReleaseMs=20;m.stallMs=20000;
  m.stoppedVelocity=4.84f;m.velocity=81.93f;m.acceleration=m.deceleration=5;m.jerk=20;
  const auto saved=m;const auto speed=r.c.speeds[axis];CHECK(r.start()==Result::Ok);auto &t=r.c.autoTune;
  t.threshold=1.77f;t.stoppingDistance=10.46f;t.peakVelocity=218.1f;t.searchLevel=35;t.relocations=3;
  t.measurements[0]={35,17,33,11.17f,74.19f,.02f,10.46f,1384};
  t.measurements[1]={35,17,23,16.91f,44.77f,.03f,4.27f,1225};
  t.modelReady=t.proposalValidated=true;t.proposedMotion=saved;t.proposedMotion.positionTolerance=3;
  t.proposedMotion.outputLimitPercent=50;t.proposedMotion.kickPercent=35;t.proposedMotion.sustainingPercent=17;
  t.minimumCandidate=t.minimumSetpoint=11.84f;t.minimumSpeed=14.58f;t.minimumOutput=17.06f;
  t.minimumPass=1;t.minimumDirectionSpeed=14.58f;t.minimumDirectionOutput=17.06f;
  r.position=direction<0?48.63f:t.lower+t.upper-48.63f;r.now=1000;
  r.c.feedbackAt(axis,r.position,true,r.now);t.lastPosition=r.position;t.positionAt=r.now;
  t.startedAt=r.now;t.phaseAt=r.now-1307;t.phasePosition=r.position-direction*34;t.direction=direction;t.phase=AutoTunePhase::Minimum;
  t.internal=true;CHECK(r.c.jog(axis,direction,false,r.now)==Result::Ok);t.internal=false;
  r.velocity=direction*80;r.c.axes[axis].direction=direction;r.c.axes[axis].speedOutputPercent=34.07f;
  r.gain=3;r.bias=13;r.sustaining=17;r.timeConstant=.1f;
  r.step();if(t.phase!=AutoTunePhase::Coast)std::cerr<<"minimum recovery "<<axis<<" "<<direction<<" "<<hz<<" phase "<<Controller::autoTunePhaseName(t.phase)<<" reason "<<t.reason<<" dir "<<r.c.axes[axis].direction<<" speed "<<r.c.axes[axis].measuredVelocity<<" sample "<<t.sampleVelocity<<" pos "<<r.position<<" reloc "<<t.relocations<<"\n";CHECK(t.phase==AutoTunePhase::Coast);CHECK(t.nextPhase==AutoTunePhase::Minimum);CHECK(t.relocate);
  CHECK(t.relocations==4);CHECK(t.active());CHECK(std::string(t.reason).empty());
  CHECK(t.minimumStartupTravel[direction>0?0:1]>34);
  bool returned=false;
  for(unsigned n=0;n<40000/r.periodMs && t.active() && t.phase!=AutoTunePhase::Minimum;++n){
    r.step();returned|=t.phase==AutoTunePhase::Reposition;
    CHECK(r.position>=t.lower && r.position<=t.upper);CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
    CHECK(t.minimumPass==1);CHECK(t.minimumCandidate==11.84f);CHECK(t.minimumSetpoint==11.84f);
    CHECK(t.minimumDirectionSpeed==14.58f);CHECK(t.minimumDirectionOutput==17.06f);
    if(returned && interrupt){
      if(interrupt==1)r.c.stopNow(-1,r.now);
      else for(unsigned k=0;k<1100/r.periodMs && t.active();++k)r.step(false);
      break;
    }
  }
  CHECK(returned);
  if(interrupt)CHECK(t.phase==(interrupt==1?AutoTunePhase::Cancelled:AutoTunePhase::Failed));
  else {CHECK(t.phase==AutoTunePhase::Minimum);CHECK(t.direction==direction);CHECK(t.startedAt==1000);r.c.cancelAutoTune(r.now);}
  CHECK(!r.c.moving());CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);
}
static void trackingRefitsLongTrajectory() {
  Rig r(true);auto &m=r.c.motionSettings[0];m.movementDegrees=.1f;m.positionTolerance=.5f;
  const auto saved=m;CHECK(r.start()==Result::Ok);auto &t=r.c.autoTune;
  t.phase=AutoTunePhase::Reposition;t.nextPhase=AutoTunePhase::Tracking;t.nextDirection=1;
  t.threshold=1;t.peakVelocity=200;t.stoppingDistance=1;t.searchLevel=35;
  t.proposedMotion=saved;t.proposedMotion.velocity=30;t.trackingBase=t.proposedMotion;
  t.trackingFast=20;t.trackingSlow=10;t.trackingPeriodMs=200;t.trackingDirection=1;t.trackingCase=4;
  t.trackingPlanned=true;t.trackingDurationMs.fill(6000);t.trackingWarmupMs.fill(1500);t.trackingReserve=60;
  t.trackingResults[0].completed=4;t.trackingResults[0].score=42;
  for(unsigned n=0;n<2000 && t.active() && t.phase!=AutoTunePhase::Tracking;++n)r.step();
  CHECK(t.phase==AutoTunePhase::Tracking);CHECK(t.trackingFast<20);CHECK(t.trackingSlow<10);
  CHECK(t.trackingCase==0);CHECK(t.trackingCandidate==0);
  for(const auto &result:t.trackingResults){CHECK(result.completed==0);CHECK(result.score==0);CHECK(!result.rejected);}
  r.c.cancelAutoTune(r.now);CHECK(!r.c.moving());CHECK(m==saved);
}
// Oracle feedback isolates the trial evaluator from the motor model: an
// ideal sparse stream passes its old waypoint while remaining on its intended
// trajectory. A final lag is a score, not a failed standalone arrival.
static void trackingReferenceAndCompletion(unsigned axis,int direction,unsigned hz,int mode=0) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  auto &m=r.c.motionSettings[axis];m.kickEnabled=false;m.velocity=20;m.stallMs=20000;
  const auto saved=m;const auto speed=r.c.speeds[axis];CHECK(r.start()==Result::Ok);
  auto &t=r.c.autoTune;r.now=1000;r.position=direction>0?40:140;
  r.c.feedbackAt(axis,r.position,true,r.now);t.phase=AutoTunePhase::Tracking;t.trackingAt=t.startedAt=r.now;
  t.lower=10;t.upper=170;t.phaseAt=r.now;t.phasePosition=r.position;t.lastPosition=r.position;t.positionAt=r.now;
  t.trackingBase=t.proposedMotion=saved;t.trackingCandidate=0;t.trackingCase=6;t.trackingPeriodMs=1500;
  t.trackingFast=t.trackingVelocity=10;t.trackingSlow=5;t.trackingDirection=t.direction=direction;
  t.trackingPlanned=true;t.trackingDurationMs.fill(6000);t.trackingWarmupMs.fill(3000);t.trackingReserve=20;
  t.trackingOrigin=t.trackingGoal=t.trackingStartMean=r.position;t.sampledAt=r.now;t.threshold=1;
  r.c.motionSettings[axis]=saved;r.c.motionSettings[axis].outputLimitPercent=50;t.internal=true;
  CHECK(r.c.target({r.position,r.position,axis==0,axis==1},false,r.now)==Result::Ok);t.internal=false;
  for(unsigned elapsed=r.periodMs;elapsed<=6040 && t.phase==AutoTunePhase::Tracking;elapsed+=r.periodMs){
    r.now=1000+elapsed;
    const float advance=mode==3?0:std::max(0.f,float(std::min(elapsed,6000u))/100- (mode==1?8.f:0.f));
    const float ahead=mode==2?15*std::min(1.f,float(elapsed)/1500):0;
    r.position=t.trackingOrigin+direction*(advance+ahead);
    r.c.feedbackAt(axis,r.position,true,r.now);r.c.feedbackAt(1-axis,90,true,r.now);r.c.tick(r.now);
    CHECK(r.position>=t.lower && r.position<=t.upper);CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
  }
  if(t.phase!=AutoTunePhase::Coast)std::cerr<<"oracle "<<axis<<" "<<direction<<" "<<hz<<" mode "<<mode<<" phase "<<Controller::autoTunePhaseName(t.phase)<<" reason "<<t.reason<<" case "<<t.trackingCase<<" pos "<<r.position<<" over "<<t.trackingOvershoot<<"\n";
  CHECK(t.phase==AutoTunePhase::Coast);const auto &result=t.trackingResults[0];
  if(mode>=2){CHECK(result.rejected);CHECK(result.failedCase==6);CHECK(std::string(result.reason)==(mode==2?"overshoot":"no_progress"));}
  else {CHECK(!result.rejected);CHECK(result.completed==1);CHECK(t.trackingSamples>=8);CHECK(t.trackingOvershoot<.01f);
    CHECK(mode==0?result.meanError<.01f:result.meanError>0);CHECK(r.now>=7000 && r.now<=7040);}
  r.c.cancelAutoTune(r.now);CHECK(!r.c.moving());CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);
}
static void capturedTrackingPlan(unsigned axis,unsigned hz,bool guardRecovery=false) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  CHECK(axis==0?r.c.setRange({-50,190},AZ_CUSTOM):r.c.setElRange({-50,190},EL_CUSTOM));
  r.position=74.5f;r.c.resetFeedback(axis,0);r.c.feedbackAt(axis,r.position,true,0);
  auto &m=r.c.motionSettings[axis];m.velocity=81.93f;m.acceleration=m.deceleration=5;m.jerk=20;
  m.positionTolerance=2;m.holdResumeDegrees=5.38f;m.movementDegrees=3;m.velocityWindowMs=240;
  const auto saved=m;CHECK(r.start(90)==Result::Ok);auto &t=r.c.autoTune;
  t.trackingBase=t.proposedMotion=saved;t.proposedMotion.velocity=84.39f;
  t.proposedMotion.positionTolerance=3;t.proposedMotion.kickEnabled=true;t.proposedMotion.kickMs=2325;
  t.minimumSetpoint=19.58f;t.minimumSpeed=27.19f;t.peakVelocity=233.92f;t.stoppingDistance=13.79f;
  t.measurements[0]={35,20,36,26.99f,95.09f,.15f,13.79f,1417};
  t.measurements[1]={35,20,33,27.97f,84.39f,.19f,12.37f,1271};
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Tracking;t.threshold=2.87f;
  for(unsigned n=0;n<1000/r.periodMs && !t.trackingPlanned && t.active();++n)r.step();
  CHECK(t.trackingPlanned);CHECK(t.active());CHECK(t.trackingFast>9.72f);CHECK(t.trackingFast>t.trackingSlow);
  const float coast=std::max(2.f,1.2f*13.79f+3);
  for(unsigned n=0;n<4;++n){const unsigned period=n%2?1500:200;const float v=n<2?t.trackingSlow:t.trackingFast;
    CHECK(t.trackingDurationMs[n]%period==0);CHECK(t.trackingWarmupMs[n]%period==0);
    CHECK(t.trackingWarmupMs[n]>=period+1417+240);
    CHECK(t.trackingDurationMs[n]-t.trackingWarmupMs[n]>=2*period);
    CHECK(v*(t.trackingDurationMs[n]-t.trackingWarmupMs[n])/1000>=2*13.79f);
    CHECK(v*t.trackingDurationMs[n]/1000<=.85f*(t.upper-t.lower-coast-t.trackingReserve)+.01f);
  }
  CHECK(t.trackingDurationMs[1]>6000);CHECK(t.trackingDurationMs[3]>6000);
  uint32_t seriesMs=0;for(const auto duration:t.trackingDurationMs)seriesMs+=6*duration;
  CHECK(t.trackingTimeoutMs>=120000+seriesMs+30000*t.options.trackingCases()+60000);
  CHECK(t.trackingTimeoutMs<=1200000);
  if(guardRecovery){
    const float oldFast=t.trackingFast,oldReserve=t.trackingReserve;
    t.trackingResults[0].completed=2;t.trackingResults[0].score=42;
    r.now=1000;r.position=123;r.c.feedbackAt(axis,r.position,true,r.now);
    t.lastPosition=r.position;t.positionAt=r.now;t.tickAt=r.now-20;t.ticked=true;
    t.internal=true;CHECK(r.c.target({160,160,axis==0,axis==1},false,r.now)==Result::Ok);t.internal=false;
    r.c.axes[axis].direction=1;r.c.axes[axis].speedOutputPercent=20;
    r.now+=r.periodMs;r.position+=200.f*r.periodMs/1000;
    r.c.feedbackAt(axis,r.position,true,r.now);r.c.feedbackAt(1-axis,90,true,r.now);r.c.tick(r.now);
    CHECK(t.phase==AutoTunePhase::Coast);CHECK(t.active());CHECK(t.trackingReserve>oldReserve);
    CHECK(t.trackingFast<oldFast);CHECK(t.trackingCase==0);CHECK(t.trackingCandidate==0);
    CHECK(!r.c.moving());CHECK(r.c.axes[axis].speedOutputPercent==0);
    for(const auto &v:t.trackingResults){CHECK(v.completed==0);CHECK(v.score==0);CHECK(!v.rejected);}
  }
  r.c.cancelAutoTune(r.now);CHECK(m==saved);CHECK(!r.c.moving());
}
// First-order motor fit to the captured plateaus, breakaway and coast. This
// complements the evaluator oracle with ordinary controller/motor dynamics.
static void measuredPlantTracking(unsigned axis,unsigned hz) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  CHECK(axis==0?r.c.setRange({-50,190},AZ_CUSTOM):r.c.setElRange({-50,190},EL_CUSTOM));
  r.position=74.5f;r.c.resetFeedback(axis,0);r.c.feedbackAt(axis,r.position,true,0);
  r.positiveStart=r.negativeStart=35;r.sustaining=20;r.gain=4.25f;r.bias=13.65f;
  r.negativeGain=4.34f/4.25f;r.timeConstant=.17f;r.quantum=.14f;r.medianFeedback=true;
  auto &m=r.c.motionSettings[axis];m.velocity=81.93f;m.acceleration=m.deceleration=5;m.jerk=20;
  m.positionTolerance=2;m.holdResumeDegrees=5.38f;m.movementDegrees=3;m.velocityWindowMs=240;
  m.kickEnabled=true;m.kickPercent=45;m.kickMs=1200;m.kickReleaseMs=20;m.stallMs=20000;
  m.positionKp=.5f;m.velocityKp=.08f;m.velocityKi=1.5f;m.feedforward=.23f;m.sustainingPercent=17;m.stoppedVelocity=4.84f;
  const auto saved=m;CHECK(r.start(90)==Result::Ok);auto &t=r.c.autoTune;
  t.proposedMotion=saved;auto &p=t.proposedMotion;p.velocity=84.39f;p.positionTolerance=3;
  p.positionKp=1/(4*.62f);p.velocityKp=.19f/(4.34f*(.62f+.12f));p.velocityKi=p.velocityKp/.19f;
  p.positionKi=p.positionKd=p.velocityKd=0;p.sustainingPercent=20;p.feedforward=33.49f/84.39f;
  p.kickPercent=35;p.kickMs=2325;p.outputLimitPercent=50;p.stoppedVelocity=2.87f;
  t.minimumSetpoint=19.58f;t.minimumSpeed=27.19f;t.peakVelocity=233.92f;t.stoppingDistance=13.79f;
  t.modelReady=t.proposalValidated=t.minimumCompletePending=true;
  t.measurements[0]={35,20,36,26.99f,95.09f,.15f,13.79f,1417};
  t.measurements[1]={35,20,33,27.97f,84.39f,.19f,12.37f,1271};
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Tracking;t.threshold=2.87f;
  for(unsigned n=0;n<480000/r.periodMs && t.active() && !t.trackingVerified;++n){r.step();
    CHECK(r.position>=t.lower && r.position<=t.upper);CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);}
  if(!t.trackingVerified){std::cerr<<"measured plant "<<axis<<" "<<hz<<" reason "<<t.reason<<" time "<<r.now<<'\n';
    for(const auto &v:t.trackingResults)std::cerr<<" candidate "<<v.completed<<" "<<v.reason<<" case "<<v.failedCase<<" over "<<v.overshoot<<" peak "<<v.peakError<<'\n';}
  CHECK(t.trackingVerified);CHECK(t.trackingResults[t.trackingBest].completed==8);
  CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);r.c.cancelAutoTune(r.now);
  CHECK(!r.c.moving());CHECK(m==saved);
}
static void partialHighSpeedTrackingPlan(unsigned axis,unsigned hz,unsigned stages) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  CHECK(axis==0?r.c.setRange({-50,190},AZ_CUSTOM):r.c.setElRange({-50,190},EL_CUSTOM));
  r.position=74.5f;r.c.resetFeedback(axis,0);r.c.feedbackAt(axis,r.position,true,0);
  auto &m=r.c.motionSettings[axis];m.velocity=81.93f;m.acceleration=m.deceleration=5;m.jerk=20;
  m.positionTolerance=2;m.holdResumeDegrees=5.38f;m.movementDegrees=3;m.velocityWindowMs=240;
  m.kickEnabled=true;m.kickPercent=45;m.kickMs=1200;m.sustainingPercent=17;
  const auto saved=m;AutoTuneOptions options;options.stages=stages;
  CHECK(r.c.startAutoTune(axis,m.mode,90,50,0,options)==Result::Ok);auto &t=r.c.autoTune;
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Tracking;t.threshold=4.84f;
  for(unsigned n=0;n<1000/r.periodMs && !t.trackingPlanned && t.active();++n)r.step();
  CHECK(t.trackingPlanned);CHECK(t.active());CHECK(t.minimumSetpoint==0);CHECK(t.trackingFast<81.93f);
  for(unsigned n=0;n<4;++n){const bool selected=options.has(n<2?TuneSlow:TuneFast);
    if(selected){CHECK(t.trackingDurationMs[n]>t.trackingWarmupMs[n]);CHECK(t.trackingDurationMs[n]<=30000);}
    else {CHECK(t.trackingDurationMs[n]==0);CHECK(t.trackingWarmupMs[n]==0);}}
  r.c.cancelAutoTune(r.now);CHECK(!r.c.moving());CHECK(m==saved);
}
static void stationaryOtherAxisTolerance(unsigned axis,unsigned hz,float ownMovement,float otherMovement,bool powered=false) {
  Rig r(true,axis);r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  r.c.motionSettings[axis].movementDegrees=ownMovement;
  r.c.motionSettings[1-axis].movementDegrees=otherMovement;
  const auto saved=r.c.motionSettings[axis];const auto speed=r.c.speeds[axis];CHECK(r.start()==Result::Ok);
  const float tolerance=std::max(5.f,otherMovement);
  // Both signs, sustained offsets and the inclusive boundary must remain
  // below the independent-axis interlock, even with a smaller tuned threshold.
  auto sample=[&](float offset) {
    r.now+=r.periodMs;r.c.feedbackAt(axis,r.position,true,r.now);
    r.c.feedbackAt(1-axis,90+offset,true,r.now);r.c.tick(r.now);
    CHECK(r.c.autoTune.active());CHECK(r.c.axes[1-axis].direction==0);
    CHECK(r.c.axes[1-axis].calibrated);
  };
  float previous=0;
  for(float offset:{2.1f,-2.1f,tolerance-.1f,-tolerance+.1f,tolerance,-tolerance}) {
    // Ramp between offsets so the separate ADC plausibility filter accepts
    // every conversion; this must test the autotune guard itself.
    while(std::abs(offset-previous)>.001f) {
      previous+=std::clamp(offset-previous,-1.f,1.f);sample(previous);
    }
    for(unsigned n=0;n<60/r.periodMs;++n)sample(offset);
  }
  r.now+=20;r.c.feedbackAt(axis,r.position,true,r.now);
  r.c.feedbackAt(1-axis,powered?90:90-tolerance-.1f,true,r.now);
  if(powered) {
    r.c.autoTune.internal=true;CHECK(r.c.jog(1-axis,1,false,r.now)==Result::Ok);r.c.autoTune.internal=false;
  }
  r.c.tick(r.now);
  if(r.c.autoTune.phase!=AutoTunePhase::Failed)std::cerr<<"other axis guard axis "<<axis<<" hz "<<hz<<" own "<<ownMovement<<" other "<<otherMovement<<" powered "<<powered<<" direction "<<r.c.axes[1-axis].direction<<" wanted "<<r.c.axes[1-axis].wanted<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);
  CHECK(std::string(r.c.autoTune.reason)=="other_axis_moved");
  CHECK(!r.c.moving());CHECK(r.c.axes[1-axis].direction==0);
  CHECK(r.c.motionSettings[axis]==saved);CHECK(r.c.speeds[axis]==speed);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
}
static void coastDeadlineAndFailureSnapshot(bool longSettle) {
  Rig r;auto &m=r.c.motionSettings[0];if(longSettle)m.settleMs=5000;
  const auto saved=m;const auto speed=r.c.speeds[0];CHECK(r.start()==Result::Ok);
  auto &t=r.c.autoTune;t.phase=AutoTunePhase::Coast;t.threshold=.2f;
  t.nextPhase=AutoTunePhase::Start;t.nextDirection=1;t.nextLevel=5;
  if(!longSettle){r.velocity=1;r.timeConstant=50;}
  for(unsigned n=0;n<600 && t.phase==AutoTunePhase::Coast;++n)r.step();
  if(longSettle) {
    CHECK(t.phase==AutoTunePhase::Start);CHECK(r.now>=5000);r.c.cancelAutoTune(r.now);
  } else {
    CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="coast_timeout");
    CHECK(t.failure.phase==AutoTunePhase::Coast);CHECK(t.failure.elapsedMs>5000);
    CHECK(t.failure.output==0);CHECK(t.failure.velocity>t.failure.threshold);
    const float captured=t.failure.position;
    r.c.feedbackAt(0,100,true,r.now+20);CHECK(t.failure.position==captured);
    char out[4096];JsonWriter j(out,sizeof out);CHECK(writeAutoTune(j,r.c,true));
    CHECK(std::string(j.data()).find("\"failure\":{\"phase\":\"coast\"")!=std::string::npos);
    t.failure={};JsonWriter absent(out,sizeof out);CHECK(writeAutoTune(absent,r.c,true));
    CHECK(std::string(absent.data()).find("\"failure\"")==std::string::npos);
  }
  CHECK(m==saved);CHECK(r.c.speeds[0]==speed);CHECK(!r.c.moving());
}
static void targetDeadlineIdentifiesPhase(AutoTunePhase phase) {
  Rig r;const auto saved=r.c.motionSettings[0];CHECK(r.start()==Result::Ok);
  r.now=70000;r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,90,true,r.now);
  auto &t=r.c.autoTune;t.startedAt=r.now;t.phase=phase;t.phaseAt=r.now-60001;t.threshold=.2f;
  r.c.axes[0].target=100;r.step();
  CHECK(t.phase==AutoTunePhase::Failed);CHECK(t.failure.phase==phase);
  CHECK(std::string(t.reason)==(phase==AutoTunePhase::Validate?"validation_timeout":"reposition_timeout"));
  CHECK(t.failure.target==100);CHECK(t.failure.elapsedMs>60000);CHECK(t.failure.position==90);
  CHECK(r.c.motionSettings[0]==saved);CHECK(!r.c.moving());
}
static void completed(bool cascade,unsigned axis,uint32_t stamp=0) {
  Rig r(cascade,axis,stamp);const auto original=r.c.motionSettings[axis];const auto speed=r.c.speeds[axis];
  CHECK(r.start()==Result::Ok);
  CHECK(r.c.target({100,100,true,true},true,r.now)==Result::Busy);
  CHECK(r.c.manualDrive(1,1,1,1,1,1,0,false,r.now)==Result::Busy);
  CHECK(r.c.jog(1-axis,1,false,r.now)==Result::Busy);
  bool sawMinimum=false,sawReconfirmation=false;
  std::array<unsigned,3> cases{};
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n){
    r.step();const auto &t=r.c.autoTune;
    sawMinimum|=t.phase==AutoTunePhase::Minimum;
    sawReconfirmation|=t.phase==AutoTunePhase::Minimum && t.trackingVerified;
    if(t.phase==AutoTunePhase::Tracking) {
      CHECK(cascade);CHECK(t.trackingCandidate<3);CHECK(t.trackingCase<8);
      cases[t.trackingCandidate]|=1u<<t.trackingCase;
      CHECK(t.trackingPeriodMs==((t.trackingCase/2)%2?1500u:200u));
      CHECK(t.trackingVelocity==(t.trackingCase<4?t.trackingSlow:t.trackingFast));
      CHECK(t.trackingSlow>0);CHECK(t.trackingFast>t.trackingSlow);
      CHECK(r.c.motionSettings[axis].positionTolerance==t.trackingBase.positionTolerance);
      CHECK(r.c.motionSettings[axis].kickPercent==t.trackingBase.kickPercent);
      CHECK(r.c.motionSettings[axis].acceleration<=original.acceleration);
      CHECK(r.c.motionSettings[axis].jerk<=original.jerk);
    }
    CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);CHECK(!r.c.axes[1-axis].direction);
  }
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"tune "<<cascade<<" axis "<<axis<<" phase "<<Controller::autoTunePhaseName(r.c.autoTune.phase)<<" reason "<<r.c.autoTune.reason<<" motion "<<r.c.fault<<" pos "<<r.position<<" low0 "<<r.c.autoTune.measurements[0].lowVelocity<<" high0 "<<r.c.autoTune.measurements[0].highVelocity<<" low1 "<<r.c.autoTune.measurements[1].lowVelocity<<" high1 "<<r.c.autoTune.measurements[1].highVelocity<<" error "<<r.c.autoTune.validationError<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.autoTune.proposedMotion.valid());
  CHECK(sawMinimum);CHECK(r.c.autoTune.minimumConfirm);CHECK(r.c.autoTune.minimumSpeed>0);
  if(cascade) {
    const auto &t=r.c.autoTune;const auto &best=t.trackingResults[t.trackingBest];
    CHECK(t.proposedMotion.velocityWindowMs==std::min(80u,original.velocityWindowMs));
    CHECK(sawReconfirmation);CHECK(cases[t.trackingBest]==255);CHECK(!best.rejected);CHECK(best.completed==8);
    for(const auto &candidate:t.trackingResults)if(!candidate.rejected) {
      CHECK(best.score<=candidate.score/.95f+.001f);
      if(t.trackingBest!=0)CHECK(best.score<=candidate.score+.001f);
    }
    const float p=t.trackingBest==0?1.f:t.trackingBest==1?.6f:1.4f;
    CHECK(std::abs(t.proposedMotion.positionKp-t.trackingBase.positionKp*p)<.0001f);
    CHECK(t.proposedMotion.velocityKd==0);CHECK(t.proposedMotion.positionKi==0);CHECK(t.proposedMotion.positionKd==0);
    if(!t.trackingResults[0].rejected && t.trackingBest!=0)CHECK(best.score<t.trackingResults[0].score*.95f);
    std::cout<<"tracking axis "<<axis<<" winner "<<t.trackingBest<<" score "<<best.score
      <<" baseline rejected "<<t.trackingResults[0].rejected<<" mean "<<best.meanError<<" overshoot "<<best.overshoot<<'\n';
  } else {CHECK(!sawReconfirmation);CHECK(!r.c.autoTune.trackingVerified);}
  CHECK(r.c.autoTune.validationPass==1);
  CHECK(r.c.motionSettings[axis]==original);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());CHECK(!r.c.axes[axis].holdArmed);
  CHECK(r.c.autoTune.measurements[0].startOutput>20);CHECK(r.c.autoTune.measurements[1].startOutput>20);
  CHECK(r.c.autoTune.proposedMotion.kickPercent<=50);CHECK(!r.c.autoTune.proposedMotion.holdEnabled);
  if(cascade){CHECK(r.c.autoTune.trackingVerified);CHECK(r.c.autoTune.trackingResults[r.c.autoTune.trackingBest].completed==8);CHECK(r.c.autoTune.proposedSpeed.speedPercent>=25);CHECK(r.c.autoTune.proposedMotion.velocityKp>0);CHECK(r.c.autoTune.proposedMotion.velocityKi>0);CHECK(r.c.autoTune.proposedMotion.positionKp>0);}
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
    for(unsigned n=0;n<24050 && probe.c.autoTune.active() && probe.c.autoTune.phase!=AutoTunePhase::Minimum;++n)probe.step();
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
    for(unsigned n=0;n<24050 && probe.c.autoTune.active() &&
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
  for(unsigned n=0;n<24050&&r.c.autoTune.active();++n)r.step();
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
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
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
static void stationarySearchRaisesOutput(unsigned axis,unsigned hz,bool jam=false) {
  Rig r(false,axis,0xfffff000u);r.periodMs=1000/hz;
  CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  r.positiveStart=r.negativeStart=16;r.jam=jam;
  auto &m=r.c.motionSettings[axis];m.stallMs=500;
  const auto saved=m;const auto speed=r.c.speeds[axis];
  AutoTuneOptions options;options.stages=TuneKick;options.searchStartPercent=1;
  options.startupObservationMs=2000;
  CHECK(r.c.startAutoTune(axis,m.mode,60,50,r.now,options)==Result::Ok);
  bool raised=false;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    r.step();const auto &t=r.c.autoTune;
    raised|=t.searchLevel>1;
    CHECK(r.c.axes[axis].motionError==MotionError::None);
    CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
    CHECK(r.position>=t.lower && r.position<=t.upper);
    if(!jam && t.measurements[0].startOutput>0 && t.measurements[1].startOutput>0)break;
  }
  const auto &t=r.c.autoTune;CHECK(raised);
  if(jam) {CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="no_start");}
  else {
    CHECK(t.active());CHECK(t.measurements[0].startOutput>=16);CHECK(t.measurements[1].startOutput>=16);
    r.c.cancelAutoTune(r.now);
  }
  CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());
}
static void minimumKickFailureRetries(unsigned axis) {
  Rig r(false,axis);auto &m=r.c.motionSettings[axis];
  m.kickEnabled=true;m.kickPercent=5;m.kickMs=100;m.stallMs=500;m.outputLimitPercent=20;
  const auto saved=m;const auto speed=r.c.speeds[axis];
  CHECK(r.c.setRuntimeSpeed(axis,1,r.now)==Result::Ok);
  AutoTuneOptions options;options.stages=TuneMinimum;
  CHECK(r.c.startAutoTune(axis,m.mode,60,20,r.now,options)==Result::Ok);
  for(unsigned n=0;n<300 && r.c.autoTune.active() && r.c.autoTune.minimumCandidate<=1;++n)r.step();
  const auto &t=r.c.autoTune;
  CHECK(t.active());CHECK(t.phase==AutoTunePhase::Coast);CHECK(t.nextPhase==AutoTunePhase::Minimum);
  CHECK(t.minimumCandidate==2);CHECK(r.position==90);CHECK(r.c.axes[axis].motionError==MotionError::None);
  r.c.cancelAutoTune(r.now);CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());
}
static void minimumCanRaiseSelectedKick() {
  Rig r;r.positiveStart=r.negativeStart=16;
  auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=5;m.kickMs=100;m.stallMs=1000;
  const auto saved=m;const auto speed=r.c.speeds[0];
  CHECK(r.c.setRuntimeSpeed(0,3,r.now)==Result::Ok);
  AutoTuneOptions options;options.stages=TuneKick|TuneMinimum;
  CHECK(r.c.startAutoTune(0,m.mode,60,50,r.now,options)==Result::Ok);
  bool sticky=false,revalidated=false;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    r.step();const auto &t=r.c.autoTune;
    if(!sticky && t.phase==AutoTunePhase::Minimum) {
      CHECK(t.proposedMotion.kickPercent<35);r.positiveStart=r.negativeStart=35;sticky=true;
    }
    revalidated|=sticky && t.phase==AutoTunePhase::Validate && t.proposedMotion.kickPercent>=35;
    CHECK(r.c.axes[0].motionError==MotionError::None);CHECK(r.c.axes[0].speedOutputPercent<=50.001f);
  }
  const auto &t=r.c.autoTune;
  if(t.phase!=AutoTunePhase::Complete)std::cerr<<"minimum selected kick "<<t.reason<<" candidate "<<t.minimumCandidate<<" kick "<<t.proposedMotion.kickPercent<<'\n';
  CHECK(sticky);CHECK(revalidated);CHECK(t.phase==AutoTunePhase::Complete);
  CHECK(t.proposedMotion.kickPercent>=35);CHECK(t.minimumConfirm);CHECK(t.minimumSpeed>0);
  CHECK(m==saved);CHECK(r.c.speeds[0]==speed);CHECK(!r.c.moving());
}
static void stationaryTrackingTriesOtherGains(bool kick) {
  Rig r(true);auto &m=r.c.motionSettings[0];m.kickEnabled=kick;m.kickMs=100;m.stallMs=500;
  m.feedforward=20;r.jam=true;
  const auto saved=m;const auto speed=r.c.speeds[0];
  AutoTuneOptions options;options.stages=TuneSlow;
  CHECK(r.c.startAutoTune(0,m.mode,60,50,r.now,options)==Result::Ok);
  bool advanced=false;
  for(unsigned n=0;n<3000 && r.c.autoTune.active();++n) {
    r.step();advanced|=r.c.autoTune.trackingCandidate>0;
    CHECK(r.c.axes[0].motionError==MotionError::None);CHECK(r.c.fault.empty());
  }
  const auto &t=r.c.autoTune;CHECK(advanced);CHECK(t.phase==AutoTunePhase::Failed);
  CHECK(std::string(t.reason)=="tracking_unstable");
  for(const auto &candidate:t.trackingResults){CHECK(candidate.rejected);CHECK(std::string(candidate.reason)=="no_progress");}
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
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
    r.step();relocated|=r.c.autoTune.phase==AutoTunePhase::Reposition;
    CHECK(r.position>=90-travel-.2f && r.position<=90+travel+.2f);
    CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
  }
  if(r.c.autoTune.phase!=AutoTunePhase::Complete && travel!=10)std::cerr<<"fast "<<travel<<" cascade "<<cascade<<" reason "<<r.c.autoTune.reason<<" phase "<<Controller::autoTunePhaseName(r.c.autoTune.phase)<<" position "<<r.position<<" level "<<r.c.autoTune.level<<" low0 "<<r.c.autoTune.measurements[0].lowVelocity<<" high0 "<<r.c.autoTune.measurements[0].highVelocity<<" low1 "<<r.c.autoTune.measurements[1].lowVelocity<<" high1 "<<r.c.autoTune.measurements[1].highVelocity<<" error "<<r.c.autoTune.validationError<<'\n';
  if(travel<=20) {
    CHECK(r.c.autoTune.phase==AutoTunePhase::Complete ||
          (r.c.autoTune.phase==AutoTunePhase::Failed && (std::string(r.c.autoTune.reason)=="stopping_distance" || std::string(r.c.autoTune.reason)=="timeout")));
    // A fresh breakaway search can discover a larger stopping reserve before
    // a transfer starts. Reject that short stroke rather than forcing a move.
    if(travel==20)CHECK(relocated || (r.c.autoTune.relocations>0 &&
        r.c.autoTune.phase==AutoTunePhase::Failed && (std::string(r.c.autoTune.reason)=="stopping_distance" || std::string(r.c.autoTune.reason)=="timeout")));
    CHECK(r.c.motionSettings[axis]==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());return;
  }
  if(cascade && r.c.autoTune.phase==AutoTunePhase::Failed && std::string(r.c.autoTune.reason)=="tracking_unstable") {
    // A fast asymmetric plant can pass a one-shot approach yet fail every
    // stream trial. The new acceptance gate must refuse Apply in that case.
    CHECK(r.c.autoTune.minimumSpeed>0);
    for(const auto &result:r.c.autoTune.trackingResults)CHECK(result.rejected);
    CHECK(r.c.motionSettings[axis]==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());
    CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);return;
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
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
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
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n)r.step();
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
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
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
  for(unsigned n=0;n<24050 && t.active();++n)r.step();
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
  for(unsigned n=0;n<24050 && t.active();++n)r.step();
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
static void reclaimStroke(unsigned axis,int direction,unsigned hz,bool impossible=false,bool repeated=false) {
  Rig r(false,axis,0xfffff000u);r.periodMs=1000/hz;
  CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  r.positiveStart=r.negativeStart=28;r.sustaining=16;r.gain=.7f;
  auto &m=r.c.motionSettings[axis];m.kickEnabled=true;m.kickPercent=28;m.kickMs=800;
  m.movementDegrees=1;m.stoppedVelocity=1;m.outputLimitPercent=50;
  const auto saved=m;const auto speed=r.c.speeds[axis];CHECK(r.start()==Result::Ok);
  auto &t=r.c.autoTune;
  r.position=impossible?90:90+direction*52;r.now+=20;
  r.c.resetFeedback(axis,r.now);r.c.feedbackAt(axis,r.position,true,r.now);
  if(impossible){t.lower=80;t.upper=100;}
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Low;t.direction=t.nextDirection=direction;
  t.threshold=1;t.nextLevel=18;t.searchLevel=28;t.relocate=true;t.phasePosition=r.position;
  t.lastPosition=r.position;t.positionAt=r.now;t.stoppingDistance=10;t.peakVelocity=200;
  if(repeated){t.peakVelocity=30;t.relocations=2;}
  for(unsigned n=0;n<300 && t.phase==AutoTunePhase::Coast;++n)r.step();
  if(impossible) {
    CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="stopping_distance");
    CHECK(r.position==90);CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());return;
  }
  CHECK(t.phase==AutoTunePhase::Reposition);
  CHECK(direction>0?r.c.axes[axis].target<t.lower+20:r.c.axes[axis].target>t.upper-20);
  for(unsigned n=0;n<8000 && t.phase==AutoTunePhase::Reposition;++n) {
    r.step();CHECK(r.position>=t.lower && r.position<=t.upper);
    CHECK(r.c.axes[axis].speedOutputPercent<=50.001f);
  }
  CHECK(t.phase==AutoTunePhase::Low);CHECK(direction*(90-r.position)>25);
  r.c.cancelAutoTune(r.now);CHECK(m==saved);CHECK(r.c.speeds[axis]==speed);CHECK(!r.c.moving());
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
  for(unsigned n=0;n<24050 && t.active();++n)r.step();
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
  for(unsigned n=0;n<6000 && t.active() && t.phase!=AutoTunePhase::Tracking;++n) {
    r.step();CHECK(r.c.axes[0].speedOutputPercent<=50.001f);
    CHECK(r.position>=t.lower && r.position<=t.upper);
    CHECK(!r.c.axes[0].holdArmed);
  }
  // This regression isolates the measured model, startup and bidirectional
  // approach. Stream acceptance is exercised separately, with its own gate.
  CHECK(t.phase==AutoTunePhase::Tracking);CHECK(t.minimumCompletePending);CHECK(t.validationPass==1);
  CHECK(std::abs(t.validationError)<=2);
  r.c.cancelAutoTune(r.now);CHECK(!r.c.moving());
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
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
    if(jam && r.c.autoTune.phase==AutoTunePhase::Low)r.jam=true;
    r.step();raised|=r.c.autoTune.minimumLevel>5;
    if(!jam)CHECK(r.c.axes[0].motionError==MotionError::None);
    CHECK(r.c.axes[0].speedOutputPercent<=50.001f);
  }
  CHECK(raised);CHECK(!r.c.moving());CHECK(m==original);CHECK(r.c.speeds[0]==speed);
  if(jam) {
    CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);
  } else {
    if(r.c.autoTune.phase!=AutoTunePhase::Complete) {
      const auto &t=r.c.autoTune;const auto &f=t.failure;
      std::cerr<<"adaptive minimum "<<cascade<<" "<<t.reason<<" phase "<<Controller::autoTunePhaseName(f.phase)
        <<" pos "<<f.position<<" target "<<f.target<<" velocity "<<f.velocity<<" output "<<f.output
        <<" error "<<t.validationError<<" verified tracking "<<t.trackingVerified<<" candidate "<<t.trackingBest<<'\n';
    }
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
  for(auto mode:{Mode::Manual,Mode::Auto,Mode::AutoHour}) for(unsigned axis:{0u,1u}) {
    Rig r(true,axis);r.c.motionSettings[axis].holdEnabled=true;
    const auto settings=r.c.motionSettings[axis];const auto speed=r.c.speeds[axis];
    CHECK(r.c.setRange({-50,190},AZ_CUSTOM));
    r.c.elRange={-45,190};
    r.c.feedbackAt(0,90,true,0);r.c.feedbackAt(1,90,true,0);
    // Center takes over existing motion and discards deferred commands.
    CHECK(r.c.jog(1-axis,1,false,0)==Result::Ok);
    r.c.pendingValid=true;r.c.presetActive=true;r.c.mode=mode;
    Protocol p(r.c);Settings cfg;
    CHECK(api(r.c,p,cfg,"/api/motion/autotune",{{"action","center"},{"axis",axis?"el":"az"},{"rangeRevision",std::to_string(r.c.rangeRevision)}},0).status==200);
    CHECK(r.c.axes[axis].target==(axis?72.5f:70.f));CHECK(r.c.axes[axis].tracking);
    CHECK(r.c.mode==Mode::Manual);CHECK(!r.c.pendingValid && !r.c.presetActive);
    CHECK(!r.c.axes[1-axis].direction && !r.c.axes[1-axis].wanted);
    CHECK(!r.c.axes[axis].holdArmed);CHECK(!r.c.axes[1-axis].tracking);
    CHECK(r.c.motionSettings[axis]==settings);CHECK(r.c.speeds[axis]==speed);
    r.c.stopNow(-1,20);CHECK(!r.c.moving());
    CHECK(api(r.c,p,cfg,"/api/motion/autotune",{{"action","center"},{"axis",axis?"el":"az"},{"rangeRevision","0"}},20).status==409);
    CHECK(!r.c.axes[axis].tracking);
  }
  Rig r;CHECK(r.start()==Result::Ok);CHECK(r.c.centerForAutoTune(0,0)==Result::Busy);
  r.c.cancelAutoTune(0);r.c.mode=Mode::Auto;r.c.axes[1].motionError=MotionError::Stalled;
  CHECK(r.c.centerForAutoTune(0,0)==Result::Unavailable);CHECK(!r.c.moving());CHECK(r.c.mode==Mode::Auto);
  r.c.resetMotion(-1,0);r.c.axes[0].calibrated=false;CHECK(r.c.centerForAutoTune(0,0)==Result::NotCalibrated);
  r.c.axes[0].calibrated=true;CHECK(r.c.centerForAutoTune(0,501)==Result::Unavailable);
  CHECK(r.c.centerForAutoTune(2,0)==Result::Unavailable);
  r.c.configurationLocked=true;CHECK(r.c.centerForAutoTune(0,0)==Result::Unavailable);
  for(auto mode:{Mode::Auto,Mode::AutoHour}) for(unsigned axis:{0u,1u}) {
    Rig tune(false,axis);tune.c.mode=mode;tune.c.pendingValid=true;tune.c.presetActive=true;
    CHECK(tune.start(1)==Result::Invalid);CHECK(tune.c.mode==mode && tune.c.pendingValid);
    CHECK(tune.start()==Result::Ok);CHECK(tune.c.mode==Mode::Manual);
    CHECK(!tune.c.pendingValid && !tune.c.presetActive);CHECK(tune.c.autoTune.active());
    tune.c.cancelAutoTune(0);CHECK(tune.c.mode==Mode::Manual);
  }
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
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
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
  CHECK(std::string(j.data()).find("\"methodVersion\":14")!=std::string::npos);
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
  for(unsigned n=0;n<24050 && cascade.c.autoTune.active();++n)cascade.step();
  CHECK(cascade.c.autoTune.phase==AutoTunePhase::Complete);
  const auto &t=cascade.c.autoTune;const auto &a=t.measurements[0],&b=t.measurements[1];
  const float gain=std::max((a.highVelocity-a.lowVelocity)/(a.highOutput-a.lowOutput),
      (b.highVelocity-b.lowVelocity)/(b.highOutput-b.lowOutput));
  const float tau=std::clamp(std::max(a.responseSeconds,b.responseSeconds),.05f,5.f);
  const float delay=std::max(.02f,float(t.proposedMotion.velocityWindowMs)/2000);
  const float lambda=std::max(.5f,2*(tau+delay))*options.lambdaFactor;
  CHECK(std::abs(t.trackingBase.velocityKp-tau/(gain*(lambda+delay)))<.0001f);
  CHECK(std::abs(t.trackingBase.positionKp-options.positionGainFactor/(4*lambda))<.0001f);
  CHECK(cascade.c.runtimeSpeed(0)==sliderSpeed); // Autotune restores the manual selection.
  Rig longWindow(false,0,0xfffff000u);longWindow.gain=.08f;options={};
  options.searchStartPercent=35;options.plateauMinMs=1000;options.plateauTimeoutMs=6000;
  CHECK(longWindow.c.startAutoTune(0,RegulatorMode::Simple,60,50,longWindow.now,options)==Result::Ok);
  bool decimated=false;
  for(unsigned n=0;n<24050 && longWindow.c.autoTune.active();++n) {
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
      // An actually measured coast that cannot fit the stroke still falls
      // back to the confirmed work point; a historical peak alone does not.
      t.nextLevel=40;t.relocate=true;t.peakVelocity=300;t.stoppingDistance=60;t.phasePosition=r.position;
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
  if(peak>=800) {
    CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="stopping_distance");
    CHECK(!r.c.moving());CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);return;
  }
  if(t.phase!=AutoTunePhase::Validate)std::cerr<<"cascade reserve phase "<<Controller::autoTunePhaseName(t.phase)<<" reason "<<t.reason<<" peak "<<t.peakVelocity<<" stop "<<t.stoppingDistance<<" fault "<<r.c.fault<<" pos "<<r.position<<" origin "<<t.validationOrigin<<" target "<<t.validationTarget<<"\n";
  CHECK(t.phase==AutoTunePhase::Validate);
  if(peak==300)CHECK(t.validationDirectional);
  else CHECK(t.validationOrigin>t.lower+70);
  CHECK(t.validationTarget<t.upper-70);
  CHECK(t.validationTarget-t.validationOrigin>2*m.positionTolerance+m.movementDegrees);
  CHECK(t.proposedMotion.kickPercent==30);CHECK(t.proposedMotion.velocity>70);
  CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);r.c.cancelAutoTune(r.now);
}
static void trackingCancellation(unsigned candidate,bool confirming=false) {
  Rig r(true,1,0xfffff000u);const auto original=r.c.motionSettings[1];const auto speed=r.c.speeds[1];
  CHECK(r.c.setRuntimeSpeed(1,original.velocity/2,r.now)==Result::Ok);
  const float selected=r.c.runtimeSpeed(1);
  CHECK(r.start()==Result::Ok);
  bool reached=false;
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
    r.step();const auto &t=r.c.autoTune;
    if((!confirming && t.phase==AutoTunePhase::Tracking && t.trackingCandidate==candidate) ||
        (confirming && t.phase==AutoTunePhase::Minimum && t.trackingVerified)) {reached=true;break;}
  }
  CHECK(reached);CHECK(r.c.autoTune.active());
  CHECK(r.c.target({100,100,false,true},true,r.now)==Result::Busy);
  CHECK(r.c.jog(1,1,false,r.now)==Result::Busy);
  CHECK(r.c.setControlFrequency(100,r.now)==Result::Busy);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
  if(candidate%2)CHECK(r.c.cancelAutoTune(r.now)==Result::Ok);
  else r.c.stopNow(-1,r.now);
  CHECK(r.c.autoTune.phase==AutoTunePhase::Cancelled);CHECK(!r.c.moving());
  CHECK(r.c.axes[1].speedOutputPercent==0);CHECK(!r.c.axes[1].tracking);
  CHECK(r.c.motionSettings[1]==original);CHECK(r.c.speeds[1]==speed);
  CHECK(r.c.runtimeSpeed(1)==selected);
  char out[4096];JsonWriter j(out,sizeof out);CHECK(writeAutoTune(j,r.c,true));
  CHECK(std::string(j.data()).find("\"verified\":false")!=std::string::npos);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
}
static void trackingFeedbackAndDeadline(bool timeout) {
  Rig r(true);const auto original=r.c.motionSettings[0];const auto speed=r.c.speeds[0];
  CHECK(r.start()==Result::Ok);
  for(unsigned n=0;n<6000 && r.c.autoTune.phase!=AutoTunePhase::Tracking && r.c.autoTune.active();++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Tracking);
  if(timeout) {
    // Both deadlines are wrap-safe and finite; entering tracking extends only
    // the total run cap, not the control-gap/feedback deadlines.
    r.c.autoTune.startedAt=r.now-r.c.autoTune.trackingTimeoutMs;
    r.step();CHECK(std::string(r.c.autoTune.reason)=="timeout");
  } else {
    for(unsigned n=0;n<70 && r.c.autoTune.active();++n)r.step(false);
    CHECK(r.c.axes[0].motionError==MotionError::FeedbackStale);
  }
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(!r.c.moving());
  CHECK(r.c.motionSettings[0]==original);CHECK(r.c.speeds[0]==speed);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
}
static void longTrackingDeadline(unsigned axis,unsigned hz,uint32_t initialTime) {
  Rig r(true,axis,initialTime);r.periodMs=1000/hz;
  CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  CHECK(axis==0?r.c.setRange({-50,190},AZ_CUSTOM):r.c.setElRange({-50,190},EL_CUSTOM));
  r.position=65.27f;r.c.resetFeedback(axis,r.now);r.c.feedbackAt(axis,r.position,true,r.now);
  auto &m=r.c.motionSettings[axis];m.velocity=81.93f;m.acceleration=m.deceleration=5;m.jerk=20;
  m.positionTolerance=3;m.holdResumeDegrees=6;m.movementDegrees=3;m.velocityWindowMs=240;m.stoppedVelocity=1.35f;
  const auto original=m;CHECK(r.start(90)==Result::Ok);auto &t=r.c.autoTune;
  t.proposedMotion=original;t.minimumSetpoint=13.95f;t.minimumSpeed=20.34f;t.stoppingDistance=18.93f;
  t.measurements[0]={35,19,40,6.64f,103.16f,.08f,18.93f,1548};
  t.measurements[1]={35,19,30,13.95f,64.69f,.03f,8.78f,1478};
  t.phase=AutoTunePhase::Coast;t.nextPhase=AutoTunePhase::Tracking;t.threshold=1.35f;
  for(unsigned n=0;n<1000/r.periodMs && !t.trackingPlanned && t.active();++n)r.step();
  CHECK(t.trackingPlanned);CHECK(t.trackingTimeoutMs>480000);CHECK(t.trackingTimeoutMs<=1200000);
  t.phase=AutoTunePhase::Validate;t.phaseAt=r.now;t.tickAt=r.now-20;t.trackingVerified=true;
  t.startedAt=r.now-480000;r.step();CHECK(t.active());CHECK(t.phase==AutoTunePhase::Validate);
  t.startedAt=r.now-t.trackingTimeoutMs;t.tickAt=r.now-20;r.step();
  CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="timeout");
  CHECK(!r.c.moving());CHECK(r.c.motionSettings[axis]==original);
}
static void trackingAtHigherRate(unsigned hz) {
  Rig r(true,1,0xfffff000u);r.periodMs=1000/hz;
  r.timeConstant=.06f;r.positiveStart=r.negativeStart=25;
  CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  const auto original=r.c.motionSettings[1];CHECK(r.start()==Result::Ok);
  unsigned periods=0,directions=0;
  for(unsigned n=0;n<481000/r.periodMs && r.c.autoTune.active();++n) {
    r.step();const auto &t=r.c.autoTune;
    if(t.phase==AutoTunePhase::Tracking) {
      periods|=t.trackingPeriodMs==200?1:2;directions|=t.direction>0?1:2;
      CHECK(r.c.axes[1].speedOutputPercent<=50.001f);
      const float commanded=t.trackingOrigin+t.direction*t.trackingVelocity*(t.trackingCommand*t.trackingPeriodMs)/1000;
      CHECK(std::abs(r.c.axes[1].target-commanded)<.0001f);
    }
  }
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)std::cerr<<"tracking rate "<<hz<<" reason "<<r.c.autoTune.reason<<" phase "<<Controller::autoTunePhaseName(r.c.autoTune.phase)<<" case "<<r.c.autoTune.trackingCase<<" selected "<<r.c.autoTune.trackingBest<<" time "<<r.now<<'\n';
  if(r.c.autoTune.phase!=AutoTunePhase::Complete)for(const auto &v:r.c.autoTune.trackingResults)std::cerr<<"case "<<v.completed<<" reason "<<v.reason<<" peak "<<v.peakError<<" over "<<v.overshoot<<" slow "<<r.c.autoTune.trackingSlow<<" fast "<<r.c.autoTune.trackingFast<<'\n';
  CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);CHECK(r.c.autoTune.trackingVerified);
  CHECK(periods==3);CHECK(directions==3);CHECK(!r.c.moving());
  CHECK(r.c.motionSettings[1]==original);
  CHECK(r.c.controlPeriodMs()==r.periodMs);
}
static Rig savedProfile(bool cascade=true) {
  Rig r(cascade);AutoTuneOptions options;options.stages=TuneModel;
  CHECK(r.c.startAutoTune(0,r.c.motionSettings[0].mode,60,50,r.now,options)==Result::Ok);
  CHECK(r.c.autoTune.options.stages==(TuneKick|TuneModel));
  bool sawModel=false;
  for(unsigned n=0;n<6000 && r.c.autoTune.active();++n) {
    r.step();const auto phase=r.c.autoTune.phase;
    sawModel|=phase==AutoTunePhase::Low;
    CHECK(phase!=AutoTunePhase::Minimum);CHECK(phase!=AutoTunePhase::Tracking);
  }
  CHECK(sawModel);CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);
  CHECK(r.c.autoTune.minimumSetpoint==0);CHECK(!r.c.autoTune.trackingVerified);
  char data[4096];JsonWriter json(data,sizeof data);CHECK(writeAutoTune(json,r.c,true));
  CHECK(std::string(json.data()).find("\"minimumVerified\":false")!=std::string::npos);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Ok);
  r.position=90;r.velocity=0;r.c.resetFeedback(0,r.now);r.c.feedbackAt(0,90,true,r.now);
  r.c.motionSettings[0].holdEnabled=cascade;
  return r;
}
static void minimumConfirmationRecovery(unsigned hz,bool tracking,bool exhausted=false) {
  auto r=savedProfile();r.periodMs=1000/hz;CHECK(r.c.setControlFrequency(hz,r.now)==Result::Ok);
  const auto original=r.c.motionSettings[0];const auto speed=r.c.speeds[0];
  AutoTuneOptions options;options.stages=TuneMinimum|(tracking?TuneSlow|TuneFast:0);
  CHECK(r.c.startAutoTune(0,RegulatorMode::Cascade,60,40,r.now,options)==Result::Ok);
  auto &t=r.c.autoTune;bool injected=false,retried=false;float previous=0;
  for(unsigned n=0;n<1200000/r.periodMs && t.active();++n) {
    if(!injected && t.phase==AutoTunePhase::Minimum && t.minimumConfirm &&
        (!tracking || t.trackingVerified)) {
      previous=t.minimumCandidate;injected=true;r.jam=true;
      if(exhausted)t.minimumCandidate=t.proposedMotion.velocity;
    }
    r.step();
    if(injected && !retried && t.minimumCandidate>previous && !exhausted) {
      CHECK(t.active());CHECK(t.phase==AutoTunePhase::Coast);CHECK(t.minimumSpeed==0);
      CHECK(!t.minimumConfirm);CHECK(!t.minimumCompletePending);CHECK(t.minimumPass==0);
      CHECK(r.c.axes[0].speedOutputPercent==0);CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);
      retried=true;r.jam=false;
    }
    CHECK(r.c.axes[0].speedOutputPercent<=40.001f);
  }
  CHECK(injected);CHECK(!r.c.moving());CHECK(r.c.motionSettings[0]==original);CHECK(r.c.speeds[0]==speed);
  if(exhausted){CHECK(t.phase==AutoTunePhase::Failed);CHECK(std::string(t.reason)=="minimum_speed_unstable");}
  else {if(t.phase!=AutoTunePhase::Complete)std::cerr<<"minimum reconfirm "<<hz<<" tracking "<<tracking<<" "<<t.reason<<'\n';
    CHECK(retried);CHECK(t.phase==AutoTunePhase::Complete);CHECK(t.minimumCompletePending);
    CHECK(t.minimumSetpoint>previous);if(tracking)CHECK(t.trackingVerified);}
}
static void partialTune(unsigned stages,bool cancel=false,bool cascade=true) {
  auto r=savedProfile(cascade);const auto original=r.c.motionSettings[0];const auto speed=r.c.speeds[0];
  const float setpoint=r.c.runtimeSpeed(0);const uint32_t began=r.now;
  AutoTuneOptions options;options.stages=stages;
  CHECK(r.c.startAutoTune(0,r.c.motionSettings[0].mode,60,40,r.now,options)==Result::Ok);
  unsigned seen=0,cases[3]={0,0,0};bool entered=false;
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
    r.step();const auto &t=r.c.autoTune;const auto phase=t.phase;
    if(phase==AutoTunePhase::Start)seen|=TuneKick;
    if(phase==AutoTunePhase::Low || phase==AutoTunePhase::High)seen|=TuneModel;
    if(phase==AutoTunePhase::Minimum)seen|=TuneMinimum;
    if(phase==AutoTunePhase::Tracking) {
      const unsigned group=t.trackingCase<4?TuneSlow:TuneFast;seen|=group;
      CHECK((stages&group)!=0);cases[t.trackingCandidate]|=1u<<t.trackingCase;
      char data[2048];JsonWriter json(data,sizeof data);CHECK(writeAutoTune(json,r.c));
      const auto total=options.trackingCases()*3;
      CHECK(std::string(json.data()).find("\"trackingTrials\":"+std::to_string(total))!=std::string::npos);
    }
    CHECK((seen&~stages)==0);CHECK(r.c.axes[0].speedOutputPercent<=40.001f);
    if(cancel && seen) {entered=true;r.c.stopNow(-1,r.now);break;}
  }
  const auto &t=r.c.autoTune;
  if(!cancel && t.phase!=AutoTunePhase::Complete)std::cerr<<"partial "<<stages<<" reason "<<t.reason<<" phase "<<Controller::autoTunePhaseName(t.phase)<<'\n';
  CHECK(r.c.motionSettings[0]==original);CHECK(r.c.speeds[0]==speed);CHECK(!r.c.moving());
  CHECK(r.c.runtimeSpeed(0)==setpoint);
  if(cancel) {
    CHECK(entered);CHECK(t.phase==AutoTunePhase::Cancelled);
    CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Unavailable);return;
  }
  CHECK(t.phase==AutoTunePhase::Complete);CHECK(seen==stages);
  CHECK(t.proposedSpeed==speed);CHECK(t.proposedMotion.holdEnabled==original.holdEnabled);
  CHECK(t.proposedMotion.outputLimitPercent==original.outputLimitPercent);
  auto expected=original;
  if(stages&TuneKick) {
    expected.kickEnabled=true;expected.kickPercent=t.proposedMotion.kickPercent;expected.kickMs=t.proposedMotion.kickMs;
    CHECK(t.validationPass==1);
  }
  if(stages&(TuneSlow|TuneFast)) {
    expected.positionKp=t.proposedMotion.positionKp;expected.velocityKp=t.proposedMotion.velocityKp;expected.velocityKi=t.proposedMotion.velocityKi;
    CHECK(t.trackingVerified);CHECK(t.validationPass==1);
    CHECK(t.trackingResults[t.trackingBest].completed==options.trackingCases());
    const unsigned mask=((stages&TuneSlow)?15u:0u)|((stages&TuneFast)?240u:0u);
    CHECK(cases[t.trackingBest]==mask);
  } else CHECK(!t.trackingVerified);
  CHECK(t.proposedMotion==expected);
  if(stages&TuneMinimum) {CHECK(t.minimumSetpoint>0);CHECK(t.minimumCompletePending);}
  else {CHECK(t.minimumSetpoint==0);CHECK(!t.minimumCompletePending);}
  CHECK(r.c.applyAutoTune(t.id,r.now)==Result::Ok);CHECK(r.c.motionSettings[0]==expected);
  if(!(stages&TuneMinimum))CHECK(r.c.runtimeSpeed(0)==setpoint);
  std::cout<<"partial stages "<<stages<<" elapsed "<<uint32_t(r.now-began)<<" ms\n";
}
static void invalidStages() {
  Rig r;const auto saved=r.c.motionSettings[0];const auto epoch=r.c.stopEpoch;
  for(unsigned mask:{0u,32u,255u,unsigned(TuneSlow),unsigned(TuneFast),unsigned(TuneSlow|TuneFast)}) {
    AutoTuneOptions options;options.stages=mask;
    CHECK(r.c.startAutoTune(0,RegulatorMode::Simple,60,50,0,options)==Result::Invalid);
    CHECK(!r.c.autoTune.active());CHECK(r.c.motionSettings[0]==saved);CHECK(r.c.stopEpoch==epoch);
  }
}
static void stageApi() {
  Rig r(true);Protocol protocol(r.c);Settings settings;
  Form form{{"action","start"},{"axis","az"},{"mode","cascade"},{"id","0"},{"travel","60"},{"outputLimit","50"}};
  for(const char *value:{"0","32","-1","1.5","minimum",""}) {
    form["tuneStages"]=value;
    CHECK(api(r.c,protocol,settings,"/api/motion/autotune",form,r.now).status==400);
    CHECK(!r.c.autoTune.active());CHECK(r.c.autoTune.id==0);
  }
  for(unsigned mask:{2u,4u,8u,16u,24u}) {
    form["tuneStages"]=std::to_string(mask);form["id"]=std::to_string(r.c.autoTune.id);
    CHECK(api(r.c,protocol,settings,"/api/motion/autotune",form,r.now).status==200);
    CHECK(r.c.autoTune.options.stages==(mask==TuneModel?unsigned(TuneModel|TuneKick):mask));
    CHECK(r.c.cancelAutoTune(r.now)==Result::Ok);CHECK(!r.c.moving());
  }
}
static void modelAndFastOnly() {
  Rig r(true);AutoTuneOptions options;options.stages=TuneModel|TuneFast;
  CHECK(r.c.startAutoTune(0,RegulatorMode::Cascade,60,50,r.now,options)==Result::Ok);
  CHECK(r.c.autoTune.options.stages==(TuneKick|TuneModel|TuneFast));
  bool sawModel=false,sawFast=false;
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n) {
    r.step();const auto &t=r.c.autoTune;
    CHECK(t.phase!=AutoTunePhase::Minimum);
    if(t.phase==AutoTunePhase::Low)sawModel=true;
    if(t.phase==AutoTunePhase::Tracking) {sawFast=true;CHECK(t.trackingCase>=4);}
  }
  CHECK(sawModel);CHECK(sawFast);CHECK(r.c.autoTune.phase==AutoTunePhase::Complete);
  CHECK(r.c.autoTune.minimumSetpoint==0);CHECK(!r.c.autoTune.minimumCompletePending);
  CHECK(r.c.autoTune.trackingVerified);CHECK(r.c.autoTune.trackingResults[r.c.autoTune.trackingBest].completed==4);
}
static void partialProfileMustNotBeClipped() {
  Rig r(true);auto &m=r.c.motionSettings[0];m.kickEnabled=true;m.kickPercent=55;
  const auto saved=m;const auto epoch=r.c.stopEpoch;
  AutoTuneOptions options;options.stages=TuneMinimum;
  CHECK(r.c.startAutoTune(0,RegulatorMode::Cascade,60,50,0,options)==Result::Invalid);
  CHECK(m==saved);CHECK(r.c.stopEpoch==epoch);
  options.stages=TuneKick;CHECK(r.c.startAutoTune(0,RegulatorMode::Cascade,60,50,0,options)==Result::Ok);
  r.c.cancelAutoTune(0);m.sustainingPercent=51;
  CHECK(r.c.startAutoTune(0,RegulatorMode::Cascade,60,50,0,options)==Result::Invalid);
  CHECK(m.sustainingPercent==51);
  auto simple=savedProfile(false);simple.c.speeds[0].speedPercent=simple.c.speeds[0].approachSpeedPercent=80;
  const auto speed=simple.c.speeds[0];options.stages=TuneMinimum;
  CHECK(simple.c.startAutoTune(0,RegulatorMode::Simple,60,40,simple.now,options)==Result::Ok);
  for(unsigned n=0;n<100 && simple.c.autoTune.phase!=AutoTunePhase::Minimum;++n)simple.step();
  CHECK(simple.c.autoTune.phase==AutoTunePhase::Minimum);
  CHECK(simple.c.autoTune.minimumCandidate==40);CHECK(simple.c.runtimeSpeed(0)==40);
  simple.c.cancelAutoTune(simple.now);CHECK(simple.c.speeds[0]==speed);
  simple.position=90;simple.velocity=0;simple.c.resetFeedback(0,simple.now);simple.c.feedbackAt(0,90,true,simple.now);
  simple.c.speeds[0].speedPercent=20;simple.c.motionSettings[0].sustainingPercent=30;
  simple.c.speeds[0].accelerationMs=10000;simple.c.motionSettings[0].kickEnabled=false;
  CHECK(simple.c.startAutoTune(0,RegulatorMode::Simple,60,40,simple.now,options)==Result::Ok);
  bool observedRamp=false;
  for(unsigned n=0;n<250 && simple.c.autoTune.active();++n) {
    simple.step();const auto &t=simple.c.autoTune;
    if(t.phase==AutoTunePhase::Minimum && simple.c.axes[0].motionPhase==MotionPhase::Run && simple.c.axes[0].speedOutputPercent<27) {
      CHECK(t.minimumCandidate==30);CHECK(t.minimumSamples==0);observedRamp=true;
    }
  }
  CHECK(observedRamp);simple.c.cancelAutoTune(simple.now);
}
static void returnMustWork() {
  Rig r(true);const auto original=r.c.motionSettings[0];const auto speed=r.c.speeds[0];
  CHECK(r.start()==Result::Ok);
  for(unsigned n=0;n<24050 && r.c.autoTune.active() && !r.c.autoTune.validationPass;++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Validate);
  CHECK(r.c.autoTune.validationPass==1);
  r.jam=true; // Successful outward approach must not conceal a failed return.
  for(unsigned n=0;n<24050 && r.c.autoTune.active();++n)r.step();
  CHECK(r.c.autoTune.phase==AutoTunePhase::Failed);CHECK(!r.c.moving());
  CHECK(r.c.motionSettings[0]==original);CHECK(r.c.speeds[0]==speed);
  CHECK(r.c.applyAutoTune(r.c.autoTune.id,r.now)==Result::Unavailable);
}
int main(){try{fullCalibratedTravel(0);fullCalibratedTravel(1);for(unsigned hz:{50u,200u})for(bool tracking:{false,true}){minimumConfirmationRecovery(hz,tracking);minimumConfirmationRecovery(hz,tracking,true);}for(unsigned axis:{0u,1u})for(unsigned hz:{50u,200u})for(uint32_t initialTime:{0u,0xfffff000u})longTrackingDeadline(axis,hz,initialTime);for(unsigned axis:{0u,1u})for(unsigned hz:{50u,200u})for(unsigned stages:{unsigned(TuneSlow),unsigned(TuneFast),unsigned(TuneSlow|TuneFast)})partialHighSpeedTrackingPlan(axis,hz,stages);capturedTrackingPlan(0,50,true);capturedTrackingPlan(1,200,true);for(unsigned axis:{0u,1u})for(unsigned hz:{50u,200u})measuredPlantTracking(axis,hz);for(unsigned axis:{0u,1u})for(unsigned hz:{50u,200u}){capturedTrackingPlan(axis,hz);for(int direction:{-1,1})for(int mode:{0,1,2,3})trackingReferenceAndCompletion(axis,direction,hz,mode);}for(unsigned axis:{0u,1u})for(int direction:{-1,1})for(unsigned hz:{50u,200u})capturedMinimumRecovery(axis,direction,hz);capturedMinimumRecovery(0,-1,200,1);capturedMinimumRecovery(1,1,50,2);trackingRefitsLongTrajectory();for(unsigned axis:{0u,1u})for(int direction:{-1,1})for(unsigned hz:{50u,200u}){retryShortPreparation(axis,direction,hz);retryShortPreparation(axis,direction,hz,0,true);}retryShortPreparation(0,1,200,1);retryShortPreparation(1,-1,50,2);retryShortPreparation(1,-1,200,3);retryShortPreparation(0,1,50,5);for(unsigned axis:{0u,1u})for(unsigned hz:{50u,200u}){stationaryOtherAxisTolerance(axis,hz,.1f,.1f);stationaryOtherAxisTolerance(axis,hz,9,2);stationaryOtherAxisTolerance(axis,hz,.1f,7);stationaryOtherAxisTolerance(axis,hz,.1f,.1f,true);}for(unsigned axis:{0u,1u})for(int direction:{-1,1})for(unsigned hz:{50u,200u}){capturedTravelFailure(axis,direction,hz);capturedTravelFailure(axis,direction,hz,true);}for(unsigned axis:{0u,1u})for(unsigned hz:{50u,200u})overlappingReserves(axis,hz);overlappingReserves(0,200,1);overlappingReserves(1,50,2);for(unsigned axis:{0u,1u})for(unsigned hz:{50u,200u})noisyStopKeepsFeedbackWindow(axis,hz);coastDeadlineAndFailureSnapshot(false);coastDeadlineAndFailureSnapshot(true);targetDeadlineIdentifiesPhase(AutoTunePhase::Validate);targetDeadlineIdentifiesPhase(AutoTunePhase::Reposition);minimumCanRaiseSelectedKick();for(unsigned axis:{0u,1u}){minimumKickFailureRetries(axis);for(unsigned hz:{50u,200u}){stationarySearchRaisesOutput(axis,hz);stationarySearchRaisesOutput(axis,hz,true);for(int direction:{-1,1})reclaimStroke(axis,direction,hz);}}reclaimStroke(0,1,50,true);reclaimStroke(0,1,50,false,true);stationaryTrackingTriesOtherGains(false);stationaryTrackingTriesOtherGains(true);partialProfileMustNotBeClipped();modelAndFastOnly();partialTune(TuneKick|TuneMinimum);partialTune(TuneKick|TuneFast);stageApi();invalidStages();partialTune(TuneMinimum,false,false);partialTune(TuneKick,false,false);partialTune(TuneKick);partialTune(TuneMinimum);partialTune(TuneSlow);partialTune(TuneFast);partialTune(TuneSlow|TuneFast);partialTune(TuneMinimum|TuneSlow);partialTune(TuneMinimum,true);partialTune(TuneFast,true);trackingAtHigherRate(100);trackingAtHigherRate(200);trackingCancellation(0);trackingCancellation(1);trackingCancellation(2);trackingCancellation(0,true);trackingFeedbackAndDeadline(false);trackingFeedbackAndDeadline(true);startupConfirmation(0,1);startupConfirmation(1,1,0xfffff000u);startupConfirmation(0,5);startupConfirmation(1,5);startupStallDeadline(false);startupStallDeadline(true);runtimeMethodology();cascadeUsesMeasuredStoppingReserve(250);cascadeUsesMeasuredStoppingReserve(300);cascadeUsesMeasuredStoppingReserve(800);validationReturnStaysInsideReserve();measuredWorkSurvivesShortStroke();returnMustWork();savedKickIsNotMinimum();completed(false,0);completed(true,0);completed(false,1);completed(true,1,0xfffff000u);safety();highStiction();fastBounded(60);fastBounded(20);fastBounded(10);fastBounded(60,true,1);inertialPlateaus();inertialPlateaus(true);marginalWorkOutput();adaptiveProbeStartup();adaptiveProbeStartup(90);strongerRestart(false);strongerRestart(true);validationAfterStrongKick();cascadeBreakawayRecovery();asymmetricCoast();workOutputHeadroom();repositionDrag();repositionDrag(true);repositionStoppingRoom();adaptiveMinimum();adaptiveMinimum(true);adaptiveMinimum(false,true);validationRetry();exhaustedMinimum();preparation();transport();std::cout<<"PASS autotune: "<<checks<<" checks\n";}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
