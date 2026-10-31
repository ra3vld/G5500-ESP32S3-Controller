#include "motion_fixture.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <new>
static bool deny=false;
static unsigned allocations=0;
void *operator new(size_t n) {
  if(deny)throw std::bad_alloc();
  ++allocations;
  if(auto p=std::malloc(n?n:1))return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept {std::free(p);}
void operator delete(void *p,size_t) noexcept {std::free(p);}
using namespace forkrot;
int main() {
  // Relay operation and a remote controller never reserve the planner.
  deny=true;
  Controller relay;
  relay.feedback(200,90,true,true);
  assert(!relay.variableSpeed(0) && !relay.variableSpeed(1));
  assert(relay.prepareMotion());
  assert(relay.target({220,100,true,true},false,0)==Result::Ok);
  relay.tick(0);relay.stopNow(-1,1);
  Controller remote;Protocol protocol(remote);Hud hud(remote,protocol);
  remote.hardware.simulation=true;remote.remote=&hud;
  assert(remote.prepareMotion());
  assert(allocations==0);
  Controller pwm;
  auto config=pwm.hardware;config.drive=DRIVE_BTS7960;
  pwm.configureHardware(config);commissionTestMotion(pwm);
  pwm.feedbackAt(0,200,true,100);pwm.feedbackAt(1,90,true,100);
  assert(!pwm.prepareMotion());
  assert(pwm.target({220,100,true,true},false,100)==Result::Unavailable);
  assert(pwm.jog(0,1,false,100)==Result::Unavailable);
  assert(pwm.manualDrive(1,1,1,1,1,1,0,false,100)==Result::Unavailable);
  assert(pwm.startAutoTune(0,RegulatorMode::Simple,60,50,100)==Result::Unavailable);
  pwm.tick(100);
  assert(!pwm.moving() && !pwm.axes[0].wanted && !pwm.axes[1].wanted && !pwm.autoTune.active());
  deny=false;
  const auto before=allocations;
  assert(pwm.prepareMotion() && allocations==before+1);
  deny=true;
  assert(pwm.prepareMotion());
  assert(pwm.startAutoTune(0,RegulatorMode::Simple,60,50,100)==Result::Ok);
  pwm.cancelAutoTune(100);
  assert(pwm.target({220,100,true,true},false,120)==Result::Ok);
  for(unsigned now=120;now<2000;now+=20) {
    pwm.feedbackAt(0,200+(now-120)*.001f,true,now);
    pwm.feedbackAt(1,90+(now-120)*.001f,true,now);
    pwm.tick(now);
  }
  pwm.stopNow(-1,2000);
  assert(allocations==before+1);
  // STOP clears transient regulation while keeping feedback and timing.
  MotionRuntime state;
  state.feedback.sample(10,true,10,600);state.feedback.sample(11,true,30,600);
  state.feedback.staticCoastFactor={.2f,.3f};state.feedback.rejectedTotal=17;
  state.timing.record(30);state.targetTimed=true;state.targetChangedAt=42;
  state.positionPid.integral=5;state.started=true;state.referenceVelocity=7;
  state.velocityProfile.valid=true;state.velocityProfile.elapsed=.2;
  state.positionProfile.valid=true;state.positionProfile.elapsed=.3;
  state.cancel(true);
  assert(state.targetTimed && state.targetChangedAt==42 && !state.started && !state.referenceVelocity);
  assert(!state.positionProfile.valid && state.positionProfile.elapsed==0);
  assert(state.positionPid.integral==0 && state.feedback.count==2 && state.feedback.samples[1].position==11);
  assert(!state.velocityProfile.valid && state.velocityProfile.elapsed==0);
  assert(state.feedback.staticCoastFactor[1]==.3f && state.feedback.rejectedTotal==17);
  assert(state.timing.steps==1 && state.timing.maxIntervalMs==30 && state.timing.lateSteps==1);
  state.cancel();assert(!state.targetTimed && state.targetChangedAt==0 && state.feedback.count==2);
  deny=false;
  std::puts("PASS: relay/HUD allocate no planner; OOM blocks all motion starts; one shared allocation; allocation-free ticks; STOP preserves feedback/timing");
}
