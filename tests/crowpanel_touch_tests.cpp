// Hold-to-run control: axes, ownership, stale input and remote packet ordering.
#include "control.hpp"
#include "crowpanel_touch.hpp"
#include "crowpanel_knob.hpp"
#include "hud.hpp"
#include "hud_input.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <deque>
#include <iostream>
#include <vector>
using namespace forkrot;
static void core() {
  Controller c;
  c.feedback(50, 50, true, true);
  assert(!c.displayedTarget().hasAz && !c.displayedTarget().hasEl);
  c.target({100, 0, true, false}, false, 0);
  assert(c.displayedTarget().hasAz && !c.displayedTarget().hasEl);
  c.target({0, 90, false, true}, false, 0);
  assert(c.displayedTarget().hasAz && c.displayedTarget().hasEl);
  auto run = [&](uint32_t gesture, uint32_t serial, unsigned mask, float az, float el, bool release,
                 uint32_t at) {
    return c.manualDrive(1, 2, gesture, serial, mask, az, el, release, at);
  };
  assert(run(1, 1, 1, -.25f, 0, false, 10) == Result::Ok);
  assert(!c.axes[0].tracking && c.axes[0].wanted == -1 && c.axes[1].tracking);
  assert(run(1, 2, 3, -.5f, 1, false, 20) == Result::Ok);
  assert(c.axes[0].wanted == -1 && c.axes[1].wanted == 1 && !c.axes[1].tracking);
  assert(run(1, 3, 3, 0, 1, false, 30) == Result::Ok && c.axes[0].wanted == 0 &&
         c.axes[1].wanted == 1);
  c.target({120, 0, true, false}, false, 40);
  run(1, 4, 3, -1, 0, false, 50);
  assert(c.axes[0].tracking && c.axes[0].target == 120 && c.axes[1].wanted == 0);
  run(1, 5, 3, 0, 0, true, 60);
  assert(c.axes[0].tracking); // Release cannot stop the newer target.
  assert(run(1, 6, 1, 1, 0, false, 70) == Result::Cancelled);
  c.stop();
  c.executeDisplayed(80);
  assert(c.axes[0].target == 120 && c.axes[1].target == 90 && c.axes[1].tracking);
  c.target({130, 0, true, false}, true, 90);
  c.executeDisplayed(100);
  assert(!c.pendingValid && c.axes[0].target == 130 && c.axes[1].target == 90);
  run(2, 7, 3, 1, -1, false, 110);
  c.tick(1110);
  assert(c.axes[0].wanted == 0 && c.axes[1].wanted == 0);
  assert(run(2, 8, 3, 1, -1, false, 1111) == Result::Cancelled);
  run(3, 9, 1, 1, 0, true, 1120); // Release arriving before the first start closes the gesture.
  assert(run(3, 8, 1, 1, 0, false, 1121) == Result::Cancelled);
  assert(run(3, 10, 1, 1, 0, false, 1122) == Result::Cancelled);
  c.feedback(450, 0, true, true);
  run(4, 11, 3, 1, -1, false, 1130);
  c.tick(1130);
  assert(!c.axes[0].wanted && !c.axes[1].wanted);
  c.feedback(100, 90, true, true);
  for (auto &s : c.speeds) {
    s.speedPercent = 80;
    s.startSpeedPercent = 100;
    s.accelerationMs = 0;
    s.decelerationMs = 0;
  }
  run(5, 12, 3, .25f, -.75f, false, 1140);
  c.tick(1440);
  if (c.variableSpeed(0)) {
    assert(std::abs(c.axes[0].speedOutputPercent - 20) < .01f);
  }
  if (c.variableSpeed(1)) {
    assert(std::abs(c.axes[1].speedOutputPercent - 60) < .01f);
  }
  assert(c.speeds[0].speedPercent == 80 && c.speeds[1].speedPercent == 80);
  run(5, 13, 3, 0, 0, true, 1450);
  c.jog(0, 1, false, 1460);
  c.tick(1760);
  if (c.variableSpeed(0)) {
    assert(c.axes[0].speedOutputPercent == 80);
  }
  // A pending sequence is still a sequence after tapping the displayed goals.
  c.stop();
  const Target points[] = {{150, 70, true, true}, {180, 80, true, true}};
  assert(c.sequence(points, 2, 1000, 1800) == Result::Pending);
  c.executeDisplayed(1810);
  assert(c.sequenceCount() == 2);
}
static void touch() {
  using namespace crowpanel;
  TouchLayout l;
  l.dualAxis = true;
  l.current = {100, 200, 280, 60};
  l.target = {100, 320, 280, 36};
  l.arrows[0] = {40, 210, 44, 44};
  l.arrows[1] = {396, 210, 44, 44};
  l.arrows[2] = {218, 154, 44, 44};
  l.arrows[3] = {218, 262, 44, 44};
  TouchGesture g;
  auto e = g.sample(true, 240, 230, l);
  assert(e.manual && !e.mask);
  e = g.sample(true, 252, 218, l);
  assert(e.manual && !e.mask);
  e = g.sample(true, 296, 174, l);
  assert(e.mask == 3 && std::abs(e.az - .5f) < .001f && std::abs(e.el - .5f) < .001f);
  e = g.sample(true, 470, 230, l);
  assert(e.mask == 3 && e.az == 1 && e.el == 0 && e.action == TouchAction::None);
  e = g.sample(false, 0, 0, l);
  assert(e.release && e.mask == 3);
  e = g.sample(true, 240, 330, l);
  assert(e.action == TouchAction::None && !e.manual);
  e = g.sample(false, 0, 0, l);
  assert(e.action == TouchAction::Confirm);
  g.sample(true, 240, 330, l);
  g.sample(true, 265, 330, l);
  assert(g.sample(false, 0, 0, l).action == TouchAction::None);
  e = g.sample(true, 60, 230, l);
  assert(e.manual && e.az == -1 && e.mask == 1);
  e = g.sample(true, 450, 150, l);
  assert(e.manual && e.az == -1 && e.el == 0); // Arrow captures its direction.
  l.enabled = false;
  e = g.sample(true, 450, 150, l);
  assert(e.release);
  l.enabled = true;
  assert(!g.sample(true, 60, 230, l).manual); // Requires lifting after cancellation.
  g.sample(false, 0, 0, l);
  // A failed read cancels a goal tap and requires a healthy release before rearming.
  g.sample(true, 240, 330, l);
  l.enabled = false;
  assert(g.sample(false, 0, 0, l).action == TouchAction::None);
  l.enabled = true;
  assert(g.sample(true, 240, 330, l).action == TouchAction::None);
  assert(g.sample(false, 0, 0, l).action == TouchAction::None);
  g.sample(true, 240, 330, l);
  assert(g.sample(false, 0, 0, l).action == TouchAction::Confirm);
  l.dualAxis = false;
  e = g.sample(true, 240, 230, l);
  e = g.sample(true, 240, 130, l);
  assert(e.el == 0 && !e.mask);
  g.sample(false, 0, 0, l);
}
static void ring() {
  using namespace crowpanel;
  TouchLayout l;
  TouchGesture g;
  const auto sample = [&](bool down, uint16_t x, uint16_t y, TouchAction expected) {
    const auto e = g.sample(down, x, y, l);
    assert(e.action == expected && !e.manual);
  };
  sample(true, 467, 240, TouchAction::Target); // Tap acts on contact.
  sample(true, 467, 241, TouchAction::None); // Same rounded bearing is not resent.
  sample(true, 400, 400, TouchAction::Target);
  sample(true, 240, 467, TouchAction::Target);
  sample(true, 13, 240, TouchAction::Target);
  sample(true, 236, 13, TouchAction::Target); // Across north: 359 -> 1 degrees.
  sample(true, 244, 13, TouchAction::Target);
  l.target = {100, 60, 280, 40}; // Layout changes cannot steal a ring contact.
  l.arrows[0] = {13, 218, 44, 44};
  sample(true, 240, 80, TouchAction::Target);
  sample(true, 13, 240, TouchAction::Target);
  sample(true, 140, 100, TouchAction::Target); // Cross the mode button on the ring.
  sample(true, 240, 240, TouchAction::None); // Outside ring: keep last target.
  sample(true, 0, 0, TouchAction::None);
  sample(true, 480, 240, TouchAction::None);
  sample(true, 467, 240, TouchAction::Target); // Resume within the same contact.
  sample(false, 0, 0, TouchAction::None);
  sample(true, 467, 240, TouchAction::Target); // A new tap at the same angle acts.
  l.enabled = false;
  sample(true, 240, 467, TouchAction::None); // Fault/selector cancels the contact.
  l.enabled = true;
  sample(true, 240, 467, TouchAction::None);
  sample(false, 0, 0, TouchAction::None);
  sample(true, 240, 467, TouchAction::Target);
  sample(false, 0, 0, TouchAction::None);
  sample(true, 240, 240, TouchAction::None); // Cannot slide into a ring gesture.
  sample(true, 467, 240, TouchAction::None);
  sample(false, 0, 0, TouchAction::None);
}
struct Node;
struct WirePacket {
  HudEndpoint from, to;
  std::vector<uint8_t> data;
};
struct Bus {
  std::vector<Node *> nodes;
  std::deque<WirePacket> packets;
  std::vector<WirePacket> history;
  bool drop = false;
  void tick(uint32_t);
  void deliver(uint32_t);
};
struct Node : HudTransport {
  Bus &bus;
  Controller c;
  Protocol p{c};
  Hud h{c, p};
  HudEndpoint endpoint;
  bool legacy = false;
  Node(Bus &b, int id, bool remote) : bus(b), endpoint{uint32_t(id), 4534, false} {
    h.config.role = remote ? DeviceRole::Hud : DeviceRole::Controller;
    h.begin(id, id + 100);
    h.setTransport(this);
    c.feedback(40, 50, true, true);
    bus.nodes.push_back(this);
  }
  bool sendHud(const HudEndpoint &e, const uint8_t *p, size_t n) override {
    if (!bus.drop && (!legacy || p[4] <= 10)) {
      bus.packets.push_back({endpoint, e, {p, p + n}});
    }
    return true;
  }
  void select(Node &n) {
    assert(h.add(n.h.id, n.endpoint, "rotator"));
    assert(h.select(n.h.id));
  }
};
void Bus::deliver(uint32_t now) {
  unsigned budget = 10000;
  while (!packets.empty()) {
    assert(budget--);
    auto msg = std::move(packets.front());
    packets.pop_front();
    history.push_back(msg);
    for (auto *n : nodes) {
      if (n->endpoint == msg.to && (!n->legacy || msg.data[4] <= 10)) {
        n->h.receive(msg.from, msg.data.data(), msg.data.size(), now);
      }
    }
  }
}
void Bus::tick(uint32_t at) {
  for (auto *n : nodes) {
    n->h.service(at);
  }
  deliver(at);
  for (auto *n : nodes) {
    if (!n->h.isHud()) {
      n->c.tick(at);
    }
  }
}
static void ringTargets() {
  Bus bus;
  Node rotor(bus, 1, false), panel(bus, 2, true);
  rotor.c.feedback(20, 50, true, true);
  panel.select(rotor);
  for (unsigned t = 100; t <= 800; t += 10) {
    bus.tick(t);
  }
  assert(panel.h.available());
  Controller local;
  local.feedback(20, 50, true, true);
  CrowPanelKnob localKnob(local);
  HudKnob remoteKnob(panel.h, panel.c);
  crowpanel::TouchGesture gesture;
  crowpanel::TouchLayout layout;
  uint32_t now = 810;
  for (const auto mode : {Mode::Manual, Mode::Auto}) {
    local.setMode(mode, now);
    rotor.c.setMode(mode, now);
    bus.tick(now);
    const uint16_t points[][2] = {{400, 80}, {467, 240}, {400, 400}, {240, 467}};
    for (const auto &p : points) {
      const auto e = gesture.sample(true, p[0], p[1], layout);
      assert(e.action == crowpanel::TouchAction::Target && !e.manual);
      const float bearing = crowpanel::dialBearing(p[0], p[1]);
      localKnob.setBearing(bearing, now);
      remoteKnob.setBearing(bearing, now);
      bus.tick(now);
      assert(local.axes[0].target == bearing && local.axes[0].tracking && !local.pendingValid);
      assert(rotor.c.axes[0].target == bearing && rotor.c.axes[0].tracking && !rotor.c.pendingValid);
      now += 10;
    }
    assert(gesture.sample(false, 0, 0, layout).action == crowpanel::TouchAction::None);
    assert(local.axes[0].target == 180 && rotor.c.axes[0].target == 180);
  }
}
static void network() {
  Bus bus;
  Node rotor(bus, 1, false), a(bus, 2, true);
  a.select(rotor);
  rotor.c.target({100, 0, true, false}, false, 0);
  rotor.c.target({0, 80, false, true}, false, 0);
  for (unsigned t = 100; t <= 800; t += 10) {
    bus.tick(t);
  }
  assert(a.h.manualAvailable() && a.c.axes[0].targetKnown && a.c.axes[1].targetKnown);
  auto send = [&](unsigned gesture, unsigned seq, unsigned mask, float az, float el, bool release,
                  unsigned at) { a.c.manualDrive(0, 1, gesture, seq, mask, az, el, release, at); };
  const auto before = bus.history.size();
  for (unsigned t = 810; t <= 1110; t += 10) {
    send(1, t, 3, .25f, -.5f, false, t);
    bus.tick(t);
  }
  assert(rotor.c.axes[0].wanted == 1 && rotor.c.axes[1].wanted == -1);
  unsigned packets = 0;
  WirePacket saved;
  for (size_t i = before; i < bus.history.size(); ++i) {
    if (bus.history[i].data[4] == 15) {
      ++packets;
      saved = bus.history[i];
    }
  }
  assert(packets >= 5 && packets <= 7); // Coalescing: at most 20 updates/s.
  send(1, 1200, 3, 0, 0, true, 1120);
  bus.tick(1120);
  assert(!rotor.c.axes[0].wanted && !rotor.c.axes[1].wanted);
  rotor.h.receive(saved.from, saved.data.data(), saved.data.size(), 1121);
  assert(!rotor.c.moving());
  // A new target wins even if an older start is delayed until after it.
  send(2, 1201, 1, -1, 0, false, 1130);
  a.h.service(1130);
  WirePacket delayed;
  for (auto it = bus.packets.begin(); it != bus.packets.end();) {
    if (it->data[4] == 15) {
      delayed = *it;
      it = bus.packets.erase(it);
    } else {
      ++it;
    }
  }
  a.c.target({150, 0, true, false}, false, 1131);
  bus.tick(1140);
  if (!delayed.data.empty()) {
    rotor.h.receive(delayed.from, delayed.data.data(), delayed.data.size(), 1141);
  }
  assert(rotor.c.axes[0].tracking && rotor.c.axes[0].target == 150);
  // Stale local touch input releases instead of being renewed by the network task.
  send(3, 1202, 3, 1, -1, false, 1150);
  bus.tick(1150);
  for (unsigned t = 1160; t <= 1300; t += 10) {
    bus.tick(t);
  }
  assert(!rotor.c.moving());
  assert(a.c.manualDrive(0, 1, 3, 1203, 3, 1, -1, false, 1301) == Result::Cancelled);
  // Total packet loss still expires the drive lease within one second.
  send(4, 1203, 1, 1, 0, false, 1310);
  bus.tick(1310);
  assert(rotor.c.axes[0].wanted == 1);
  bus.drop = true;
  for (unsigned t = 1320; t <= 2320; t += 10) {
    bus.tick(t);
  }
  assert(!rotor.c.moving());
  bus.drop = false;
  // Selection releases the previous device; the new device never receives that gesture.
  for (unsigned t = 2330; t <= 2800; t += 10) {
    bus.tick(t);
  }
  Node other(bus, 3, false);
  assert(a.h.add(other.h.id, other.endpoint, "other"));
  send(5, 1204, 1, 1, 0, false, 2810);
  bus.tick(2810);
  assert(rotor.c.axes[0].wanted == 1);
  assert(a.h.select(other.h.id));
  bus.tick(2820);
  assert(!rotor.c.moving() && !other.c.moving());
  Bus old;
  Node legacy(old, 10, false), modern(old, 11, true);
  legacy.legacy = true;
  modern.select(legacy);
  for (unsigned t = 100; t <= 800; t += 10) {
    old.tick(t);
  }
  assert(modern.h.available() && !modern.h.manualAvailable());
  assert(modern.c.manualDrive(0, 1, 1, 1, 1, 1, 0, false, 810) == Result::Unavailable);
  modern.c.target({80, 0, true, false}, false, 810);
  old.tick(810);
  assert(legacy.c.axes[0].target == 80);
}
int main() {
  core();
  touch();
  ring();
  ringTargets();
  network();
  std::cout
      << "PASS: ring tap/drag local and HUD, dual targets, hold/swipe axes and speed, ownership, lease expiry, coalescing, packet order and legacy compatibility\n";
}
