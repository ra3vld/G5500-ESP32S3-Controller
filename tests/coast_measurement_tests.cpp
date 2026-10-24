#include "control.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace forkrot;
int main(){
  Controller c;auto hw=c.hardware;hw.simulation=false;hw.drive=DRIVE_YAESU;c.configureHardware(hw);
  c.feedback(90,45,true,true);uint32_t now=1000;float position=90;
  auto sample=[&](float p){now+=100;position=p;c.feedbackAt(0,p,true,now);c.tickCoast(now);};
  for(unsigned i=0;i<6;++i)sample(position);
  c.setupRequired=true;assert(c.startCoast(0,1,10,now)==Result::Unavailable);c.setupAccess=true;
  assert(c.startCoast(0,1,10,now)==Result::Ok);
  for(unsigned i=0;i<20;++i)sample(position+.5f);
  assert(c.coast.phase==CoastMeasurement::Coasting);
  sample(position+.8f);sample(position+.5f);sample(position+.2f);
  for(unsigned i=0;i<12;++i)sample(position);
  assert(c.coast.phase==CoastMeasurement::Complete && c.coast.measured[0]==2);
  assert(std::abs(c.coast.degrees[0][1]-1.5f)<.01f && c.coast.seconds[0][1]>0);
  assert(c.applyCoast(0,now)==Result::Unavailable);
  assert(c.startCoast(0,-1,10,now)==Result::Ok);
  for(unsigned i=0;i<20;++i)sample(position-.5f);
  sample(position-.6f);sample(position-.3f);
  for(unsigned i=0;i<12;++i)sample(position);
  assert(c.coast.phase==CoastMeasurement::Complete&&c.coast.measured[0]==3);
  assert(c.applyCoast(0,now)==Result::Ok && std::abs(c.speeds[0].coastDegrees-1.5f)<.01f);
  assert(c.startCoast(0,1,10,now)==Result::Ok);c.cancelCoast(now);
  assert(c.coast.phase==CoastMeasurement::Cancelled && !c.moving());
  assert(c.applyCoast(0,now)==Result::Unavailable);
  assert(std::abs(c.speeds[0].coastDegrees-1.5f)<.01f);
  assert(c.startCoast(0,1,10,now)==Result::Ok);now+=31000;c.tickCoast(now);
  assert(c.coast.phase==CoastMeasurement::Failed && !c.moving());
  c.axes[0].current=449;assert(c.startCoast(0,1,10,now)!=Result::Ok);
  std::cout<<"PASS: fixed-speed coast both directions, time/distance, shared maximum, incomplete result, cancellation, stale timeout and range reserve\n";
}
