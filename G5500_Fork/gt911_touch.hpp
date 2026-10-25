// Bounded GT911 polling, independent of the board's optional INT wiring. GPL-3.0.
#pragma once
#include "touch_sample.hpp"
#include <cstddef>
#include <initializer_list>
namespace forkrot {
template<class Bus> class Gt911Touch {
  Bus &bus_;
  uint8_t address_=0;
  TouchSample previous_{};
  uint32_t freshAt_=0;
  uint32_t probeAt_=0;
  bool valid_=false;
  bool read(uint16_t reg,uint8_t *data,size_t size) {
    bus_.beginTransmission(address_);bus_.write(uint8_t(reg>>8));bus_.write(uint8_t(reg));
    if(bus_.endTransmission(false)!=0 || bus_.requestFrom(address_,size)!=size) return false;
    for(size_t i=0;i<size;++i) data[i]=bus_.read();
    return true;
  }
  bool acknowledge() {
    bus_.beginTransmission(address_);bus_.write(uint8_t(0x81));bus_.write(uint8_t(0x4e));bus_.write(uint8_t(0));
    return bus_.endTransmission()==0;
  }
public:
  explicit Gt911Touch(Bus &bus):bus_(bus) {}
  bool begin() {
    valid_=false;
    for(uint8_t address:{uint8_t(0x5d),uint8_t(0x14)}) {
      address_=address;uint8_t product[4];
      if(read(0x8140,product,sizeof product) && product[0]=='9' && product[1]=='1' && product[2]=='1') return true;
    }
    address_=0;return false;
  }
  TouchSample poll(uint32_t now) {
    TouchSample sample;sample.sampledAt=now;
    if(!address_) {
      if(uint32_t(now-probeAt_)<1000) return sample;
      probeAt_=now;
      if(!begin()) return sample;
    }
    uint8_t status=0;
    if(!read(0x814e,&status,1)) {valid_=false;return sample;}
    if(!(status&0x80)) {
      // No new frame is not a release. Stale held input cannot renew a motor lease.
      if(valid_ && (!previous_.down || uint32_t(now-freshAt_)<=100)) {
        sample=previous_;sample.sampledAt=now;
      } else valid_=false;
      return sample;
    }
    const unsigned count=status&15;
    bool complete=true;
    if(count==0) sample.healthy=true;
    else if(count==1) {
      uint8_t point[8];complete=read(0x814f,point,sizeof point);
      if(complete) {
        const unsigned x=point[1]|(unsigned(point[2])<<8),y=point[3]|(unsigned(point[4])<<8);
        // Landscape MADCTL=MY|MX|MV: raw 320x480 -> logical 480x320.
        sample.healthy=x<320 && y<480;
        if(sample.healthy) {sample.x=479-y;sample.y=x;sample.down=true;}
      }
    }
    // Clear status only after consuming the coordinate record, including invalid/multi-touch frames.
    if(!acknowledge() || !complete) sample.healthy=sample.down=false;
    valid_=sample.healthy;freshAt_=now;previous_=sample;
    return sample;
  }
};
}
