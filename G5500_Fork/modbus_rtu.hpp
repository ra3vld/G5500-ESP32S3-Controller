// Portable RTU framing and bounded serial queues. All command execution is on loop().
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace forkrot::mb {
inline constexpr size_t Max=256;
inline constexpr uint8_t Service=0x41;
inline uint16_t get16(const uint8_t *p) {return uint16_t(p[0])<<8|p[1];}
inline uint32_t get32(const uint8_t *p) {return uint32_t(get16(p))<<16|get16(p+2);}
inline void put16(uint8_t *p,uint16_t v){p[0]=uint8_t(v>>8);p[1]=uint8_t(v);}
inline void put32(uint8_t *p,uint32_t v){put16(p,uint16_t(v>>16));put16(p+2,uint16_t(v));}
inline uint16_t crc(const uint8_t *p,size_t n) {
  uint16_t v=0xffff;for(size_t i=0;i<n;++i){v^=p[i];for(unsigned b=0;b<8;++b)v=(v&1)?(v>>1)^0xa001:v>>1;}return v;
}
struct Frame {
  uint8_t data[Max]{};uint16_t size=0;uint32_t at=0;
  bool valid() const {return size>=4 && size<=Max && crc(data,size)==0;}
  void finish(size_t n) {if(n>Max-2){size=0;return;}const auto c=crc(data,n);data[n]=uint8_t(c);data[n+1]=uint8_t(c>>8);size=uint16_t(n+2);}
};
inline Frame exception(const Frame &request,uint8_t code){Frame f;f.data[0]=request.data[0];f.data[1]=request.data[1]|0x80;f.data[2]=code;f.finish(3);return f;}
inline uint32_t t35(uint32_t baud){return baud>19200?1750:(38500000u+baud-1)/baud;}
inline uint32_t t15(uint32_t baud){return baud>19200?750:(16500000u+baud-1)/baud;}
// Tests and interrupt/event adapters supply timestamps, not main-loop read times.
class Framer {
  Frame frame_;uint32_t last_=0;bool discard_=true,started_=false;
public:
  uint32_t rejected=0;
  template<class Receive> void poll(uint32_t now,uint32_t baud,Receive receive) {
    if(!started_ && uint32_t(now-last_)>=t35(baud))discard_=false;
    if(started_ && uint32_t(now-last_)>=t35(baud)) {
      if(!discard_ && frame_.valid()){frame_.at=last_;receive(frame_);}else if(frame_.size)++rejected;
      frame_.size=0;started_=false;discard_=false;
    }
  }
  template<class Receive> void feed(uint8_t byte,uint32_t now,uint32_t baud,Receive receive) {
    poll(now,baud,receive);
    if(started_ && uint32_t(now-last_)>t15(baud))discard_=true;
    if(!started_){started_=true;frame_.size=0;}
    if(frame_.size<Max)frame_.data[frame_.size++]=byte;else discard_=true;
    last_=now;
  }
  void reset(uint32_t now){frame_.size=0;last_=now;started_=false;discard_=true;}
  void idle(){frame_.size=0;started_=discard_=false;}
};
}
