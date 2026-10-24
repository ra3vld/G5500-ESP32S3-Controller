#include "hud_uart_gate.hpp"
#include <cassert>
#include <vector>
#include <iostream>
using namespace forkrot;
struct Tx {
  size_t left=80;
  template<class Write> void drain(size_t available,Write write) {
    uint8_t bytes[80]{};
    const size_t n=std::min(left,available);
    if(n)left-=write(bytes,n);
  }
};
int main() {
  HudLinkConfig config; config.baud=9600;
  HudUartGate gate; Tx tx;
  bool ready=false;
  std::vector<size_t> writes;
  auto tick=[&](uint32_t now) {
    gate.service(now,config,true,ready,100,tx,
        [&](const uint8_t *,size_t n){writes.push_back(n);return n;});
  };
  tick(0); assert(writes.empty());
  ready=true; tick(1); assert(writes.size()==1 && writes[0]==32);
  tick(2); assert(writes.size()==1);
  ready=false; tick(100); assert(writes.size()==1);
  ready=true; tick(101); assert(writes.size()==2 && tx.left==16);
  tick(137); assert(writes.size()==3 && !tx.left);
  tick(200); assert(writes.size()==3);
  Tx cable; HudUartGate direct;
  direct.service(0,config,false,true,100,cable,[](const uint8_t *,size_t n){return n;});
  assert(!cable.left);
  HudUartGate wrap; Tx partial;
  auto run=[&](uint32_t now){wrap.service(now,config,true,true,100,partial,
      [](const uint8_t *,size_t){return size_t(1);});};
  run(UINT32_MAX-3);assert(partial.left==79);
  run(UINT32_MAX);assert(partial.left==79);
  run(0);assert(partial.left==78);
  std::cout << "PASS: AUX gating, bounded chunks, direct UART, partial writes and millis wrap\n";
}
