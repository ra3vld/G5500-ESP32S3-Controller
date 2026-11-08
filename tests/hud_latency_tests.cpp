#include "motion_fixture.hpp"
#include "hud.hpp"
#include <cassert>
#include <vector>
#include <iostream>
using namespace forkrot;
struct Node;
struct PacketOnWire {
  HudEndpoint from, to;
  std::vector<uint8_t> data;
  uint32_t due;
};
struct Bus {
  uint32_t now = 0, delay = 150;
  bool dropReplies=false;
  uint32_t suppressV5Until=0;
  std::vector<Node *> nodes;
  std::vector<PacketOnWire> queue;
  void tick(uint32_t);
};
struct Node : HudTransport {
  Bus &bus;
  Controller c = forkrot::testController();
  Protocol p{c};
  Hud h{c, p};
  HudEndpoint endpoint;
  Node(Bus &b, unsigned id, bool hud) : bus(b), endpoint{id, 4534, false} {
    h.config.role = hud ? DeviceRole::Hud : DeviceRole::Controller;
    h.begin(id, id + 100);
    h.setTransport(this);
    c.feedback(20, 30, true, true);
    bus.nodes.push_back(this);
  }
  bool sendHud(const HudEndpoint &to, const uint8_t *data, size_t n) override {
    if(endpoint.address==1 && bus.dropReplies)return true;
    if(endpoint.address==1 && data[3]=='5' && bus.now<bus.suppressV5Until)return true;
    bus.queue.push_back({endpoint, to, {data, data + n}, bus.now + bus.delay});
    return true;
  }
};
void Bus::tick(uint32_t at) {
  now = at;
  for (auto *node : nodes) {
    node->h.service(now);
  }
  for (size_t i = 0; i < queue.size();) {
    if (int32_t(now - queue[i].due) < 0) {
      ++i;
      continue;
    }
    auto packet = std::move(queue[i]);
    queue.erase(queue.begin() + i);
    for (auto *node : nodes) {
      if (node->endpoint == packet.to) {
        node->h.receive(packet.from, packet.data.data(), packet.data.size(), now);
      }
    }
  }
}
int main() {
  for(unsigned suppress:{700u,UINT32_MAX}) {
    Bus bus;bus.delay=0;bus.suppressV5Until=suppress;
    Node rotor(bus,1,false),panel(bus,2,true);
    panel.h.add(1,rotor.endpoint,"drive");panel.h.select(1);
    for(unsigned t=10;t<=2000;t+=10)bus.tick(t);
    assert(panel.h.available() && panel.h.active()->wireVersion==4);
    assert(panel.h.manualAvailable());
    // Loss during discovery must not permanently disable an installed HUD/5.
    for(unsigned t=2010;t<=12000;t+=10)bus.tick(t);
    if(suppress==UINT32_MAX) {
      assert(panel.h.available() && panel.h.active()->wireVersion==4);
      assert(panel.h.manualAvailable());
      assert(panel.c.manualDrive(0,1,1,1,1,.5f,0,false,12010,100,true)==Result::Unavailable);
      continue;
    }
    assert(panel.h.available() && panel.h.active()->wireVersion==5);
    assert(panel.c.manualDrive(0,1,1,1,1,.5f,0,false,12010,100,true)==Result::Deferred);
    bus.tick(12010);assert(rotor.c.axes[0].wanted==1);
    panel.c.manualDrive(0,1,1,2,1,0,0,true,12020);bus.tick(12020);
    assert(!rotor.c.moving());
  }
  {
    Bus bus;bus.delay=0;
    Node rotor(bus,1,false),panel(bus,2,true);
    panel.h.add(1,rotor.endpoint,"drive");panel.h.select(1);
    rotor.h.setMemoryLimited(true);
    for(unsigned t=10;t<=1000;t+=10)bus.tick(t);
    assert(panel.h.active()->live && panel.h.manualAvailable());
    bus.dropReplies=true;
    for(unsigned t=1010;t<=2300;t+=10) {
      bus.tick(t);
      assert(panel.h.available()); // A delayed 500ms stream must not flicker offline.
    }
    bus.dropReplies=false;
    for(unsigned t=2310;t<=3000;t+=10)bus.tick(t);
    assert(panel.h.available());
    bus.dropReplies=true;
    for(unsigned t=3010;t<=5500;t+=10)bus.tick(t);
    assert(!panel.h.available()); // Real loss still expires; tickets stay bounded.
  }
  for (unsigned delay : {150u, 350u, 450u}) {
    Bus bus;
    bus.delay = delay;
    Node rotor(bus, 1, false), panel(bus, 2, true);
    assert(panel.h.add(1, rotor.endpoint, "drive"));
    assert(panel.h.select(1));
    unsigned offline = 0;
    for (unsigned t = 10; t <= 12000; t += 10) {
      bus.tick(t);
      if (t >= 4000 && !panel.h.available()) {
        ++offline;
      }
    }
    if (offline) {
      std::cerr << "one-way delay=" << delay << "ms; offline samples=" << offline << "\n";
    }
    assert(!offline && panel.h.manualAvailable());
    // A genuinely old packet must not renew freshness after the challenge expires.
    auto old = bus.queue;
    bus.queue.clear();
    bus.tick(15000);
    const auto received = panel.h.active()->receivedAt;
    for (const auto &packet : old) {
      if (packet.to == panel.endpoint) {
        panel.h.receive(packet.from, packet.data.data(), packet.data.size(), 15000);
      }
    }
    assert(panel.h.active()->receivedAt == received && !panel.h.available());
  }
  std::cout
      << "PASS: 300/700/900ms RTT HUD recovery and continuous freshness; expired challenges remain rejected\n";
}
