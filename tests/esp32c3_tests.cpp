#include "motion_fixture.hpp"
#include "hardware.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace forkrot;
static void defaultsAndPins() {
  static_assert(SELECTED_BOARD==BOARD_ESP32_C3 && FORK_C3 && DISPLAY_KIND==DISPLAY_NONE);
  assert(!std::strcmp(board::NAME,"ESP32-C3"));
  assert(HudConfig{}.role==DeviceRole::Controller);
  HardwareConfig c;assert(!c.simulation && !c.bluetooth && !board::validateHardware(c));
  assert(c.buttonGpio==9 && c.pins.uartRx==20 && c.pins.uartTx==21);
  assert(!c.localAxisConfigured(0) && !c.localAxisConfigured(1));
  // No built-in display/touch owner on the generic module.
  for(int pin=0;pin<=10;++pin) {
    assert(board::gpio(pin) && board::output(pin) && !board::fixedPinOwner(pin));
    auto next=c;next.buttonGpio=-1;next.pins.led=pin;assert(!board::validateHardware(next));
  }
  for(int pin=0;pin<=4;++pin) assert(board::adc(pin));
  assert(!board::adc(5));
  for(int pin=11;pin<=19;++pin) {
    assert(!board::gpio(pin));auto next=c;next.pins.led=pin;assert(board::validateHardware(next));
  }
  auto next=c;next.pins.led=20;assert(board::validateHardware(next));
  next.pins.uartRx=next.pins.uartTx=-1;assert(!board::validateHardware(next));
  next=c;next.bluetooth=true;next.bluetoothMode=BT_HC05;
  next.pins.hc05Rx=2;next.pins.hc05Tx=3;next.pins.hc05State=8;assert(!board::validateHardware(next));
  next.bluetoothMode=BT_ESP32_SPP;assert(board::validateHardware(next));
  MotionTestRuntime r;r.setup.required=false;Hardware h(r);fakeHardware={};
  r.controller.configureHardware(c);assert(r.controller.model==Model::G5500);
  r.controller.setModel(Model::G800DXA);r.controller.setModel(Model::G5500);
  assert(r.controller.model==Model::G5500);
  h.beginDirections();h.beginAdc();h.beginInputs();h.controlRotator();
  for(const auto &event:fakeHardware.events)
    assert(event.operation=="mode" && event.pin==9 && event.value==INPUT_PULLUP);
  assert(!fakeHardware.readsRaw && !fakeHardware.readsMv && !r.controller.moving());
}
static void relaysOnFreedPins() {
  HardwareConfig c;c.pins.azNegative=6;c.pins.azPositive=7;
  assert(!board::validateHardware(c) && c.manualWithoutFeedback());
  MotionTestRuntime r;r.setup.required=false;r.controller.configureHardware(c);
  Hardware h(r);fakeHardware={};h.beginDirections();h.beginAdc();
  auto tick=[&](uint32_t now){fakeHardware.now=now;h.controlRotator();};
  assert(r.controller.manualDrive(0,2,1,1,1,-1,0,false,10,100)==Result::Ok);
  tick(10);assert(fakeHardware.levels[6]==HIGH && fakeHardware.levels[7]==LOW);
  assert(r.controller.manualDrive(0,2,1,2,1,0,0,true,20,100)==Result::Ok);
  tick(20);assert(fakeHardware.levels[6]==LOW && fakeHardware.levels[7]==LOW);
  assert(r.controller.manualDrive(0,2,2,3,1,1,0,false,30,100)==Result::Ok);
  tick(30);assert(fakeHardware.levels[6]==LOW && fakeHardware.levels[7]==LOW);
  assert(r.controller.manualDrive(0,2,2,4,1,1,0,false,330,100)==Result::Ok);
  tick(330);assert(fakeHardware.levels[6]==LOW && fakeHardware.levels[7]==HIGH);
  r.controller.stopNow(-1,340);tick(340);
  assert(fakeHardware.levels[6]==LOW && fakeHardware.levels[7]==LOW);
  assert(!fakeHardware.readsRaw && !fakeHardware.readsMv);
  for(auto drive:{DRIVE_SEPARATE_PWM,DRIVE_BTS7960}) {
    auto next=c;next.drive=drive;next.pins.azSpeedEnable=8;
    assert(!board::validateHardware(next));
  }
}
static void oneSensorBus() {
  HardwareConfig c;c.pins.azNegative=6;c.pins.azPositive=7;c.pins.elNegative=0;c.pins.elPositive=1;
  c.sensors={1,6,0,2,0x36,0};
  c.pins.azSensorSda=c.pins.elSensorSda=4;c.pins.azSensorScl=c.pins.elSensorScl=5;
  assert(!board::validateHardware(c));
  auto next=c;next.pins.elSensorSda=2;next.pins.elSensorScl=3;
  assert(!std::strcmp(board::validateHardware(next),"sensor_bus_unavailable"));
  next=c;next.sensors[3]=1;next.sensors[4]=6;
  assert(!std::strcmp(board::validateHardware(next),"sensor_address_conflict"));
  next=c;next.sensors[1]=0x15;assert(!board::validateHardware(next)); // no touch device
  fakeI2c[0]=fakeI2c[1]={};
  fakeI2c[0].devices[6][3]=4096>>6;fakeI2c[0].devices[6][4]=(4096&63)<<2;
  fakeI2c[0].devices[0x36][0xb]=0x20;fakeI2c[0].devices[0x36][0xc]=4;
  PositionSensors sensors;sensors.begin(c);
  assert(fakeI2c[0].begins==1 && fakeI2c[1].begins==0);
  assert(fakeI2c[0].sda==4 && fakeI2c[0].scl==5);
  assert(sensors.read(c,0)==4096 && sensors.read(c,1)==1024);
}
int main() {
  defaultsAndPins();relaysOnFreedPins();oneSensorBus();
  std::puts("PASS: headless C3 defaults, free GPIO, USB/Flash/UART ownership, relays, STOP/release/reversal and shared I2C");
}
