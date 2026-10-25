#include "gt911_touch.hpp"
#include <cassert>
#include <array>
#include <map>
#include <vector>
#include <cstdio>
struct Bus {
  unsigned address=0,reg=0,index=0;
  unsigned accepted=0x5d;
  bool fail=false,shortRead=false,ackFail=false;
  std::vector<uint8_t> sent;
  std::map<unsigned,uint8_t> memory;
  unsigned acknowledgements=0;
  void beginTransmission(uint8_t a) {address=a;sent.clear();}
  void write(uint8_t b) {sent.push_back(b);}
  int endTransmission(bool stop=true) {
    if(fail || address!=accepted) return 2;
    assert(sent.size()>=2);reg=(unsigned(sent[0])<<8)|sent[1];
    if(stop) {assert(reg==0x814e && sent.size()==3 && sent[2]==0);++acknowledgements;return ackFail?2:0;}
    return 0;
  }
  size_t requestFrom(uint8_t,size_t n) {index=0;return shortRead?0:n;}
  uint8_t read() {return memory[reg+index++];}
  void frame(unsigned count,unsigned x=0,unsigned y=0) {
    memory[0x814e]=0x80|count;memory[0x8150]=x&255;memory[0x8151]=x>>8;
    memory[0x8152]=y&255;memory[0x8153]=y>>8;
  }
};
int main() {
  Bus bus;bus.memory[0x8140]='9';bus.memory[0x8141]='1';bus.memory[0x8142]='1';
  forkrot::Gt911Touch<Bus> touch(bus);assert(touch.begin());
  auto sample=touch.poll(0);assert(!sample.healthy && !sample.down);
  bus.frame(0);sample=touch.poll(1);assert(sample.healthy && !sample.down);
  const std::array<std::array<unsigned,4>,4> corners{{{0,0,479,0},{319,0,479,319},{319,479,0,319},{0,479,0,0}}};
  for(auto p:corners) {bus.frame(1,p[0],p[1]);sample=touch.poll(10);assert(sample.healthy && sample.down && sample.x==p[2] && sample.y==p[3]);}
  bus.memory[0x814e]=0;sample=touch.poll(110);assert(sample.healthy && sample.down);
  sample=touch.poll(111);assert(!sample.healthy && !sample.down);
  sample=touch.poll(112);assert(!sample.healthy); // no synthetic release after timeout
  bus.frame(0);sample=touch.poll(113);assert(sample.healthy && !sample.down);
  for(auto p:std::array<std::array<unsigned,2>,2>{{{320,10},{10,480}}}) {
    bus.frame(1,p[0],p[1]);sample=touch.poll(120);assert(!sample.healthy && !sample.down);
  }
  bus.frame(2);sample=touch.poll(130);assert(!sample.healthy && !sample.down);
  bus.frame(1,100,200);bus.shortRead=true;sample=touch.poll(140);assert(!sample.healthy && !sample.down);bus.shortRead=false;
  bus.ackFail=true;sample=touch.poll(150);assert(!sample.healthy && !sample.down);bus.ackFail=false;
  bus.fail=true;sample=touch.poll(160);assert(!sample.healthy && !sample.down);bus.fail=false;
  bus.memory[0x814e]=0;sample=touch.poll(170);assert(!sample.healthy);
  bus.frame(0);sample=touch.poll(180);assert(sample.healthy && !sample.down);
  bus.frame(1,10,20);sample=touch.poll(0xfffffff0);assert(sample.down);
  bus.memory[0x814e]=0;sample=touch.poll(0x20);assert(sample.down);
  sample=touch.poll(0x90);assert(!sample.healthy);
  assert(bus.acknowledgements>10);
  Bus alternate=bus;alternate.accepted=0x14;alternate.frame(0);
  forkrot::Gt911Touch<Bus> other(alternate);assert(other.begin());assert(other.poll(10).healthy);
  alternate.memory[0x8140]='0';assert(!other.begin());assert(!other.poll(500).healthy);
  alternate.memory[0x8140]='9';assert(other.poll(1000).healthy); // bounded re-probe after a failed identity read
  std::puts("PASS: GT911 identity, frame acknowledgement, rotated corners, bounds, multi-touch, stale input, I2C failures and millis wrap");
}
