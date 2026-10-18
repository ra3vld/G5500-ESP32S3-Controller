#include "motion_fixture.hpp"
#include "hud.hpp"
#include <cassert>
#include <deque>
#include <iostream>
#include <vector>
using namespace forkrot;
struct Node;
struct WirePacket { HudEndpoint from, to; std::vector<uint8_t> bytes; };
struct Bus {
  uint32_t now = 0, blocked = 0;
  std::vector<Node *> nodes;
  std::deque<WirePacket> queue;
  std::vector<uint32_t> failures;
  unsigned healthyTelemetry = 0;
  void tick(uint32_t at);
};
struct Node : HudTransport {
  Bus &bus;
  Controller c = forkrot::testController();
  Protocol p{c};
  Hud h{c, p};
  HudEndpoint endpoint;
  Node(Bus &b, uint32_t id, bool remote) : bus(b), endpoint{id, 4534, false} {
    h.config.role = remote ? DeviceRole::Hud : DeviceRole::Controller;
    h.begin(id, id + 100);
    h.setTransport(this);
    c.feedback(20, 30, true, true);
    bus.nodes.push_back(this);
  }
  bool sendHud(const HudEndpoint &to, const uint8_t *data, size_t size) override {
    const bool telemetry = data[4] == 8 || data[4] == 14;
    if (endpoint.address == 1 && telemetry && to.address == bus.blocked) {
      bus.failures.push_back(bus.now);
      return false;
    }
    if (endpoint.address == 1 && telemetry && to.address == 3) ++bus.healthyTelemetry;
    bus.queue.push_back({endpoint, to, {data, data + size}});
    return true;
  }
  void select(Node &other) {
    assert(h.add(other.h.id, other.endpoint, "drive"));
    assert(h.select(other.h.id));
  }
};
void Bus::tick(uint32_t at) {
  now = at;
  for (auto *node : nodes) node->h.service(at);
  unsigned budget = 1000;
  while (!queue.empty()) {
    assert(budget--);
    auto packet = std::move(queue.front()); queue.pop_front();
    for (auto *node : nodes)
      if (node->endpoint == packet.to)
        node->h.receive(packet.from, packet.bytes.data(), packet.bytes.size(), at);
  }
}
int main() {
  Bus bus;
  Node rotor(bus, 1, false), blocked(bus, 2, true), healthy(bus, 3, true);
  blocked.select(rotor); healthy.select(rotor);
  for (uint32_t t = 10; t < 500; t += 5) bus.tick(t);
  assert(blocked.h.active()->live && healthy.h.active()->live);
  bus.blocked = 2;
  const auto before = bus.healthyTelemetry;
  for (uint32_t t = 500; t < 2500; t += 5) {
    rotor.c.feedback(20.f + (t - 500) / 100.f, 30, true, true);
    bus.tick(t);
  }
  std::cout << "failed attempts=" << bus.failures.size() << "; healthy updates="
            << bus.healthyTelemetry - before << '\n';
  // Repeated renewals and new revisions must not reset a blocked sender's backoff.
  assert(!bus.failures.empty() && bus.failures.size() <= 16);
  for (size_t i = 1; i < bus.failures.size(); ++i)
    assert(bus.failures[i] - bus.failures[i - 1] >= 20);
  assert(bus.healthyTelemetry - before >= 20 && bus.healthyTelemetry - before <= 24 && healthy.h.available());
  // STOP and its ACK are independent of telemetry backpressure.
  blocked.c.stopNow(-1, 2500); const auto request = blocked.h.requestId();
  bus.tick(2500); bus.tick(2505);
  assert(blocked.h.result(request).result == Result::Ok && !rotor.c.moving());
  bus.blocked = 0;
  rotor.c.feedback(77, 30, true, true);
  for (uint32_t t = 2510; t <= 2800; t += 5) bus.tick(t);
  assert(blocked.h.active()->live && blocked.h.available() && blocked.c.axes[0].current == 77);
  const auto healthyBeforeMode = bus.healthyTelemetry;
  rotor.c.setMode(Mode::Auto, 2805);
  bus.tick(2805);
  assert(bus.healthyTelemetry == healthyBeforeMode + 1); // Control changes bypass position coalescing.
  std::cout << "PASS: bounded telemetry failure retries across renewals, independent peer progress, latest-state recovery and immediate STOP ACK\n";
}
