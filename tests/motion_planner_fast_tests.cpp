#include "control.hpp"
#include "src/ruckig/ruckig.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
namespace forkrot {
unsigned motionPlannerCalculations();
bool testMotionProfileStep(bool,float &,float &,float &,float,float,float,float,float,float);
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
  controller.feedbackAt(0,50,true,0);controller.feedbackAt(1,90,true,0);
  controller.setMode(Mode::Auto,0);
  assert(controller.jog(0,1,false,0)==Result::Ok);
  const auto step=[&](unsigned at) {
    controller.feedbackAt(0,50+float(at)*.004f,true,at);
    controller.feedbackAt(1,90,true,at);controller.tick(at);
    assert(controller.axes[0].motionError==MotionError::None && controller.axes[0].direction==1);
  };
  for(unsigned at=0;at<=2000;at+=5)step(at);
  assert(controller.axes[0].speedOutputPercent==controller.runtimeSpeed(0));
  const auto settled=motionPlannerCalculations();
  for(unsigned at=2005;at<=3000;at+=5)step(at);
  assert(motionPlannerCalculations()==settled); // Real control ticks reach the fast path after a ramp.
  controller.stopNow(-1,3001);assert(!controller.axes[0].direction);
  std::cout<<"PASS: exact Ruckig parity, invalid limits, jerk tails, speed changes; 1000 steady ticks without replanning\n";
}
