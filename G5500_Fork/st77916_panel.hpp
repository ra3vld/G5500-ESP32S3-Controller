// ST77916 panel protocol, independent of MCU and GUI. GPL-3.0.
#pragma once
#include "st77916_init.hpp"
#include <cstddef>
namespace forkrot::st77916 {
inline unsigned variant(const uint8_t (&id)[4]) {
  return id[0]==0 && id[1]==0x02 && id[2]==0x7f && id[3]==0x7f ? 2 : 1;
}
template<class Bus,class Wait> bool initialize(Bus &bus,unsigned version,uint8_t orientation,Wait wait) {
  const auto *commands=version==2?revised:original;
  const auto count=version==2?sizeof revised/sizeof *revised:sizeof original/sizeof *original;
  const uint8_t rgb565=0x55;
  if(!bus.command(0x3a,&rgb565,1))return false;
  for(size_t i=0;i<count;++i) {
    const auto &c=commands[i];
    if(!bus.command(c.command,c.data,c.size))return false;
    if(c.waitMs)wait(c.waitMs);
  }
  return bus.command(0x36,&orientation,1);
}
template<class Bus> bool window(Bus &bus,unsigned x,unsigned y,unsigned w,unsigned h) {
  if(!w || !h || x>=360 || y>=360 || w>360-x || h>360-y)return false;
  const unsigned right=x+w-1,bottom=y+h-1;
  const uint8_t columns[]={uint8_t(x>>8),uint8_t(x),uint8_t(right>>8),uint8_t(right)};
  const uint8_t rows[]={uint8_t(y>>8),uint8_t(y),uint8_t(bottom>>8),uint8_t(bottom)};
  return bus.command(0x2a,columns,sizeof columns) && bus.command(0x2b,rows,sizeof rows);
}
}
