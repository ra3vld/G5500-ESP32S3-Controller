#include "board.hpp"
#include "st77916_panel.hpp"
#include "touch_sample.hpp"
#include "display_settings.hpp"
#include <cassert>
#include <vector>
#include <string>
#include <iostream>
using namespace forkrot;
struct Bus {
  struct Command {uint8_t reg;std::vector<uint8_t> data;};
  std::vector<Command> sent;
  size_t failAt=~size_t(0);
  bool command(uint8_t reg,const uint8_t *data,size_t n) {
    if(sent.size()==failAt)return false;
    sent.push_back({reg,{data,data+n}});return true;
  }
};
int main() {
  static_assert(DISPLAY_KIND==6 && GRAPHICAL_DISPLAY);
  HardwareConfig c;
  assert(!board::validateHardware(c));
  assert(!board::validateHardware(c,false,true));
  for(auto member:{&PinMap::tftMosi,&PinMap::tftMiso,&PinMap::tftSck,&PinMap::tftCs,
                  &PinMap::tftD2,&PinMap::tftD3,&PinMap::touchSda,&PinMap::touchScl}) {
    auto bad=c;bad.pins.*member=-1;
    assert(std::string(board::validateHardware(bad))=="missing_pin");
    bad=c;bad.pins.*member=c.pins.azPositive;
    assert(std::string(board::validateHardware(bad))=="gpio_conflict");
  }
  auto bad=c;bad.pins.touchSda=c.pins.touchScl;
  assert(std::string(board::validateHardware(bad))=="gpio_conflict");
  if(FORK_CLASSIC_ESP32) {
    bad=c;bad.pins.tftMiso=36;
    assert(std::string(board::validateHardware(bad))=="input_only_gpio");
  }
  // Both encoders must share the remaining I2C controller; touch owns I2C1.
  bad=c;bad.sensors={1,6,0,1,7,0,0,0};
  bad.pins.azSensorSda=FORK_CLASSIC_ESP32?12:39;bad.pins.azSensorScl=FORK_CLASSIC_ESP32?2:40;
  bad.pins.elSensorSda=FORK_CLASSIC_ESP32?13:41;bad.pins.elSensorScl=FORK_CLASSIC_ESP32?4:42;
  assert(std::string(board::validateHardware(bad))=="sensor_bus_unavailable");
  bad.pins.elSensorSda=bad.pins.azSensorSda;bad.pins.elSensorScl=bad.pins.azSensorScl;
  assert(!board::validateHardware(bad));
  HardwareRecord record;record.good=c;record.candidate=c;
  record.candidate.pins.touchRst=FORK_CLASSIC_ESP32?12:39;
  record.candidate.pins.tftBacklight=-1;
  HardwareRecord restored;
  assert(decodeHardware(encodeHardware(record),restored));
  assert(restored.good==record.good && restored.candidate==record.candidate);
  const uint8_t id1[]={0,0x7f,0x7f,0x7f},id2[]={0,2,0x7f,0x7f};
  assert(st77916::variant(id1)==1 && st77916::variant(id2)==2);
  for(unsigned v:{1,2}) {
    Bus bus;unsigned waited=0;
    assert(st77916::initialize(bus,v,0xe0,[&](unsigned ms){waited+=ms;}));
    assert(waited>=120 && bus.sent.front().reg==0x3a && bus.sent.front().data[0]==0x55);
    assert(bus.sent.back().reg==0x36 && bus.sent.back().data[0]==0xe0);
    Bus broken;broken.failAt=3;
    assert(!st77916::initialize(broken,v,0,[](unsigned){}));assert(broken.sent.size()==3);
  }
  Bus bus;assert(st77916::window(bus,255,350,105,10));
  assert(bus.sent[0].reg==0x2a && bus.sent[0].data==std::vector<uint8_t>({0,255,1,103}));
  assert(bus.sent[1].reg==0x2b && bus.sent[1].data==std::vector<uint8_t>({1,94,1,103}));
  assert(!st77916::window(bus,359,359,2,1));assert(!st77916::window(bus,0,0,0,1));
  assert(bus.sent.size()==2);
  Bus broken;broken.failAt=1;assert(!st77916::window(broken,0,0,360,8));assert(broken.sent.size()==1);
  // Full 12-bit CST coordinates, edge taps, release and all display orientations.
  const uint8_t pressed[]={1,0x81,0x67,0x01,0x67},released[]={0,0xff,0xff,0xff,0xff};
  auto sample=decodeCstTouch(pressed,360,360,100);
  assert(sample.healthy && sample.down && sample.x==359 && sample.y==359);
  sample=decodeCstTouch(released,360,360,110);assert(sample.healthy && !sample.down);
  for(unsigned pose=0;pose<16;++pose) {
    DisplaySettings settings;settings.screen=pose;
    for(auto xy:{std::pair{0,0},std::pair{359,359},std::pair{180,20}}) {
      auto logical=settings.correctedTouch(xy.first,xy.second,360,360);
      auto physical=settings.uncorrectedTouch(logical.x,logical.y,360,360);
      assert(physical.x==xy.first && physical.y==xy.second);
    }
  }
  std::cout<<"ST77916 protocol, GPIO, touch and record tests passed\n";
}
