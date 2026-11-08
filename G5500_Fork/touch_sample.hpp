// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
struct TouchSample {
  bool healthy=false,down=false;
  uint16_t x=0,y=0;
  uint32_t sampledAt=0;
  uint8_t contactCode=0;
};
// CST8xx registers 0x02..0x06. Idle frames may contain stale coordinates.
inline TouchSample decodeCstTouch(const uint8_t (&data)[5], unsigned width, unsigned height, uint32_t at) {
  TouchSample sample;
  sample.sampledAt=at;
  const unsigned count=data[0]&15, event=data[1]>>6;
  sample.contactCode=uint8_t(count | (event<<4));
  if(!count) {sample.healthy=true;return sample;}
  if(count!=1 || event==3) return sample;
  sample.x=((data[1]&15)<<8)|data[2];sample.y=((data[3]&15)<<8)|data[4];
  sample.healthy=sample.x<width && sample.y<height;
  sample.down=sample.healthy && event!=1;
  return sample;
}
// Some CST816 samples briefly report count=0/event=0 during a held swipe.
// Bridge only that ambiguous frame, only for an existing manual contact, and
// never renew its deadline with another idle frame. Explicit UP and faults win.
class CstContactFilter {
  TouchSample lastDown_{};
  bool held_=false;
public:
  static constexpr uint32_t ReleaseGraceMs=40;
  TouchSample sample(TouchSample raw,bool manualActive) {
    if(raw.healthy && raw.down) {lastDown_=raw;held_=true;return raw;}
    if(raw.healthy && raw.contactCode==0 && manualActive && held_ &&
        uint32_t(raw.sampledAt-lastDown_.sampledAt)<ReleaseGraceMs) {
      raw.down=true;raw.x=lastDown_.x;raw.y=lastDown_.y;
      return raw;
    }
    held_=false;
    return raw;
  }
};
}
