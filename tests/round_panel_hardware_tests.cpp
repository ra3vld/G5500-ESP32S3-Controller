#include "motion_fixture.hpp"
#include "hardware.hpp"
#include "setup_state.hpp"
#include "panel_input.hpp"
#include <cassert>
#include <cstdio>
using namespace forkrot;
static void idleDefaults() {
  MotionTestRuntime r;r.setup.required=false;r.hud.config.role=DeviceRole::Controller;
  Hardware h(r);fakeHardware={};fakeI2c[0]=fakeI2c[1]={};
  h.beginDirections();h.beginAdc();h.beginInputs();
  r.controller.jog(0,-1,false,millis());r.controller.jog(1,1,false,millis());h.controlRotator();h.applyOutputs();h.allStop();
  for(const auto &event:fakeHardware.events)
    assert(event.operation=="mode" && event.pin==9 && event.value==INPUT_PULLUP);
  assert(fakeHardware.readsRaw==0 && fakeHardware.readsMv==0 && !r.controller.moving());
  assert(fakeI2c[0].begins==0 && fakeI2c[1].begins==0);
}
static void localRelays() {
  MotionTestRuntime r;r.setup.required=false;r.hud.config.role=DeviceRole::Controller;
  HardwareConfig c;c.pins.uartRx=c.pins.uartTx=-1;c.pins.azNegative=20;c.pins.azPositive=21;
  assert(!board::validateHardware(c) && c.manualWithoutFeedback());
  auto conflict=c;conflict.pins.uartRx=20;conflict.pins.uartTx=21;
  assert(board::validateHardwareDetailed(conflict).error);
  conflict=c;conflict.pins.azPositive=-1;assert(board::validateHardware(conflict));
  r.controller.configureHardware(c);r.controller.speedEnabled=true;
  assert(!r.controller.variableSpeed(0)); // no speed wire means fixed relay outputs
  Hardware h(r);fakeHardware={};fakeI2c[0]=fakeI2c[1]={};
  h.beginDirections();h.beginAdc();h.beginInputs();
  assert(!r.controller.driveFault && !fakeHardware.attachCalls);
  auto tick=[&](uint32_t at){fakeHardware.now=at;h.controlRotator();};
  assert(r.controller.manualDrive(0,2,1,1,1,-1,0,false,10,100)==Result::Ok);
  tick(10);assert(fakeHardware.levels[20]==HIGH && fakeHardware.levels[21]==LOW);
  assert(r.controller.manualDrive(0,2,1,2,1,0,0,true,20,100)==Result::Ok);
  tick(20);assert(fakeHardware.levels[20]==LOW && fakeHardware.levels[21]==LOW);
  assert(r.controller.manualDrive(0,2,2,3,1,1,0,false,30,100)==Result::Ok);
  tick(30);assert(fakeHardware.levels[20]==LOW && fakeHardware.levels[21]==LOW); // reversal delay
  assert(r.controller.manualDrive(0,2,2,4,1,1,0,false,330,100)==Result::Ok);
  tick(330);assert(fakeHardware.levels[20]==LOW && fakeHardware.levels[21]==HIGH);
  tick(1340);assert(fakeHardware.levels[20]==LOW && fakeHardware.levels[21]==LOW); // local one-second lease
  assert(r.controller.manualDrive(0,2,2,5,1,1,0,false,1450,100)==Result::Cancelled);
  assert(r.controller.target({90,0,true,false},false,1450)==Result::NotCalibrated);
  assert(r.controller.manualDrive(0,2,3,6,1,1,0,false,1460,100)==Result::Ok);
  tick(1460);assert(fakeHardware.levels[21]==HIGH);
  panel::TouchGesture touch;
  panel::TouchLayout layout;layout.stop={80,167,80,28};
  const auto event=touch.sample(true,120,180,layout,1470);
  assert(event.action==panel::TouchAction::Stop);
  r.controller.stopNow(-1,1470);tick(1470);
  assert(touch.sample(false,0,0,layout,1471).action==panel::TouchAction::None);
  tick(1480);h.allStop();
  assert(fakeHardware.levels[20]==LOW && fakeHardware.levels[21]==LOW);
  assert(fakeHardware.readsRaw==0 && fakeHardware.readsMv==0 && !fakeHardware.attachCalls);
  bool left=false,right=false;
  for(const auto &event:fakeHardware.events) {
    assert(event.pin==9 || event.pin==20 || event.pin==21);
    if(event.operation=="write" || event.operation=="write_unowned")assert(event.pin!=9);
    if(event.operation=="write" || event.operation=="write_unowned") {
      (event.pin==20?left:right)=event.value!=0;
      assert(!left || !right);
    }
  }
  SetupState state;state.step=SetupStep::Inputs;
  assert(state.next(false,c.simulation||c.manualWithoutFeedback())==SetupStep::Programs);
}
static void sharedTouchBus() {
  HardwareConfig c;c.pins.uartRx=c.pins.uartTx=-1;c.pins.azNegative=20;c.pins.azPositive=21;
  c.sensors[0]=1;c.pins.azSensorSda=4;c.pins.azSensorScl=5;
  assert(!board::validateHardware(c));
  fakeI2c[0]=fakeI2c[1]={};Wire.begin(4,5,400000);
  auto &device=fakeI2c[0].devices[6];device[3]=4096>>6;device[4]=(4096&63)<<2;
  PositionSensors sensors;sensors.begin(c);
  assert(sensors.read(c,0)==4096);
  assert(fakeI2c[0].begins==1 && fakeI2c[0].sda==4 && fakeI2c[0].scl==5 && fakeI2c[1].begins==0);
  auto bad=c;bad.pins.azSensorSda=8;bad.pins.azSensorScl=9;
  assert(board::validateHardware(bad));
  bad=c;bad.pins.led=4;assert(board::validateHardware(bad)); // bus sharing never frees GPIO
}
int main() {
  idleDefaults();localRelays();sharedTouchBus();
  std::puts("PASS: configurable C3 relays, UART conflicts, no feedback/PWM, release/STOP/lease/reversal and shared encoder/touch I2C");
}
