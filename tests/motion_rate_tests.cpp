#include "control.hpp"
#include "position_filter.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace forkrot;
int main() {
  const unsigned period=MotionControlPeriodMs;
  for(unsigned hz:{50u,100u,200u}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    assert(c.setControlFrequency(hz,0)==Result::Ok);
    PositionFilter sensor;const unsigned p=1000/hz;
    for(unsigned t=0;t<=1000;++t) {
      if(sensor.due(t,p)) {
        sensor.sample(100,t,p);c.feedbackAt(0,90,true,t);c.feedbackAt(1,90,true,t);
      }
      if(!t)assert(c.jog(0,1,false,0)==Result::Ok);
      c.tick(t);
    }
    assert(sensor.conversions()==hz+1);
    assert(c.motionTiming(0).steps==hz+1);
    assert(c.setControlFrequency(hz==50?200:50,1001)==Result::Ok);
    assert(!c.moving() && !c.axes[0].speedOutputPercent && !c.axes[1].holdArmed);
    assert(c.setControlFrequency(201,1002)==Result::Invalid);
  }
  // Same full physical window at every rate, including clock rollover.
  for(unsigned window:{40u,80u,200u,600u}) {
    MotionFeedback f;
    const uint32_t epoch=0xffffff00u;
    for(unsigned t=0;t<=1200;t+=period) f.sample(20+.5f*t/1000,true,epoch+t,window);
    assert(std::abs(f.velocity-.5f)<.001f);
    assert(f.progressed(1,.5f*window/1000-.001f,window));
    const auto count=f.count;const auto at=f.at;
    f.sample(999,true,at,window);assert(f.count==count && f.at==at);
  }
  PositionFilter filter;filter.sample(100,0xfffffffcu);
  assert(!filter.due(uint32_t(0xfffffffcu+period-1)));
  assert(filter.due(uint32_t(0xfffffffcu+period)));
  {
    // A 1 ms scheduling error must not turn 200 Hz into a drifting 166 Hz.
    PositionFilter jitter;Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    c.feedbackAt(0,90,true,0);c.feedbackAt(1,90,true,0);
    assert(c.jog(0,1,false,0)==Result::Ok);c.tick(0);jitter.sample(100,0);
    for(unsigned stamp=period;stamp<=1000;stamp+=period) {
      const unsigned actual=stamp+((stamp/period)%2);
      assert(jitter.due(actual));jitter.sample(100,actual);
      c.feedbackAt(0,90,true,actual);c.tick(actual);c.tick(actual);
    }
    assert(c.motionTiming(0).steps==1+1000/period);
    assert(jitter.conversions()==1+1000/period);
    assert(c.motionTiming(0).maxIntervalMs==period+1);
  }
  // Integration uses actual elapsed time; a faster loop cannot multiply Ki.
  for(unsigned p:{5u,10u,20u}) {
    MotionPid pid;
    for(unsigned t=0;t<1000;t+=p) pid.step(1,0,0,0,1,0,p/1000.f,.1f,-10,10);
    assert(std::abs(pid.integral-1)<.0001f);
  }
  for(unsigned axis:{0u,1u}) for(int direction:{-1,1}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    auto &m=c.motionSettings[axis];m.mode=RegulatorMode::Cascade;
    m.velocity=2;m.acceleration=2;m.deceleration=2;m.jerk=8;
    m.kickEnabled=false;m.sustainingPercent=0;m.feedbackTimeoutMs=500;
    c.feedbackAt(0,90,true,0);c.feedbackAt(1,90,true,0);
    assert(c.jog(axis,direction,false,0)==Result::Ok);
    c.tick(0);assert(c.motionTiming(axis).steps==1);
    for(unsigned t=1;t<=1000;++t) {
      if(t%period==0)c.feedbackAt(axis,90+direction*.001f*t,true,t);
      c.tick(t);
      assert(c.axes[axis].motionError==MotionError::None);
    }
    assert(c.motionTiming(axis).steps==1+1000/period);
    assert(c.motionTiming(axis).maxIntervalMs==period);
    assert(!c.motionTiming(axis).lateSteps);
    c.tick(1000+3*period);assert(c.motionTiming(axis).lateSteps==1);
    // A fresh control tick must never make stale feedback fresh.
    c.tick(1600);assert(c.axes[axis].motionError==MotionError::FeedbackStale);
    assert(!c.axes[axis].direction && !c.axes[axis].speedOutputPercent);
    assert(c.motionTiming(axis).lateSteps==1); // counters survive STOP/fault
  }
  // An overspeed kick must discard pulse torque but retain running friction
  // compensation. Rebuilding I from zero at 3 deg/s used to take >5 seconds.
  for(int direction:{-1,1}) {
    Controller c;c.hardware.simulation=false;c.hardware.drive=DRIVE_BTS7960;
    auto &m=c.motionSettings[0];m.mode=RegulatorMode::Cascade;m.velocity=3;
    m.kickEnabled=true;m.kickPercent=34;m.movementDegrees=1;m.sustainingPercent=16;
    m.velocityKp=m.velocityKd=m.feedforward=0;m.velocityKi=1;m.kickReleaseMs=20;
    c.feedbackAt(0,90,true,0);c.feedbackAt(1,90,true,0);
    assert(c.jog(0,direction,false,0)==Result::Ok);c.tick(0);
    for(unsigned stamp=period;stamp<=1200;stamp+=period) {
      c.feedbackAt(0,90+direction*1.2f,true,stamp);c.tick(stamp);
      assert(c.axes[0].motionError==MotionError::None);
    }
    assert(c.axes[0].speedOutputPercent>16.2f && c.axes[0].speedOutputPercent<20);
    c.stopNow(-1,1200);assert(!c.axes[0].speedOutputPercent);
  }
  std::cout << "PASS rate=" << 1000/period << " Hz, complete velocity windows, timestamp rollover, actual-dt PI, gating, stale STOP, slow kick friction handoff\n";
}
