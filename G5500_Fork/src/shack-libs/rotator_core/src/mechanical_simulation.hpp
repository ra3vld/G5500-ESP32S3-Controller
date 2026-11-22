// Small deterministic motor/load model. No GPIO, heap or random noise. GPL-3.0.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace forkrot {
struct MechanicalSimulation {
  float velocity = 0, slack = 0;
  int8_t engagedDirection = 0;
  float step(float position, float output, float dt, float lower, float upper) {
    if(dt<=0) return position;
    const float magnitude=std::abs(output);
    // Static friction exceeds running friction; output is signed PWM percent.
    const bool stuck=std::abs(velocity)<.02f && magnitude<8;
    const float target=stuck?0:std::copysign(13.4f*std::max(0.f,magnitude-4)/96,output);
    const float previous=velocity;
    const float tau=magnitude>0?.25f:.35f;
    velocity+=(target-velocity)*(dt/(tau+dt));
    if(!magnitude || stuck) {
      velocity=std::copysign(std::max(0.f,std::abs(velocity)-2*dt),velocity);
    }
    float travel=(previous+velocity)*.5f*dt;
    const int direction=(travel>0)-(travel<0);
    if(direction && engagedDirection && direction!=engagedDirection) slack=.35f;
    if(direction) engagedDirection=int8_t(direction);
    const float lost=std::min(std::abs(travel),slack);
    slack-=lost;
    travel-=std::copysign(lost,travel);
    const float next=std::clamp(position+travel,lower,upper);
    if((next<=lower && velocity<0) || (next>=upper && velocity>0)) velocity=0;
    return next;
  }
};
static_assert(sizeof(MechanicalSimulation)==12);
}
