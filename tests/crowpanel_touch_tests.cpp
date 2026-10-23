#include "motion_fixture.hpp"
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
  Controller c = forkrot::testController();
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
    s.accelerationMs = 100;
    s.decelerationMs = 100;
  }
  run(5, 12, 3, .25f, -.75f, false, 1140);
  for (unsigned at = 1140; at <= 1740; at += 20) c.tick(at);
  if (c.variableSpeed(0)) {
    assert(std::abs(c.axes[0].speedOutputPercent - 23) < .01f);
  }
  if (c.variableSpeed(1)) {
    assert(std::abs(c.axes[1].speedOutputPercent - 61) < .01f);
  }
  assert(c.speeds[0].speedPercent == 80 && c.speeds[1].speedPercent == 80);
  run(5, 13, 3, 0, 0, true, 1750);
  c.jog(0, 1, false, 1760);
  for (unsigned at = 1760; at <= 2160; at += 20) c.tick(at);
  if (c.variableSpeed(0)) {
    assert(c.axes[0].speedOutputPercent == 80);
  }
  // A pending sequence is still a sequence after tapping the displayed goals.
  c.stop();
  const Target points[] = {{150, 70, true, true}, {180, 80, true, true}};
  assert(c.sequence(points, 2, 1000, 2200) == Result::Pending);
  c.executeDisplayed(2210);
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
  assert(e.mask == 3 && std::abs(e.az - .25f) < .001f && std::abs(e.el - .25f) < .001f);
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
static void progressiveSwipe() {
  using namespace crowpanel;
  TouchLayout layout;
  layout.current = {100, 200, 280, 60};
  layout.dualAxis = true;
  for (unsigned axis : {0u, 1u}) for (int direction : {-1, 1}) {
    TouchGesture gesture;
    gesture.sample(true, 240, 240, layout);
    float previous = 0, previousStep = 0;
    for (int distance = 0; distance <= 200; ++distance) {
      const auto event = gesture.sample(true, axis ? 240 : 240 + direction * distance,
                                        axis ? 240 - direction * distance : 240, layout);
      const float value = axis ? event.el : event.az;
      const float magnitude = std::abs(value);
      assert(event.manual && event.absoluteSpeed && !event.release && event.action == TouchAction::None);
      assert((axis ? event.az : event.el) == 0);
      assert(magnitude >= previous && magnitude <= 1);
      if (distance <= 12) assert(value == 0);
      else assert(value * direction > 0);
      if (distance >= 13 && distance <= 100) {
        const float step = magnitude - previous;
        assert(step + 1e-6f >= previousStep); // Sensitivity increases with displacement.
        previousStep = step;
      }
      if (distance == 34) assert(std::abs(magnitude - .0625f) < 1e-6f);
      if (distance == 56) assert(std::abs(magnitude - .25f) < 1e-6f);
      if (distance == 78) assert(std::abs(magnitude - .5625f) < 1e-6f);
      if (distance >= 100) assert(magnitude == 1);
      previous = magnitude;
    }
    const auto center = gesture.sample(true, 240, 240, layout);
    assert(center.manual && center.az == 0 && center.el == 0);
    assert(gesture.sample(false, 240, 240, layout).release);
  }
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
static void fullscreenSwipe() {
  using namespace crowpanel;
  for (bool dualAxis : {false, true}) {
    for (uint16_t startY : {230, 330}) { // Current angles and target row.
      TouchLayout l;
      l.dualAxis = dualAxis;
      l.current = {100, 200, 280, 60};
      l.target = {100, 320, 280, 36};
      TouchGesture g;
      auto e = g.sample(true, 240, startY, l);
      assert(e.action == TouchAction::None && !e.release && !e.mask);
      assert(e.manual == (startY == 230));
      e = g.sample(true, 252, startY - 12, l);
      assert(e.action == TouchAction::None && !e.mask); // Still a possible tap.
      assert(e.manual == (startY == 230));
      e = g.sample(true, 296, startY - 56, l);
      assert(e.manual && !e.release && e.gesture && e.action == TouchAction::None);
      assert(std::abs(e.az - .25f) < .001f);
      assert(e.el == (dualAxis ? .25f : 0.f));
      const auto gesture = e.gesture;
      const uint8_t mask = dualAxis ? 3 : 1;
      // Redraws may put every other control beneath the finger.
      l.current = {};
      l.target = {0, 0, 480, 480};
      for (auto &arrow : l.arrows) arrow = {0, 0, 480, 480};
      l.dualAxis = !dualAxis;
      const auto move = [&](uint16_t x, uint16_t y, float az, float el) {
        const auto event = g.sample(true, x, y, l);
        assert(event.manual && !event.release && event.gesture == gesture);
        assert(event.action == TouchAction::None && g.pressed() == TouchAction::None);
        assert(event.mask == mask && event.az == az && event.el == (dualAxis ? el : 0.f));
      };
      move(479, startY, 1, 0);
      move(0, startY, -1, 0); // Reverse without lifting.
      move(240, 0, 0, 1);
      move(240, 479, 0, -1);
      move(240, 100, 0, 1); // Mode button.
      move(240, 50, 0, 1); // HUD device selector.
      move(479, 0, 1, 1);
      move(0, 479, -1, -1);
      move(240, startY, 0, 0); // Stop in the original dead zone, retaining capture.
      move(479, startY, 1, 0);
      e = g.sample(false, 0, 0, l);
      assert(e.manual && e.release && e.gesture == gesture && e.mask == mask);
      assert(e.action == TouchAction::None); // Never confirms the goal after a swipe.
      for (auto &arrow : l.arrows) arrow = {};
      g.sample(true, 240, 330, l);
      assert(g.sample(false, 0, 0, l).action == TouchAction::Confirm);
    }
  }
}
static void idleTouchFrames() {
  using namespace crowpanel;
  // Recorded idle CST8xx frame: coordinates/event are unused when no finger is present.
  const uint8_t idle[5] = {0, 0xff, 0xff, 0xff, 0xff};
  assert(touchFrameHealthy(idle));
  uint16_t x = 240, y = 230;
  assert(!decodeTouch(idle, x, y));
  TouchLayout layout;
  layout.dualAxis = true;
  layout.current = {100, 200, 280, 60};
  TouchGesture g;
  layout.enabled = touchFrameHealthy(idle);
  g.sample(false, x, y, layout);
  layout.enabled = true;
  auto event = g.sample(true, 240, 230, layout);
  assert(event.manual && !event.release); // The first contact after idle is usable.
  event = g.sample(true, 340, 130, layout);
  assert(event.manual && event.az == 1 && event.el == 1);
  layout.enabled = touchFrameHealthy(idle);
  event = g.sample(false, x, y, layout);
  assert(event.manual && event.release && event.mask == 3);
  layout.enabled = true;
  assert(g.sample(true, 240, 230, layout).manual); // No extra tap is needed to rearm.
  const uint8_t multiple[5] = {2, 0, 240, 0, 230};
  const uint8_t invalid[5] = {1, 0xc0, 240, 0, 230};
  const uint8_t outside[5] = {1, 1, 224, 0, 230};
  assert(!touchFrameHealthy(multiple) && !touchFrameHealthy(invalid) && !touchFrameHealthy(outside));
  layout.enabled = touchFrameHealthy(invalid);
  assert(g.sample(true, 340, 130, layout).release); // Active invalid input still stops.
  layout.enabled = touchFrameHealthy(idle);
  g.sample(false, x, y, layout);
  assert(g.sample(true, 240, 230, layout).manual);
}
static void speedScreenTouch() {
  using namespace crowpanel;
  TouchLayout l; l.current = {100,200,280,60}; l.dualAxis = true;
  TouchGesture g;
  g.sample(true,240,230,l,100);
  assert(g.sample(false,0,0,l,140).action == TouchAction::None);
  g.sample(true,242,232,l,250);
  auto e = g.sample(false,0,0,l,280);
  assert(e.action == TouchAction::SpeedOpen && e.release && !e.mask);
  // A drag, a slow tap, and a disabled layout never become a double tap.
  for (unsigned variant : {0u,1u,2u}) {
    TouchGesture input;
    input.sample(true,240,230,l,100);
    if (variant == 0) input.sample(true,270,230,l,120);
    if (variant == 2) l.enabled = false;
    input.sample(false,0,0,l,variant == 1 ? 400 : 140);
    l.enabled = true; input.sample(false,0,0,l,410);
    input.sample(true,240,230,l,420);
    assert(input.sample(false,0,0,l,450).action == TouchAction::None);
  }
  l.speedScreen = true; l.speedEnabled[0] = l.speedEnabled[1] = true;
  for (unsigned axis : {0u,1u}) {
    e = g.sample(true,100,227+95*axis,l,500);
    assert(e.action == TouchAction::SpeedValue && e.axis == axis && e.percent == 5 && !e.manual);
    e = g.sample(true,240,227+95*axis,l,510); assert(e.percent == 53);
    e = g.sample(true,479,0,l,520); assert(e.percent == 100 && e.axis == axis);
    assert(g.sample(false,0,0,l,530).action == TouchAction::None);
  }
  l.speedEnabled[1] = false;
  assert(g.sample(true,240,322,l,600).action == TouchAction::None);
  g.sample(false,0,0,l,610);
  g.sample(true,240,390,l,620);
  assert(g.sample(false,0,0,l,650).action == TouchAction::SpeedClose);
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
  Controller c = forkrot::testController();
  Protocol p{c};
  Hud h{c, p};
  HudEndpoint endpoint;
  bool legacy = false;
  uint8_t maxVersion = 5;
  Node(Bus &b, int id, bool remote) : bus(b), endpoint{uint32_t(id), 4534, false} {
    h.config.role = remote ? DeviceRole::Hud : DeviceRole::Controller;
    h.begin(id, id + 100);
    h.setTransport(this);
    c.feedback(40, 50, true, true);
    bus.nodes.push_back(this);
  }
  bool sendHud(const HudEndpoint &e, const uint8_t *p, size_t n) override {
    if (!bus.drop && p[3] <= char('0'+maxVersion) && (!legacy || p[4] <= 10)) {
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
      if (n->endpoint == msg.to && msg.data[3] <= char('0'+n->maxVersion) && (!n->legacy || msg.data[4] <= 10)) {
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
static void webKeyboardLease() {
  Bus bus;Node rotor(bus,1,false),panel(bus,2,true);panel.select(rotor);
  for(unsigned t=0;t<=800;t+=10)bus.tick(t);
  assert(panel.h.manualAvailable());
  for(unsigned t=810;t<=1810;t+=10){
    if((t-810)%200==0)assert(panel.c.manualDrive(7,8,1,t,3,1,-1,false,t,1000)==Result::Deferred);
    bus.tick(t);assert(rotor.c.axes[0].wanted==1&&rotor.c.axes[1].wanted==-1);
  }
  // A web renewal is valid between 200 ms packets; touch still defaults to 100 ms.
  for(unsigned t=1820;t<=2800;t+=10)bus.tick(t);
  bus.tick(2810);assert(!rotor.c.moving());
  // A reloaded browser starts its own gesture counter at one; the HUD maps it.
  assert(panel.c.manualDrive(9,10,1,1,3,-1,0,false,2820,1000)==Result::Deferred);
  bus.tick(2820);assert(rotor.c.axes[0].wanted==-1);
  assert(panel.c.manualDrive(9,10,1,3,3,0,0,true,2830,1000)==Result::Deferred);
  bus.tick(2830);assert(!rotor.c.moving());
  assert(panel.c.manualDrive(9,10,1,2,3,-1,0,false,2840,1000)==Result::Cancelled);
  assert(panel.c.manualDrive(11,12,1,1,3,1,0,false,2850,1000)==Result::Deferred);
  bus.tick(2850);assert(rotor.c.axes[0].wanted==1);
  assert(panel.c.manualDrive(13,14,1,1,3,-1,0,false,2860,1000)==Result::Deferred);
  bus.tick(2860);assert(rotor.c.axes[0].wanted==-1);
  assert(panel.c.manualDrive(11,12,1,2,3,0,0,true,2870,1000)==Result::Cancelled);
  bus.tick(2870);assert(rotor.c.axes[0].wanted==-1); // Old page's release cannot stop its replacement.
  assert(panel.c.manualDrive(13,14,1,2,3,0,0,true,2880,1000)==Result::Deferred);
  bus.tick(2880);assert(!rotor.c.moving());
  // Touch has a separate logical gesture namespace and keeps its 100 ms lease.
  assert(panel.c.manualDrive(0,1,1,1,3,1,0,false,2890)==Result::Deferred);
  bus.tick(2890);assert(rotor.c.axes[0].wanted==1);
  bus.tick(2990);assert(!rotor.c.moving());
}
static void inputAfterRenderPause() {
  // A fresh physical sample must precede HUD expiry after a slow display frame.
  for (unsigned mask : {1u, 2u, 3u}) {
    Bus bus;
    Node rotor(bus, 1, false), panel(bus, 2, true);
    panel.select(rotor);
    for (unsigned t = 100; t <= 800; t += 10) bus.tick(t);
    assert(panel.h.manualAvailable());
    uint32_t now = 810, serial = 0;
    const auto sample = [&](uint32_t gesture, float direction, bool release = false) {
      return panel.c.manualDrive(0, 1, gesture, ++serial, mask,
                                mask & 1 ? direction : 0, mask & 2 ? direction : 0,
                                release, now);
    };
    assert(sample(1, 1) == Result::Deferred);
    bus.tick(now);
    for (unsigned pause : {102u, 140u, 102u}) {
      now += pause; // HUD's previous service time is stale; the finger is freshly read now.
      assert(sample(1, -1) == Result::Deferred);
      bus.tick(now);
      for (unsigned axis = 0; axis < 2; ++axis)
        if (mask & (1u << axis)) assert(rotor.c.axes[axis].wanted == -1);
    }
    now += 10;
    assert(sample(1, 0, true) == Result::Deferred);
    bus.tick(now);
    assert(!rotor.c.moving());
    now += 10;
    assert(sample(2, 1) == Result::Deferred);
    bus.tick(now);
    now += 100;
    bus.tick(now); // No physical sample: the unchanged 100 ms guard must still stop.
    assert(!rotor.c.moving());
    now += 10;
    assert(sample(2, 1) == Result::Cancelled); // An expired gesture cannot replay.
  }
}
static void fullscreenSwipeControl() {
  using namespace crowpanel;
  for (bool remote : {false, true}) {
    Bus bus;
    Node rotor(bus, 1, false), panel(bus, 2, true);
    for (auto &speed : rotor.c.speeds) {
      speed.accelerationMs = speed.decelerationMs = 100;
    }
    panel.select(rotor);
    rotor.c.target({100, 80, true, true}, false, 0);
    for (unsigned t = 100; t <= 800; t += 10) bus.tick(t);
    assert(panel.h.manualAvailable());
    auto &input = remote ? panel.c : rotor.c;
    TouchGesture g;
    uint32_t now = 800, serial = 0;
    for (uint16_t startY : {230, 330}) {
      TouchLayout l;
      l.dualAxis = true;
      l.current = {100, 200, 280, 60};
      l.target = {100, 320, 280, 36};
      const auto sample = [&](bool down, uint16_t x, uint16_t y) {
        now += 10;
        const auto e = g.sample(down, x, y, l);
        assert(e.action == TouchAction::None);
        if (e.manual) {
          assert(input.manualDrive(0, 1, e.gesture, ++serial, e.mask, e.az, e.el, e.release,
                                   now, 100, e.absoluteSpeed) == (remote ? Result::Deferred : Result::Ok));
        }
        bus.tick(now);
      };
      sample(true, 240, startY);
      const auto hold = [&](uint16_t x, uint16_t y, int az, int el) {
        // Include the S-brake, mandatory 300 ms dead time and S-start.
        for (unsigned i = 0; i < 60; ++i) sample(true, x, y);
        assert(rotor.c.axes[0].wanted == az && rotor.c.axes[1].wanted == el);
        assert(rotor.c.axes[0].direction == az && rotor.c.axes[1].direction == el);
      };
      hold(479, 0, 1, 1);
      hold(0, 479, -1, -1);
      hold(240, 100, 0, 1); // Pass over mode and HUD controls while reversing EL.
      hold(240, 50, 0, 1);
      hold(240, startY, 0, 0);
      hold(479, startY, 1, 0); // Resume after returning to the dead zone.
      sample(false, 0, 0);
      assert(!rotor.c.axes[0].wanted && !rotor.c.axes[1].wanted);
      for (unsigned i = 0; i < 40; ++i) sample(false, 0, 0);
      assert(!rotor.c.moving() && !rotor.c.axes[0].tracking && !rotor.c.axes[1].tracking);
      assert(rotor.c.axes[0].target == 100 && rotor.c.axes[1].target == 80);
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
  Controller local = forkrot::testController();
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
static void oldSpeedCompatibility() {
  Bus bus; Node motor(bus,1,false),panel(bus,2,true); motor.maxVersion=4; panel.select(motor);
  for (unsigned t=100;t<=2000;t+=10) bus.tick(t);
  assert(panel.h.available() && panel.h.active()->wireVersion == 4 && panel.h.manualAvailable());
  assert(!panel.c.runtimeSpeedAvailable(0));
  assert(panel.c.setRuntimeSpeed(0,50,2010) == Result::Unavailable);
  assert(panel.c.manualDrive(0,1,1,1,1,1,0,false,2010,100,true) == Result::Unavailable);
  bus.tick(2010); assert(!motor.c.moving());
  assert(panel.c.manualDrive(0,1,2,2,1,1,0,false,2020) == Result::Deferred);
  bus.tick(2020); assert(motor.c.axes[0].wanted == 1);
  panel.c.manualDrive(0,1,2,3,1,0,0,true,2030); bus.tick(2030);
  assert(!motor.c.moving());
}
static void fullRangeAndSpeedCommands() {
  for (bool remote : {false,true}) {
    Bus bus; Node motor(bus,1,false), panel(bus,2,true); panel.select(motor);
    motor.c.hardware.simulation = true;
    for (auto &m : motor.c.motionSettings) { m.kickEnabled = false; m.stallMs = 60000; m.outputLimitPercent = 80; }
    for (auto &speed : motor.c.speeds) { speed.speedPercent = 15; speed.accelerationMs = speed.decelerationMs = 100; }
    for (unsigned t=100;t<=800;t+=10) bus.tick(t);
    assert(panel.h.active()->wireVersion == 5);
    auto &input = remote ? panel.c : motor.c;
    assert(input.runtimeSpeedAvailable(0) && input.runtimeSpeedAvailable(1));
    assert(input.setRuntimeSpeed(0,16,810) == (remote ? Result::Deferred : Result::Ok));
    assert(input.setRuntimeSpeed(1,24,810) == (remote ? Result::Deferred : Result::Ok));
    bus.tick(820);
    for (unsigned t=830;t<=1100;t+=10) bus.tick(t);
    assert(motor.c.runtimeSpeed(0)==16 && motor.c.runtimeSpeed(1)==24);
    assert(panel.c.runtimeSpeed(0)==16 && panel.c.runtimeSpeed(1)==24);
    uint32_t serial=0, now=1110;
    for (float fraction : {1.f,.5f,.01f}) {
      for (unsigned n=0;n<200;++n,now+=10) {
        if (n == 100 && fraction == 1.f)
          assert(input.setRuntimeSpeed(0,8,now) == (remote ? Result::Deferred : Result::Ok));
        assert(input.manualDrive(0,1,1,++serial,3,fraction,fraction,false,now,100,true) ==
               (remote ? Result::Deferred : Result::Ok));
        bus.tick(now);
      }
      for (unsigned axis : {0u,1u}) assert(std::abs(motor.c.axes[axis].speedOutputPercent-80*fraction)<.02f);
    }
    assert(motor.c.runtimeSpeed(0)==8 && motor.c.runtimeSpeed(1)==24);
    input.manualDrive(0,1,1,++serial,3,0,0,true,now,100,true); bus.tick(now);
    assert(!motor.c.axes[0].wanted && !motor.c.axes[1].wanted);
    // Both directions of the temporary encoder modifier work over the real HUD codec.
    HudKnob knob(panel.h,panel.c);
    knob.sample(true,false,0,now+=40,false);
    for (unsigned selected : {0u,1u}) {
      knob.sample(true,true,0,now+=40,false); assert(knob.axis()==1-selected);
      knob.sample(true,true,1,now+=40,false); bus.tick(now);
      assert(motor.c.axes[1-selected].tracking);
      knob.sample(true,false,0,now+=10,false);
      knob.sample(true,false,0,now+=40,false); assert(knob.axis()==selected);
      knob.sample(true,false,0,now+=400,false); bus.tick(now);
      assert(motor.c.axes[1-selected].tracking && !knob.menu);
      motor.c.stop();
      for (unsigned n=0;n<10;++n) bus.tick(now+=50);
      // Persistently select the other axis using the existing double click.
      for (unsigned n=0;n<2;++n) {
        knob.sample(true,true,0,now+=40,false);
        knob.sample(true,false,0,now+=10,false); knob.sample(true,false,0,now+=40,false);
      }
      assert(knob.axis()==1-selected);
    }
  }
}
int main() {
  speedScreenTouch();
  oldSpeedCompatibility();
  fullRangeAndSpeedCommands();
  core();
  touch();
  progressiveSwipe();
  fullscreenSwipe();
  idleTouchFrames();
  webKeyboardLease();
  inputAfterRenderPause();
  fullscreenSwipeControl();
  ring();
  ringTargets();
  network();
  std::cout
      << "PASS: fullscreen current/target swipe capture and reversal locally and via HUD, ring tap/drag, dual targets, hold/swipe axes and speed, ownership, lease expiry, coalescing, packet order and legacy compatibility\n";
}
