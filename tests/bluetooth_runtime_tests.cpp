#include "bluetooth.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main() {
  static_assert(BLUETOOTH_ENABLED==0, "test begins with the disabled legacy default");
  static_assert(sizeof(Bluetooth)<=32, "inactive transports must not retain inline queues");
  Bluetooth off;
  assert(off.begin(BT_NONE) && !off.connected() && !off.linkConnected());
  assert(fakeSpp.starts==0 && !off.send("C"));
  Bluetooth hc;
  assert(hc.begin(BT_HC05));
  assert(fakeSpp.starts==0 && fakeUart.baud==9600 && fakeUart.rxPin==board::PIN_HC05_RX);
  fakeHardware.levels[board::PIN_HC05_STATE]=HIGH; assert(hc.connected());
  for(char c : std::string("C2\r")) fakeUart.rx.push_back(c);
  std::string line; assert(hc.receive(line) && line=="C2");
  hc.end();
  PinMap remapped; remapped.hc05Rx=34; remapped.hc05Tx=23; remapped.hc05State=22;
  assert(hc.begin(BT_HC05,remapped));
  assert(fakeUart.rxPin==34 && fakeUart.txPin==23);
  fakeHardware.levels[board::PIN_HC05_STATE]=LOW;
  fakeHardware.levels[22]=HIGH; assert(hc.connected());
  // Fresh objects model separate boots. The real SDK's released SPP memory is
  // intentionally not reacquired without rebooting.
  Bluetooth spp;
  assert(spp.begin(BT_ESP32_SPP) && fakeSpp.starts==1);
  sppConnect(9); sppFeed("M090\r");
  assert(spp.connected() && spp.receive(line) && line=="M090");
  spp.end(); assert(!spp.connected() && !spp.send("C"));
  std::cout<<"PASS: OFF, HC-05 and SPP selected at runtime in one ESP32 binary with Bluetooth default OFF\n";
}
