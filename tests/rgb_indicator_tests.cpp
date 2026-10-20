#include "rgb_indicator.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main() {
  RgbIndicator led; led.write(-1,1,2,3);assert(fakeRmt.creates==0);
  led.write(7,0xa5,0x12,0x80);
  assert(fakeRmt.config.gpio_num==7 && fakeRmt.config.resolution_hz==10000000);
  assert(fakeRmt.config.mem_block_symbols==SOC_RMT_MEM_WORDS_PER_CHANNEL && fakeRmt.config.trans_queue_depth==1);
  assert(fakeRmt.creates==1 && fakeRmt.encoders==1 && fakeRmt.enables==1 && fakeRmt.sends==1);
  assert(fakeRmt.sent.size()==25 && fakeRmt.waitMs==20);
  uint32_t color=0;
  for(unsigned i=0;i<24;++i) {
    const auto &s=fakeRmt.sent[i];
    assert(s.level0==1 && s.level1==0 && s.duration0+s.duration1==12);
    assert(s.duration0==4 || s.duration0==8);
    color=(color<<1)|(s.duration0==8);
  }
  assert(color==0x12a580); // G, R, B: same byte order as the board's Arduino API.
  assert(!fakeRmt.sent[24].level0 && !fakeRmt.sent[24].level1 && fakeRmt.sent[24].duration0==500);
  led.write(7,0,0,0);assert(fakeRmt.creates==1 && fakeRmt.sends==2);
  led.write(8,0,0,0);assert(fakeRmt.creates==1 && fakeRmt.sends==2); // pin changes require reboot
  fakeRmt.waitResult=1;led.write(7,10,20,30);
  const auto *pending=static_cast<const rmt_symbol_word_t *>(fakeRmt.payload);
  const auto first=pending[0].duration0;
  led.write(7,255,255,255);
  assert(fakeRmt.sends==3 && pending[0].duration0==first); // timeout retains DMA/encoder payload lifetime
  fakeRmt={};fakeRmt.encoderResult=1;
  RgbIndicator badEncoder;badEncoder.write(3,1,2,3);badEncoder.write(3,1,2,3);
  assert(fakeRmt.creates==1 && fakeRmt.channelDeletes==1 && fakeRmt.sends==0);
  fakeRmt={};fakeRmt.enableResult=1;
  RgbIndicator badEnable;badEnable.write(3,1,2,3);
  assert(fakeRmt.channelDeletes==1 && fakeRmt.encoderDeletes==1 && fakeRmt.sends==0);
  std::cout << "PASS: RGB RMT GPIO, board color order, WS2812 timing/latch, channel reuse, failures and persistent timeout payload\n";
}
