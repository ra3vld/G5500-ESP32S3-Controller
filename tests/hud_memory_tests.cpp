#include "hud.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
static bool deny = false;
void *operator new(size_t n) {
  if (deny)
    throw std::bad_alloc();
  if (auto *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
using namespace forkrot;
struct Link : HudTransport {
  struct Frame {
    HudEndpoint source;
    uint8_t data[HUD_FRAME_MAX];
    size_t size = 0;
  };
  HudEndpoint endpoint;
  Link *other = nullptr;
  Frame pending[8];
  unsigned count = 0;
  Hud *hud = nullptr;
  explicit Link(uint32_t address) : endpoint{address, 4534, false} {}
  bool sendHud(const HudEndpoint &, const uint8_t *p, size_t n) override {
    if (other->count == 8)
      return false;
    auto &f = other->pending[other->count++];
    f.source = endpoint;
    f.size = n;
    std::memcpy(f.data, p, n);
    return true;
  }
  void receive(uint32_t now) {
    for (unsigned i = 0; i < count; ++i) {
      auto &f = pending[i];
      hud->receive(f.source, f.data, f.size, now);
    }
    count = 0;
  }
};
int main() {
  Controller motor, mirror;
  Protocol direct(motor), remote(mirror);
  Hud server(motor, direct), client(mirror, remote);
  Link a(1), b(2);
  a.other = &b;
  b.other = &a;
  a.hud = &server;
  b.hud = &client;
  server.setTransport(&a);
  client.setTransport(&b);
  server.begin(1, 100);
  client.config.role = DeviceRole::Hud;
  client.begin(2, 200);
  motor.feedback(30, 40, true, true);
  for (auto &p : motor.presets)
    p.label = std::string(48, 'A');
  direct.rotorName = std::string(31, 'R');
  assert(client.add(1, a.endpoint, "Motor"));
  assert(client.select(1));
  auto tick = [&](uint32_t now) {
    server.service(now);
    client.service(now);
    a.receive(now);
    b.receive(now);
    motor.tick(now);
  };
  tick(100);
  assert(client.available());
  deny = true;
  assert(!client.add(3, {3, 4534, false}, "No memory") && client.catalogError && client.count() == 1);
  for (unsigned i = 0; i < 10000; ++i) {
    const uint32_t now = 200 + i * 100;
    assert(mirror.target({float(60 + i % 100), 0, true, false}, false, now) == Result::Deferred);
    tick(now);
    assert(client.result(client.requestId()).result == Result::Ok);
    mirror.stopNow(-1, now);
    tick(now + 5);
    assert(!motor.moving());
  }
  // Client allocation failure leaves the mirror offline and motion unavailable.
  Controller empty;
  Protocol emptyProtocol(empty);
  Hud failed(empty, emptyProtocol);
  failed.config.role = DeviceRole::Hud;
  failed.begin(3, 300);
  assert(failed.catalogError && !failed.available());
  assert(empty.jog(0, 1, false, 0) == Result::Unavailable);
  deny = false;
  std::cout << "PASS: 10000 HUD poll/command/STOP cycles with long labels and allocations forbidden; "
               "catalogue/client OOM is explicit\n";
}
