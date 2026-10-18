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
