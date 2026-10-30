#include "motion_fixture.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include "hardware_form.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
namespace forkrot::crowpanel { bool beginBus(){return true;} }
using namespace forkrot;
int main() {
  MechanicalSimulation plant;
  float position=100;
  for(unsigned i=0;i<100;++i)position=plant.step(position,5,.01f,0,450);
  assert(position==100 && plant.velocity==0); // Below breakaway torque.
  position=plant.step(position,50,.01f,0,450);
  assert(plant.velocity>0 && plant.velocity<1);
  for(unsigned i=0;i<300;++i)position=plant.step(position,50,.01f,0,450);
  const float running=position;
  position=plant.step(position,0,.01f,0,450);
  assert(position>running && plant.velocity>0); // STOP removes drive, inertia remains.
  for(unsigned i=0;i<300;++i)position=plant.step(position,0,.01f,0,450);
  assert(plant.velocity==0);
  unsigned backlashSamples=0;
  for(unsigned i=0;i<200;++i) {
    const float before=position;position=plant.step(position,-50,.01f,0,450);
    if(plant.velocity<0 && position==before)++backlashSamples;
  }
  assert(backlashSamples>3 && position<running);
  for(unsigned i=0;i<1000;++i)position=plant.step(position,-100,.02f,99,101);
  assert(position==99 && plant.velocity==0);

  Preferences::database.clear();
  MotionTestRuntime runtime;
  auto config=runtime.controller.hardware;config.simulation=SimulationMechanics;config.bluetooth=false;
  runtime.controller.configureHardware(config);commissionTestMotion(runtime.controller);
  Hardware hw(runtime);fakeHardware={};hw.beginDirections();hw.beginAdc();hw.applyOutputs();
  assert(fakeHardware.events.empty() && !fakeHardware.readsRaw && !fakeHardware.readsMv);
  auto &c=runtime.controller;c.feedback(100,90,true,true);c.setMode(Mode::Auto,0);
  assert(c.jog(0,1,false,0)==Result::Ok);
  for(unsigned at=0;at<=3000;at+=10)c.tick(at);
  assert(c.axes[0].current>100 && c.axes[1].current==90);
  c.stopNow(-1,3000);const float stopped=c.axes[0].current;c.tick(3010);
  assert(c.axes[0].direction==0 && c.axes[0].speedOutputPercent==0 && c.axes[0].current>stopped);
  Controller copy=c;
  for(unsigned at=3020;at<=6000;at+=10){c.tick(at);copy.tick(at);}
  assert(c.axes[0].current==copy.axes[0].current);
  const float resting=c.axes[0].current;c.tick(6500);assert(c.axes[0].current==resting);
  assert(stateJson(c,runtime.protocol,runtime.loggerSettings,6500).find("SIMULATED_MECHANICS")!=std::string::npos);
  for(unsigned drive=0;drive<=DRIVE_DIR_PWM;++drive)for(auto model:{Model::G5500,Model::G800DXA,Model::G800SA}) {
    Controller speed;auto hw=speed.hardware;hw.simulation=SimulationMechanics;hw.drive=drive;
    speed.configureHardware(hw);speed.setModel(model);
    assert(speed.setRuntimeSpeed(0,25,0)==Result::Ok && speed.runtimeSpeed(0)==25);
    assert(speed.runtimeSpeeds.values[RuntimeSpeeds::Count-1]==0);
  }

  HardwareForm form;
  assert(parseHardwareForm("token=1&generation=1&simulation=2",form,true,config));
  assert(form.config.simulation==2 && form.config.pins.azAdc==-1);
  assert(!parseHardwareForm("token=1&generation=1&simulation=3",form,true,config));
  HardwareRecord source,decoded;source.hasConfirmed=true;source.good=source.candidate=form.config;
  assert(decodeHardware(encodeHardware(source),decoded) && decoded.good==source.good);
  assert(!decodeHardware(encodeHardwareRecord<HardwareBytes{}.size()>(source,9),decoded));
  source.good.simulation=source.candidate.simulation=SimulationSimple;
  assert(decodeHardware(encodeHardwareRecord<HardwareBytes{}.size()>(source,9),decoded) && decoded.good.simulation==1);
  assert(decodeHardware(encodeHardwareRecord<40>(source),decoded) && decoded.good.simulation==1);
  source.good.simulation=source.candidate.simulation=SimulationMechanics;
  auto bytes=encodeHardware(source);
  Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
  Storage storage;MotionTestRuntime restored;storage.loadHardware(restored);
  assert(!restored.recovery && restored.controller.hardware.simulation==2);
  runtime.controller.motionSettings[0].positionKp=3;
  assert(storage.saveForkSettings(runtime));
  MotionTestRuntime tuned;tuned.controller.configureHardware(config);storage.loadMotionSettings(tuned);
  assert(tuned.controller.motionSettings[0].positionKp==3);
  config.simulation=SimulationSimple;tuned.controller.configureHardware(config);storage.loadMotionSettings(tuned);
  assert(tuned.controller.motionSettings[0].positionKp==MotionSettings{}.positionKp);
  for(int drive=0;drive<=3;++drive)for(unsigned mode=0;mode<=2;++mode) {
    SetupState s,d;s.drive=int8_t(drive);s.simulation=mode;
    assert(decodeSetup(encodeSetup(s),d) && d.drive==drive && d.simulation==mode);
  }
  std::cout<<"PASS: inertia, friction, coasting STOP, reverse backlash, limits, controller copy, GPIO isolation and NVS migration\n";
}
