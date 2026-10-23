#include "motion_fixture.hpp"
#include "rs485_hud.hpp"
#include "rs485_reply.hpp"
#include "local_input.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <vector>
using namespace forkrot;
struct Node:HudTransport {
  Controller c=testController();Protocol p{c};Hud hud{c,p};Rs485Hud wire;
  std::deque<uint8_t> rx;std::vector<uint8_t> sent;uint32_t now=0;
  Node(bool remote) {
    hud.config.role=remote?DeviceRole::Hud:DeviceRole::Controller;hud.config.uart=true;
    hud.begin(remote?2:1,remote?102:101);hud.setTransport(this);
    c.feedback(20,30,true,true);wire.begin(remote,hud.boot,0);
  }
  bool sendHud(const HudEndpoint &,const uint8_t *p,size_t n) override {return wire.enqueue(p,n,now);}
  void read() { while(!rx.empty()) {auto b=rx.front();rx.pop_front();wire.feed(b,now,[&](const uint8_t *p,size_t n){hud.receive({0,4534,true},p,n,now);});} }
  bool write(Node &other,bool drop=false) {
    bool wrote=false;
    wire.service(now,12,[&](const uint8_t *p,size_t n) {
      wrote=n!=0;sent.insert(sent.end(),p,p+n);
      if(!drop)other.rx.insert(other.rx.end(),p,p+n);
      return n;
    },[]{return true;});return wrote;
  }
};
int main() {
  // HUD/5 absolute input keeps release priority even with its speed flag set.
  {
    uint8_t command[64]{}; command[3]='5'; command[4]=4;
    uint8_t manual[64]{}; manual[3]='5'; manual[4]=15; manual[54]=2;
    Rs485Hud transport; transport.begin(true,1,0);
    assert(transport.enqueue(command,sizeof command,0));
    assert(transport.enqueue(command,sizeof command,0));
    assert(!transport.enqueue(command,sizeof command,0));
    assert(transport.enqueue(manual,sizeof manual,0));
    assert(!transport.enqueue(command,sizeof command,0)); // Running input cannot evict commands.
    manual[54]=3; assert(transport.enqueue(manual,sizeof manual,0));
    assert(transport.enqueue(command,sizeof command,0)); // Release cleared the older commands.
  }
  Rs485Reply reply;std::string output;
  const std::string text="AZ=123\r\n";
  assert(reply.write(reinterpret_cast<const uint8_t *>(text.data()),text.size())==text.size());
  auto write=[&](const uint8_t *p,size_t n){output.append(reinterpret_cast<const char *>(p),n);return n;};
  reply.received(100);reply.service(102,100,write);assert(output.empty());
  reply.received(103);reply.service(105,100,write);assert(output.empty());
  for(unsigned t=106;t<120;++t)reply.service(t,2,write);
  assert(output==text); // Complete response after CR/LF and partial UART writes.

  Node controller(false),hud(true);
  // A responder has no right to start a transmission, even with telemetry queued.
  uint8_t dummy[60]{};dummy[3]='4';dummy[4]=14;
  assert(controller.wire.enqueue(dummy,sizeof dummy,0));
  for(unsigned t=0;t<30;++t) {controller.now=t;assert(!controller.write(hud));}
  assert(hud.hud.add(1,{0,4534,true},"rotator") && hud.hud.select(1));
  bool broken=false,dropResponse=false;
  auto run=[&](unsigned first,unsigned last) {
    for(unsigned t=first;t<=last;++t) {
      controller.now=hud.now=t;controller.read();hud.read();
      controller.hud.service(t);hud.hud.service(t);controller.c.tick(t);hud.c.tick(t);
      const bool a=hud.write(controller,broken),b=controller.write(hud,broken||dropResponse);
      assert(!(a&&b)); // Includes sustained input and the full-state exchange.
    }
  };
  run(30,1800);assert(hud.hud.available() && hud.hud.active()->manual);
  controller.c.setMode(Mode::Auto,1801);run(1801,2100);
  // Unmodified PC GS-232 parser on the HUD forwards the goal to the controller.
  const auto goal=hud.p.line("W120 40",2101);
  assert(goal.result==Result::Deferred);const auto request=hud.hud.requestId();
  run(2101,2550);
  assert(controller.c.axes[0].target==120 && controller.c.axes[1].target==40);
  assert(hud.hud.result(request).result==Result::Ok);
  const auto position=hud.p.line("C2",2551);
  assert(position.reply=="AZ=020 EL=030\r\n");
  // Lost response: core retries the same STOP transaction, no extra execution.
  dropResponse=true;hud.p.line("S",2552);const auto stop=hud.hud.requestId();
  const auto epoch=controller.c.stopEpoch;run(2552,2800);dropResponse=false;run(2801,3350);
  assert(controller.c.stopEpoch==epoch+1 && hud.hud.result(stop).result==Result::Ok);
  // Link loss expires the HUD's view and jog leases; reconnection cannot replay a goal.
  broken=true;run(3351,4900);assert(!hud.hud.available());
  const auto oldTarget=controller.c.axes[0].target;
  assert(hud.p.line("M200",4901).result!=Result::Ok);
  broken=false;run(4901,6600);assert(hud.hud.available() && controller.c.axes[0].target==oldTarget);
  LocalAxisInput input(hud.c,3);
  input.sample(0,0,3,hud.hud.manualAvailable(),6601,hud.hud.selectionEpoch);run(6601,6640);
  input.sample(0,0,3,hud.hud.manualAvailable(),6641,hud.hud.selectionEpoch);run(6641,6660);
  for(unsigned t=6661;t<7100;t+=20) {
    input.sample(.5f,0,2,hud.hud.manualAvailable(),t,hud.hud.selectionEpoch);run(t,t+19);
  }
  assert(controller.c.axes[0].wanted==1);
  input.sample(0,0,3,hud.hud.manualAvailable(),7101,hud.hud.selectionEpoch);run(7101,7400);
  assert(!controller.c.axes[0].wanted);
  controller.hud.begin(1,999);controller.wire.begin(false,999,7401);
  run(7401,9500);assert(hud.hud.available() && hud.hud.active()->boot==999);
  // Maximum payload and an exact duplicate request are handled without a
  // second dispatch. An oversized frame must not poison the next boundary.
  Rs485Hud maxMaster,maxSlave;maxMaster.begin(true,123,0);maxSlave.begin(false,321,0);
  uint8_t maximum[HUD_FRAME_MAX]{};maximum[3]='4';maximum[4]=12;maximum[100]=127;
  assert(maxMaster.enqueue(maximum,sizeof maximum,0));
  std::vector<uint8_t> requestWire;unsigned calls=0;
  maxMaster.service(20,1000,[](const uint8_t *,size_t n){return n;},[]{return true;});
  maxMaster.service(21,1000,[&](const uint8_t *p,size_t n){requestWire.insert(requestWire.end(),p,p+n);return n;},[]{return true;});
  for(unsigned i=0;i<2000;++i)maxSlave.feed(1,22,[](const uint8_t *,size_t){assert(false);});
  maxSlave.feed(0,22,[](const uint8_t *,size_t){assert(false);});
  auto receive=[&](const uint8_t *p,size_t n){assert(n==sizeof maximum && !std::memcmp(p,maximum,n));++calls;};
  auto corrupted=requestWire;
  auto corrupt=std::find(corrupted.begin()+20,corrupted.end(),uint8_t(127));
  assert(corrupt!=corrupted.end());*corrupt=126;
  const auto badFrames=maxSlave.rejected;
  for(auto b:corrupted)maxSlave.feed(b,23,receive);
  assert(!calls && maxSlave.rejected==badFrames+1);
  for(auto b:requestWire)maxSlave.feed(b,23,receive);
  assert(calls==1);
  maxSlave.service(25,1000,[](const uint8_t *,size_t n){return n;},[]{return true;});
  for(auto b:requestWire)maxSlave.feed(b,27,receive);
  assert(calls==1);
  // CRC damage is discarded; a following valid frame restores framing.
  Node slave(false);unsigned delivered=0;
  auto count=[&](const uint8_t *,size_t){++delivered;};
  const auto before=slave.wire.rejected;
  for(auto byte:std::vector<uint8_t>{4,'b','a','d',0})slave.wire.feed(byte,10,count);
  assert(slave.wire.rejected>before && delivered==0);
  // Exact duplicate of the final empty poll returns cached data, with no delivery.
  auto end=std::find(hud.sent.rbegin()+1,hud.sent.rend(),uint8_t(0));
  const size_t offset=end==hud.sent.rend()?0:size_t(hud.sent.rend()-end);
  const std::vector<uint8_t> poll(hud.sent.begin()+offset,hud.sent.end());
  for(auto byte:poll)slave.wire.feed(byte,20,count);
  std::vector<uint8_t> response;
  for(unsigned t=20;t<120;++t)slave.wire.service(t,12,[&](const uint8_t *p,size_t n){response.insert(response.end(),p,p+n);return n;},[]{return true;});
  const auto once=delivered;
  for(auto byte:poll)slave.wire.feed(byte,130,count);
  assert(delivered==once && !response.empty());
  std::cout<<"PASS: half-duplex polling, PC GS-232 through HUD, position, manual input/release, STOP/lost ACK, offline/reconnect/reboot, maximum frame, CRC and duplicate poll\n";
}
