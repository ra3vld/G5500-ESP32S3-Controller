#pragma once
#include <array>
#include <cassert>
#include <cstdint>
struct TwoWire {
  std::array<uint8_t,256> registers{};
  unsigned reg=0,bytes=0,readAt=0,remaining=0;
  bool failed=false;
  explicit TwoWire(unsigned=0) {}
  bool begin(int sda,int scl,unsigned hz) {assert(sda==4&&scl==5&&hz==400000);return true;}
  void setTimeOut(unsigned) {}
  void beginTransmission(uint8_t address) {assert(address==0x15);bytes=0;}
  size_t write(uint8_t value) {if(!bytes++)reg=value;else registers[reg++]=value;return 1;}
  uint8_t endTransmission(bool=true) {return failed?2:0;}
  size_t requestFrom(uint8_t address,size_t count,bool=true) {
    assert(address==0x15);readAt=reg;remaining=failed?0:count;return remaining;
  }
  int read() {if(!remaining)return -1;--remaining;return registers[readAt++];}
};
inline TwoWire Wire;
