#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <vector>
struct FakeI2c {
  bool beginOk=true;
  unsigned begins=0,transactions=0,timeout=0,frequency=0;
  int sda=-1,scl=-1;
  std::map<unsigned,std::array<uint8_t,256>> devices;
  std::vector<unsigned> registers;
  int nackAt=-1,shortAt=-1;
};
inline FakeI2c fakeI2c[2];
class TwoWire {
  unsigned bus_,address_=0,reg_=0,readAt_=0,remaining_=0;
public:
  explicit TwoWire(unsigned bus):bus_(bus) {}
  bool begin(int sda,int scl,unsigned frequency) {
    auto &f=fakeI2c[bus_];++f.begins;f.sda=sda;f.scl=scl;f.frequency=frequency;return f.beginOk;
  }
  void setTimeOut(unsigned timeout) { fakeI2c[bus_].timeout=timeout; }
  void beginTransmission(uint8_t address) { address_=address; }
  size_t write(uint8_t reg) { reg_=reg;return 1; }
  uint8_t endTransmission(bool stop) {
    auto &f=fakeI2c[bus_];f.registers.push_back(reg_);++f.transactions;
    return stop || int(f.transactions)==f.nackAt || !f.devices.count(address_)?2:0;
  }
  size_t requestFrom(uint8_t address,size_t count,bool stop) {
    auto &f=fakeI2c[bus_];
    if(!stop || address!=address_ || int(f.transactions)==f.shortAt) return 0;
    readAt_=reg_;remaining_=count;return count;
  }
  int read() { if(!remaining_)return -1;--remaining_;return fakeI2c[bus_].devices[address_][readAt_++]; }
};
inline TwoWire Wire{0};
