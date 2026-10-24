#include "motion_fixture.hpp"
#include "hud.hpp"
#include "hud_uart_tx.hpp"
#include <cassert>
#include <deque>
#include <vector>
#include <iostream>
using namespace forkrot;
struct Node;
struct RadioPacket { Node *from; uint32_t due; std::vector<uint8_t> bytes; };
struct Bus {
  uint32_t now=0, delay=600;
  Node *nodes[2]{};
  std::deque<RadioPacket> queue;
  std::vector<RadioPacket> history;
  void tick(uint32_t at);
};
struct Node : HudTransport {
  Bus &bus;
  Controller c=forkrot::testController(); Protocol p{c}; Hud h{c,p};
  Node(Bus &bus,uint64_t id,bool panel):bus(bus) {
    h.config.role=panel?DeviceRole::Hud:DeviceRole::Controller;
    h.config.uart=true; h.config.link.baud=9600; h.config.link.intervalMs=5000;
    h.begin(id,id+100);h.setTransport(this);c.feedback(20,30,true,true);
    bus.nodes[panel?1:0]=this;
  }
  bool sendHud(const HudEndpoint &to,const uint8_t *bytes,size_t n) override {
    assert(to.uart);
    RadioPacket packet{this,bus.now+bus.delay,{bytes,bytes+n}};
    bus.queue.push_back(packet);bus.history.push_back(packet);return true;
  }
};
void Bus::tick(uint32_t at) {
  now=at;
  for(auto *node:nodes){node->h.service(at);node->c.tick(at);}
  while(!queue.empty() && queue.front().due<=at) {
    auto packet=std::move(queue.front());queue.pop_front();
    for(auto *node:nodes)if(node!=packet.from)node->h.receive({0,4534,true},packet.bytes.data(),packet.bytes.size(),at);
  }
}
int main() {
  Bus bus;Node rotor(bus,1,false),panel(bus,2,true);
  rotor.c.setMode(Mode::Auto,0);
  assert(panel.h.add(1,{0,4534,true},"radio"));assert(panel.h.select(1));
  for(uint32_t now=10;now<=10000;now+=10)bus.tick(now);
  assert(panel.h.available() && !panel.h.active()->live);
  assert(!panel.h.manualAvailable()); // Extended capability is learned with the next state reply.
  unsigned queries=0;
  for(const auto &packet:bus.history)if(packet.from==&panel) {
    const auto kind=packet.bytes[4]; assert(kind!=7 && kind!=13);
    if(kind==2||kind==11)++queries;
  }
  assert(queries==2); // Exactly one full request every five seconds, without capability probes.
  assert(panel.h.freshnessAge()>2000);
  ControlCommand target;target.op=ControlOp::Target;target.target={90,0,true,false};
  assert(panel.h.submitAt(target,bus.now)==Result::Deferred);
  const auto request=panel.h.requestId();
  for(uint32_t now=10010;now<=12500;now+=10)bus.tick(now);
  assert(panel.h.result(request).result==Result::Ok && rotor.c.axes[0].target==90);
  assert(panel.h.manualAvailable()); // Slow state polling does not disable a usable manual command stream.
  ControlCommand stop;stop.op=ControlOp::Stop;
  assert(panel.h.submitAt(stop,bus.now)==Result::Deferred);
  for(uint32_t now=12510;now<=14000;now+=10)bus.tick(now);
  assert(panel.h.result(panel.h.requestId()).result==Result::Ok && !rotor.c.moving());
  // A disconnected radio becomes unavailable at its configured freshness deadline.
  bus.delay=60000;
  for(uint32_t now=14010;now<=33000;now+=10)bus.tick(now);
  assert(!panel.h.available());
  // UART STOP discards queued targets, manual samples and telemetry before transmission.
  HudUartTx tx;
  std::vector<uint8_t> motion,halt;
  for(const auto &packet:bus.history)if(packet.from==&panel && (packet.bytes[4]==4||packet.bytes[4]==9)) {
    const auto offset=packet.bytes[3]=='1'?45u:49u;
    if(packet.bytes[offset]==uint8_t(ControlOp::Target))motion=packet.bytes;
    if(packet.bytes[offset]==uint8_t(ControlOp::Stop))halt=packet.bytes;
  }
  assert(!motion.empty() && !halt.empty());
  assert(tx.enqueue(motion.data(),motion.size()));assert(tx.enqueue(halt.data(),halt.size()));
  HudUartDecoder decoder;uint8_t decoded[HUD_FRAME_MAX]{};unsigned frames=0;
  while(tx.pending())tx.drain(17,[&](const uint8_t *data,size_t n){
    for(size_t i=0;i<n;++i)if(const auto size=decoder.feed(data[i],decoded,sizeof decoded)) {
      assert(size==halt.size() && !std::memcmp(decoded,halt.data(),size));++frames;
    }
    return n;
  });
  assert(frames==1);
  std::cout << "PASS: paced UART polling, 1.2 s radio RTT, commands after old telemetry, STOP priority and disconnect freshness\n";
}
