#include "control.hpp"
#include "src/ruckig/ruckig.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
namespace forkrot {
unsigned motionPlannerCalculations();
bool testMotionProfileStep(bool,float &,float &,float &,float,float,float,float,float,float);
bool testCachedMotionProfileStep(MotionVelocityProfile &,float &,float &,float &,float,float,float,float,float,float);
bool testCachedPositionProfileStep(MotionPositionProfile &,float &,float &,float &,float,float,float,float,float,float);
}
using namespace forkrot;
static void compare(float p,float v,float a,float target,float limit,float up,float down,float jerk,float dt,bool steady) {
  ruckig::Ruckig<1> solver;ruckig::InputParameter<1> input;ruckig::Trajectory<1> trajectory;
  input.control_interface=ruckig::ControlInterface::Velocity;
  input.current_position={p};input.current_velocity={v};input.current_acceleration={a};
  input.target_velocity={target};input.target_acceleration={0};input.max_velocity={limit};
  input.max_acceleration={v<0?down:up};input.min_acceleration=std::array<double,1>{-(v<0?up:down)};input.max_jerk={jerk};
  const bool reference=solver.calculate(input,trajectory)>=0;
  std::array<double,1> rp{},rv{},ra{};
  if(reference)trajectory.at_time(std::min(double(dt),trajectory.get_duration()),rp,rv,ra);
  const auto before=motionPlannerCalculations();
  assert(testMotionProfileStep(false,p,v,a,target,limit,up,down,jerk,dt)==reference);
  assert(motionPlannerCalculations()==before+unsigned(!steady));
  if(reference) {
    assert(p==float(rp[0]) && v==float(rv[0]) && a==float(ra[0]));
  }
}
int main() {
  float p=1,v=20,a=0;
  assert(!testMotionProfileStep(false,p,v,a,20,100,50,50,100,.005f));
  assert(prepareMotionPlanner());
  unsigned positionSteps=0,positionSolves=0;
  for(float start:{-2.f,0.f,2.f})for(float target:{-12.f,12.f})
    for(float initialAcceleration:{-.25f,0.f,.25f})for(float dt:{.005f,.01f,.02f}) {
      MotionPositionProfile cache;float p=0,v=start,a=initialAcceleration;
      ruckig::Ruckig<1> solver;ruckig::InputParameter<1> input;ruckig::Trajectory<1> trajectory;
      input.current_position={p};input.current_velocity={v};input.current_acceleration={a};
      input.target_position={target};input.max_velocity={4};input.max_jerk={8};
      input.max_acceleration={v<0?3.:2.};input.min_acceleration=std::array<double,1>{v<0?-2.:-3.};
      assert(solver.calculate(input,trajectory)>=0);
      double elapsed=0;const auto before=motionPlannerCalculations();
      for(unsigned step=0;step<1600;++step) {
        const float interval=step%2?dt*1.1f:dt*.9f;const bool negative=v<0;
        assert(testCachedPositionProfileStep(cache,p,v,a,target,4,2,3,8,interval));
        ++positionSteps;elapsed=std::min(elapsed+interval,trajectory.get_duration());
        std::array<double,1> rp{},rv{},ra{};trajectory.at_time(elapsed,rp,rv,ra);
        assert(std::abs(p-rp[0])<.00002&&std::abs(v-rv[0])<.00002&&std::abs(a-ra[0])<.00002);
        if((v<0)!=negative || !cache.valid)break;
      }
      positionSolves+=motionPlannerCalculations()-before;
      auto original=cache;const float bp=p,bv=v,ba=a;
      for(unsigned change=0;change<8;++change) {
        cache=original;p=bp;v=bv;a=ba;float goal=target,limit=4,up=2,down=3,jerk=8;
        if(change==0)goal+=1;
        if(change==1)limit+=1;
        if(change==2)up+=1;
        if(change==3)down+=1;
        if(change==4)jerk+=1;
        if(change==5)p+=.001f;
        if(change==6)v+=.001f;
        if(change==7)a+=.001f;
        const auto count=motionPlannerCalculations();
        assert(testCachedPositionProfileStep(cache,p,v,a,goal,limit,up,down,jerk,dt));
        assert(motionPlannerCalculations()==count+1);
      }
    }
  assert(positionSolves<positionSteps/10);
  std::cout<<"Continued position paths: "<<positionSolves<<" solves / "<<positionSteps<<" steps; cache bytes="<<sizeof(MotionPositionProfile)<<" per axis\n";
  // Compare continued paths to Ruckig's original trajectory at actual elapsed
  // times, including jitter, nonzero acceleration and virtual-position resets.
  unsigned cachedSteps=0,solves=0;
  for(float start:{-2.f,0.f,2.f})for(float target:{-3.f,.5f,3.f})
    for(float acceleration:{-.25f,0.f,.25f})for(float dt:{.005f,.01f,.02f}) {
      MotionVelocityProfile cache;
      float p=10,v=start,a=acceleration;
      ruckig::Ruckig<1> solver;ruckig::InputParameter<1> input;ruckig::Trajectory<1> trajectory;
      input.control_interface=ruckig::ControlInterface::Velocity;
      input.current_position={0};input.current_velocity={v};input.current_acceleration={a};
      input.target_velocity={target};input.max_velocity={4};
      input.max_acceleration={v<0?3.:2.};input.min_acceleration=std::array<double,1>{v<0?-2.:-3.};input.max_jerk={8};
      assert(solver.calculate(input,trajectory)>=0);
      double elapsed=0,position=0;
      const auto before=motionPlannerCalculations();
      // Stay on the same velocity side: crossing zero changes asymmetric
      // acceleration bounds and deliberately requires a fresh solution.
      for(unsigned step=0;step<20;++step) {
        const float interval=step%2?dt*1.1f:dt*.9f;
        const bool negative=v<0;
        p=0;
        assert(testCachedMotionProfileStep(cache,p,v,a,target,4,2,3,8,interval));
        ++cachedSteps;elapsed=std::min(elapsed+interval,trajectory.get_duration());
        std::array<double,1> rp{},rv{},ra{};trajectory.at_time(elapsed,rp,rv,ra);
        assert(std::abs(v-rv[0])<.00002 && std::abs(a-ra[0])<.00002);
        assert(std::abs(p-(rp[0]-position))<.00002);position=rp[0];
        if((v<0)!=negative)break;
      }
      solves+=motionPlannerCalculations()-before;
      // Any state/intent/constraint change must invalidate the cached path.
      auto original=cache;float bp=p,bv=v,ba=a;
      for(unsigned change=0;change<7;++change) {
        cache=original;p=bp;v=bv;a=ba;
        float goal=target,limit=4,up=2,down=3,jerk=8;
        if(change==0)goal+=.01f;
        if(change==1)limit+=1;
        if(change==2)up+=1;
        if(change==3)down+=1;
        if(change==4)jerk+=1;
        if(change==5)v+=.001f;
        if(change==6)a+=.001f;
        const auto count=motionPlannerCalculations();
        assert(testCachedMotionProfileStep(cache,p,v,a,goal,limit,up,down,jerk,dt));
        assert(motionPlannerCalculations()==count+1);
      }
    }
  assert(solves<cachedSteps/4);
  std::cout<<"Continued velocity paths: "<<solves<<" solves / "<<cachedSteps<<" steps\n";
  // Rounded terminal acceleration must settle to exact zero, so an exhausted
  // cached path never adds work forever at constant commanded speed.
  for(float target:{-2.5f,-1.5f,.5f,1.5f,2.5f}) {
    MotionVelocityProfile cache;float position=0,velocity=0,acceleration=0;
    for(unsigned n=0;n<400;++n) {
      position=0;
      assert(testCachedMotionProfileStep(cache,position,velocity,acceleration,target,3,2,2,8,n%2?.004f:.006f));
    }
    assert(velocity==target && acceleration==0);
    const auto before=motionPlannerCalculations();
    for(unsigned n=0;n<400;++n)
      assert(testCachedMotionProfileStep(cache,position,velocity,acceleration,target,3,2,2,8,.005f));
    assert(motionPlannerCalculations()==before);
  }
  for(float speed:{-100.f,-1.f,0.f,1.f,100.f})
    for(float dt:{.005f,.01f,.02f})
      for(float accel:{.1f,50.f})compare(10,speed,0,speed,100,accel,2*accel,100,dt,true);
  // Exact equality only: small remaining acceleration and changed speed still
  // use the full planner, as do invalid or degenerate limits.
  for(float accel:{-.001f,.001f})compare(10,20,accel,20,100,50,50,100,.005f,false);
  compare(10,20,0,std::nextafter(20.f,21.f),100,50,50,100,.005f,false);
  compare(10,20,0,0,100,50,50,100,.005f,false);
  compare(10,20,0,20,100,0,0,0,.005f,false);
  compare(10,20,0,20,100,-1,50,100,.005f,false);
  compare(10,20,0,20,100,50,50,-1,.005f,false);
  const auto before=motionPlannerCalculations();
  for(unsigned tick=0;tick<1000;++tick) {
    p=1;v=20;a=0;
    assert(testMotionProfileStep(false,p,v,a,20,100,50,50,100,.005f));
    assert(p==1 && v==20 && a==0);
  }
  assert(motionPlannerCalculations()==before);
  p=1;v=0;a=0;
  assert(testMotionProfileStep(true,p,v,a,1,100,50,50,100,.005f));
  assert(motionPlannerCalculations()==before+1); // Position control stays unchanged.
  Controller controller;
  controller.hardware.simulation=false;controller.hardware.drive=DRIVE_BTS7960;
  controller.motionSettings[0].movementDegrees=.02f;
  assert(controller.setControlFrequency(200,0)==Result::Ok);
  assert(controller.loopDelayMs(0)==1);
  controller.feedbackAt(0,50,true,0);controller.feedbackAt(1,90,true,0);
  controller.setMode(Mode::Auto,0);
  assert(controller.jog(0,1,false,0)==Result::Ok);
  const auto step=[&](unsigned at) {
    controller.feedbackAt(0,50+float(at)*.004f,true,at);
    controller.feedbackAt(1,90,true,at);controller.tick(at);
    assert(controller.axes[0].motionError==MotionError::None && controller.axes[0].direction==1);
  };
  for(unsigned at=0;at<=2000;at+=5)step(at);
  assert(controller.loopDelayMs(2002)==1&&controller.loopDelayMs(2004)==0);
  assert(controller.axes[0].speedOutputPercent==controller.runtimeSpeed(0));
  const auto settled=motionPlannerCalculations();
  for(unsigned at=2005;at<=3000;at+=5)step(at);
  assert(motionPlannerCalculations()==settled); // Real control ticks reach the fast path after a ramp.
  controller.stopNow(-1,3001);assert(!controller.axes[0].direction);
  assert(controller.loopDelayMs(3004)==1);
  // Controller copies are used by settings/preview validation. A mid-ramp
  // copy must continue both axes independently of the shared solver scratch.
  for(bool positioning:{false,true}) {
  Controller original;auto config=original.hardware;config.simulation=SimulationMechanics;
  original.configureHardware(config);original.feedback(100,90,true,true);
  for(unsigned axis=0;axis<2;++axis) {
    original.motionSettings[axis].mode=RegulatorMode::Cascade;
    assert(original.setRuntimeSpeed(axis,1.5f,0)==Result::Ok);
    if(!positioning)assert(original.jog(axis,1,false,0)==Result::Ok);
  }
  if(positioning) {
    original.setMode(Mode::Auto,0);
    assert(original.target({150,120,true,true},false,0)==Result::Ok);
  }
  for(unsigned at=0;at<=100;at+=5)original.tick(at);
  Controller copied=original;
  for(unsigned at=105;at<=1500;at+=5) {
    original.tick(at);copied.tick(at);
    for(unsigned axis=0;axis<2;++axis) {
      assert(original.axes[axis].current==copied.axes[axis].current);
      assert(original.axes[axis].speedOutputPercent==copied.axes[axis].speedOutputPercent);
    }
  }
  original.stopNow(-1,1501);copied.tick(1520);
  assert(!original.moving() && copied.moving());
  }
  std::cout<<"PASS: exact Ruckig parity, invalid limits, jerk tails, speed changes; 1000 steady ticks without replanning\n";
}
