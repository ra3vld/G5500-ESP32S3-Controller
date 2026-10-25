#include "board.hpp"
#include "hud.hpp"
#include <cassert>
#include <cstdio>
using namespace forkrot;
int main() {
  HardwareConfig c;
  assert(!board::validateHardware(c));
  assert(!c.simulation && HudConfig{}.role==DeviceRole::Hud);
  assert(board::PIN_CONFIRM==9);
  assert(board::gpio(20) && board::gpio(21) && board::adc(0) && !board::adc(5));
  for(int pin=0;pin<=10;++pin) {
    if(pin==8 || pin==9) continue;
    auto next=c;next.pins.led=pin;
    assert(board::validateHardware(next));
  }
  for(int pin=11;pin<=19;++pin) assert(!board::gpio(pin));
  auto bad=c;bad.pins.azPositive=8;assert(board::validateHardware(bad));
  bad=c;bad.bluetooth=true;assert(board::validateHardware(bad));
  bad=c;bad.sensors[0]=1;assert(board::validateHardware(bad));
  bad=c;bad.pins.uartRx=bad.pins.uartTx=-1;bad.pins.azNegative=20;bad.pins.azPositive=21;
  bad.drive=DRIVE_BTS7960;assert(!board::validateHardware(bad)); // bridge enable GPIO is optional
  auto relays=bad;relays.drive=DRIVE_YAESU;
  assert(!board::validateHardware(relays) && relays.manualWithoutFeedback());
  bad=relays;bad.drive=DRIVE_BTS7960;bad.pins.azSpeedEnable=8;assert(!board::validateHardware(bad));
  bad=relays;bad.drive=DRIVE_SEPARATE_PWM;bad.pins.azSpeedEnable=8;assert(!board::validateHardware(bad));
  bad=c;bad.pins.uartRx=bad.pins.uartTx=-1;bad.bluetooth=true;
  bad.pins.hc05Rx=20;bad.pins.hc05Tx=21;bad.pins.hc05State=8;assert(!board::validateHardware(bad));
  bad=c;bad.buttonGpio=-1;bad.local[0]=LOCAL_INPUT_BUTTONS;
  bad.pins.localAzNegative=8;bad.pins.localAzPositive=9;assert(!board::validateHardware(bad));
  bad=relays;bad.pins.led=8;assert(!board::validateHardware(bad));
  bad=relays;bad.sensors[0]=1;bad.pins.azSensorSda=4;bad.pins.azSensorScl=5;
  assert(!board::validateHardware(bad));
  bad.sensors[1]=0x15;assert(board::validateHardware(bad));
  Controller controller;
  assert(!controller.movementBlocked());
  assert(controller.jog(0,1,false,10)==Result::Unavailable);
  controller.configureHardware(relays);
  assert(controller.jog(0,1,false,10)==Result::Ok);
  assert(controller.target({90,0,true,false},false,10)==Result::NotCalibrated);
  c.simulation=true;controller.configureHardware(c);
  assert(!controller.movementBlocked());
  Protocol protocol(controller);Hud hud(controller,protocol);controller.remote=&hud;
  assert(!controller.movementBlocked());

  std::puts("PASS: round panel GPIO, role and motion gates");
}
