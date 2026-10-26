// Persistent panel orientation, independent of motion/hardware trial records. GPL-3.0.
#pragma once
#include "panel_pose.h"
#include <array>
#include <cstdint>
namespace forkrot {
struct DisplaySettings {
  uint8_t screen=0,touch=0;
  uint32_t revision=0;
  bool samePose(const DisplaySettings &other) const {return screen==other.screen&&touch==other.touch;}
  bool valid() const {return screen<16&&touch<16;}
  unsigned width(unsigned w,unsigned h) const {return screen&1?h:w;}
  unsigned height(unsigned w,unsigned h) const {return screen&1?w:h;}
  panel_point_t physical(int x,int y,int w,int h) const {
    return panel_forward(screen,x,y,int(width(w,h)),int(height(w,h)));
  }
  panel_point_t logical(int x,int y,int w,int h) const {
    return panel_inverse(screen,x,y,int(width(w,h)),int(height(w,h)));
  }
  panel_point_t correctedTouch(int x,int y,int w,int h) const {
    auto p=panel_forward(touch,x,y,w,h);
    // Independent sensor quarter turns use the sensor's full normalized range.
    if(touch&1){p.x=int(int64_t(p.x)*(w-1)/(h-1));p.y=int(int64_t(p.y)*(h-1)/(w-1));}
    return logical(p.x,p.y,w,h);
  }
  panel_point_t uncorrectedTouch(int x,int y,int w,int h) const {
    auto p=physical(x,y,w,h);
    if(touch&1){p.x=int(int64_t(p.x)*(h-1)/(w-1));p.y=int(int64_t(p.y)*(w-1)/(h-1));}
    return panel_inverse(touch,p.x,p.y,w,h);
  }
  uint8_t madctl(uint8_t base) const {
    const unsigned r=screen&3;
    const bool swap=r&1,mx=((r==1||r==2)!=bool(screen&4)),my=((r==2||r==3)!=bool(screen&8));
    return uint8_t((base&~0xe0)|((bool(base&0x20)!=swap)?0x20:0)|
        ((bool(base&0x40)!=(base&0x20?my:mx))?0x40:0)|
        ((bool(base&0x80)!=(base&0x20?mx:my))?0x80:0));
  }
};
using DisplayBytes=std::array<uint8_t,16>;
inline uint32_t displayChecksum(const DisplayBytes &b) {
  uint32_t h=2166136261u;for(unsigned i=0;i<12;++i)h=(h^b[i])*16777619u;return h;
}
inline DisplayBytes encodeDisplay(const DisplaySettings &s) {
  DisplayBytes b{{'P','O',1,s.screen,s.touch}};
  for(unsigned i=0;i<4;++i)b[8+i]=uint8_t(s.revision>>(8*i));
  const auto crc=displayChecksum(b);for(unsigned i=0;i<4;++i)b[12+i]=uint8_t(crc>>(8*i));return b;
}
inline bool decodeDisplay(const DisplayBytes &b,DisplaySettings &s) {
  uint32_t crc=0,revision=0;for(unsigned i=0;i<4;++i){crc|=uint32_t(b[12+i])<<(8*i);revision|=uint32_t(b[8+i])<<(8*i);}
  if(b[0]!='P'||b[1]!='O'||b[2]!=1||b[3]>15||b[4]>15||b[5]||b[6]||b[7]||crc!=displayChecksum(b))return false;
  s.screen=b[3];s.touch=b[4];s.revision=revision;return true;
}
} // namespace forkrot
