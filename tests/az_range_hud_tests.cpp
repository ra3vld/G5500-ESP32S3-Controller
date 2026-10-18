#include "motion_fixture.hpp"
#include "hud.hpp"
#include "hud_input.hpp"
#include "api.hpp"
#include <cassert>
#include <deque>
#include <vector>
#include <iostream>
#include <algorithm>
using namespace forkrot;
struct Node;
struct PacketWire {
  HudEndpoint source, destination;
  std::vector<uint8_t> bytes;
};
struct Bus {
  uint32_t now = 0;
  std::vector<Node *> nodes;
  std::deque<PacketWire> packets;
  unsigned queries = 0;
  bool drop = false;
  void pump();
  void tick(uint32_t);
};
struct Node : HudTransport {
  Bus &bus;
  HudEndpoint endpoint;
  Controller controller = forkrot::testController();
  Protocol protocol{controller};
  Hud hud{controller, protocol};
  unsigned maxVersion = 4;
  unsigned selectionChanges = 0;
  void selectionChanged() override { ++selectionChanges; }
  Node(Bus &b, uint64_t id, uint32_t address, bool remote = false) : bus(b), endpoint{address, 4534, false} {
    hud.config.role = remote ? DeviceRole::Hud : DeviceRole::Controller;
    hud.begin(id, id + 1000);
    hud.setTransport(this);
    if (!remote)
      controller.feedback(20, 30, true, true);
    bus.nodes.push_back(this);
  }
  bool sendHud(const HudEndpoint &e, const uint8_t *p, size_t n) override {
    if (n>3 && unsigned(p[3]-'0')>maxVersion) return true;
    if (n > 4 && p[4] == 2)
      ++bus.queries;
    if (!bus.drop)
      bus.packets.push_back({endpoint, e, {p, p + n}});
    return true;
  }
  void save(Node &n) { assert(hud.add(n.hud.id, n.endpoint, "Rotator")); }
};
void Bus::pump() {
  unsigned budget = 10000;
  while (!packets.empty()) {
    assert(budget--);
    auto p = std::move(packets.front());
    packets.pop_front();
    for (auto *n : nodes)
      if (p.destination.address == 0xffffffffu || p.destination == n->endpoint)
        n->hud.receive(p.source, p.bytes.data(), p.bytes.size(), now);
  }
}
void Bus::tick(uint32_t at) {
  now = at;
  for (auto *n : nodes) {
    n->hud.service(now);
    n->controller.tick(now);
  }
  pump();
}
int main() {
  for (const auto &range : {AzRange{-60,390},{50,500},{-700,-250},{300,390},{-3600,0},{3600,7200}}) {
    Bus bus;Node rotor(bus,1,1), panel(bus,2,2,true);
    rotor.controller.setRange(range,AZ_CUSTOM);rotor.controller.feedback(range.start,20,true,true);
    panel.save(rotor);panel.hud.select(1);bus.tick(100);
    assert(panel.hud.available() && panel.hud.active()->wireVersion==4);
    assert(panel.controller.azRange==range && panel.controller.rangeRevision==rotor.controller.rangeRevision);
    assert(panel.controller.target({range.end,0,true,false},false,100)==Result::Deferred);
    const auto request=panel.hud.requestId();bus.tick(110);
    assert(panel.hud.result(request).result==Result::Ok && rotor.controller.axes[0].target==range.end);
    bus.tick(600);assert(panel.hud.active()->manual);
    bus.tick(900);assert(panel.controller.azRange==range);
    // Delay a valid HUD/2 target across a geometry change.
    panel.controller.target({range.end,0,true,false},false,900);
    const auto stale=panel.hud.requestId();panel.hud.service(910);
    rotor.controller.setRange({50,500},AZ_CUSTOM,911);
    if (range==AzRange{50,500}) rotor.controller.setRange({51,501},AZ_CUSTOM,911);
    rotor.controller.feedback(rotor.controller.minimum,20,true,true);
    const auto target=rotor.controller.axes[0].target;
    bus.now=912;bus.pump();
    assert(!rotor.controller.moving() && rotor.controller.axes[0].target==target);
    assert(panel.hud.result(stale).result==Result::Cancelled);
    assert(panel.controller.rangeRevision==rotor.controller.rangeRevision);
    panel.controller.stopNow(-1,913);bus.tick(914);assert(!rotor.controller.moving());
  }
  { // New HUD falls back to a real HUD/1 wire image for a legacy standard-range peer.
    Bus bus;Node rotor(bus,1,1), panel(bus,2,2,true);rotor.maxVersion=1;
    panel.save(rotor);panel.hud.select(1);bus.tick(100);assert(!panel.hud.available());
    bus.tick(200);bus.tick(300);bus.tick(400);assert(panel.hud.available() && panel.hud.active()->wireVersion==1);
    assert(panel.controller.minimum==0 && panel.controller.maximum==450);
    panel.controller.target({100,0,true,false},false,401);bus.tick(410);
    assert(rotor.controller.axes[0].target==100);
  }
  { // A legacy HUD must not keep controlling after the controller switches to custom.
    Bus bus;Node rotor(bus,1,1), panel(bus,2,2,true);panel.maxVersion=1;
    panel.save(rotor);panel.hud.select(1);bus.tick(100);bus.tick(200);bus.tick(300);bus.tick(400);
    assert(panel.hud.available() && panel.hud.active()->wireVersion==1);
    rotor.controller.setRange({-60,390},AZ_CUSTOM,401);rotor.controller.feedback(0,20,true,true);
    panel.controller.target({100,0,true,false},false,401);bus.tick(410);
    assert(!rotor.controller.moving() && !rotor.controller.axes[0].targetKnown);
    rotor.controller.jog(0,1,false,411);rotor.controller.tick(411);assert(rotor.controller.moving());
    panel.controller.stopNow(-1,412);bus.tick(420);assert(!rotor.controller.moving());
  }
  for (unsigned oldVersion : {1u,2u,3u}) {
    Bus bus;Node rotor(bus,1,1),panel(bus,2,2,true);panel.maxVersion=oldVersion;
    panel.save(rotor);panel.hud.select(1);bus.tick(100);bus.tick(200);bus.tick(300);bus.tick(400);
    assert(panel.hud.available() && panel.hud.active()->wireVersion==oldVersion);
    rotor.controller.setElRange({-30,210},EL_CUSTOM,401);rotor.controller.feedback(20,10,true,true);
    panel.controller.target({100,100,true,true},false,402);bus.tick(410);
    assert(!rotor.controller.moving()); // Old geometry and old protocol cannot start either axis.
    if(oldVersion==3) {
      bus.tick(600);assert((panel.controller.elRange==RotationRange{-30,210}));
      assert(panel.controller.elProfile==EL_CUSTOM);
      assert(panel.controller.target({0,-20,false,true},false,601)==Result::Deferred);
      bus.tick(610);assert(rotor.controller.axes[1].target==-20);
    }
    rotor.controller.jog(1,1,false,700);rotor.controller.tick(700);
    panel.controller.stopNow(-1,701);bus.tick(710);assert(!rotor.controller.moving());
  }
  std::cout<<"PASS: HUD/4 AZ/EL ranges/revisions, delayed-command rejection, HUD/1 fallback and STOP compatibility\n";
}
