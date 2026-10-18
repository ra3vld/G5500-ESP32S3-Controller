#include "motion_fixture.hpp"
#include "crowpanel_knob.hpp"
#include "crowpanel_touch.hpp"
#include "hud_input.hpp"
#include "api.hpp"
#include <cassert>
#include <deque>
#include <vector>
#include <cstdio>
using namespace forkrot;
static void latch(Controller &c, unsigned i, MotionError error = MotionError::FeedbackInvalid) {
  c.stopNow(-1, 100);
  c.axes[i].motionError = error;
  c.axes[i].motionPhase = MotionPhase::Fault;
  c.fault = std::string(i ? "el_" : "az_") + motionErrorName(error);
}
static void stopped(const Controller &c) {
  assert(!c.moving());
  for (const auto &a : c.axes) assert(!a.tracking && !a.holdArmed && !a.speedOutputPercent);
}
struct Node;
struct ResetPacket { HudEndpoint from, to; std::vector<uint8_t> bytes; };
struct Bus {
  uint32_t now = 0;
  std::vector<Node *> nodes;
  std::deque<ResetPacket> packets;
  std::vector<ResetPacket> history;
  void tick(uint32_t at);
};
struct Node : HudTransport {
  Bus &bus;
  HudEndpoint endpoint;
  Controller c = testController();
  Protocol p{c};
  Hud h{c, p};
  Node(Bus &b, uint64_t id, bool hud) : bus(b), endpoint{uint32_t(id),4534,false} {
    h.config.role = hud ? DeviceRole::Hud : DeviceRole::Controller;
    h.begin(id, id + 100); h.setTransport(this);
    if (!hud) c.feedback(50,50,true,true);
    bus.nodes.push_back(this);
  }
  bool sendHud(const HudEndpoint &e, const uint8_t *p, size_t n) override {
    ResetPacket packet{endpoint,e,{p,p+n}};bus.packets.push_back(packet);bus.history.push_back(packet);return true;
  }
};
void Bus::tick(uint32_t at) {
  now=at;
  for (auto *n:nodes) n->h.service(at);
  unsigned budget=10000;
  while (!packets.empty()) {
    assert(budget--);auto p=packets.front();packets.pop_front();
    for (auto *n:nodes) if (n->endpoint==p.to) n->h.receive(p.from,p.bytes.data(),p.bytes.size(),at);
  }
}
int main() {
  for (auto error : {MotionError::FeedbackInvalid,MotionError::FeedbackStale,MotionError::KickTimeout,
                     MotionError::Stalled,MotionError::Planner}) {
    Controller c=testController();c.feedback(50,50,true,true);
    CrowPanelKnob knob(c);knob.sample(true,false,0,50,false);
    assert(c.target({120,80,true,true},true,60)==Result::Pending);
    latch(c,1,error);
    knob.sample(true,true,0,110,false); // Press resets immediately, including other-axis error.
    assert(!c.hasDriveError());stopped(c);assert(!c.pendingValid);
    knob.sample(true,true,0,1500,false); // Holding the acknowledgement cannot change mode.
    knob.sample(true,false,0,1540,false);knob.sample(true,false,0,1580,false);
    knob.sample(true,false,0,2000,false);stopped(c);assert(c.mode==Mode::Manual);
    assert(c.target({120,0,true,false},false,2010)==Result::Ok);assert(c.axes[0].tracking);
  }
  {
    Controller c=testController();c.feedback(50,50,true,true);
    c.axes[0].target=120;c.axes[0].targetKnown=true;
    latch(c,0); // A target tap resets and does not re-execute the old displayed target.
    crowpanel::TouchGesture gesture;crowpanel::TouchLayout layout;
    layout.target={160,280,160,40};
    assert(gesture.sample(true,200,300,layout).action==crowpanel::TouchAction::None);
    assert(gesture.sample(false,200,300,layout).action==crowpanel::TouchAction::Confirm);
    assert(c.executeDisplayed(200)==Result::Ok);assert(!c.hasDriveError());stopped(c);
    assert(c.executeDisplayed(210)==Result::Ok);assert(c.axes[0].tracking);
    c.stopNow(-1,220);c.driveFault=true;c.fault="pwm_attach_failed";
    assert(c.executeDisplayed(230)==Result::Ok);assert(c.hasDriveError());stopped(c);
    c.configurationLocked=true;assert(c.resetMotion(-1,240)==Result::Unavailable);assert(c.driveFault);
  }
  {
    Bus bus;Node motor(bus,1,false),panel(bus,2,true);
    assert(panel.h.add(1,motor.endpoint,"Motor"));assert(panel.h.select(1));bus.tick(100);
    latch(motor.c,0);bus.tick(200);assert(panel.c.hasDriveError());
    HudKnob knob(panel.h,panel.c);knob.sample(true,false,0,200,false);
    knob.sample(true,true,0,240,false);bus.tick(240);
    assert(!motor.c.hasDriveError());stopped(motor.c);
    const uint32_t request=panel.h.requestId();assert(panel.h.result(request).result==Result::Ok);
    auto reset=bus.history.end();
    for (auto it=bus.history.begin();it!=bus.history.end();++it)
      if (it->from==panel.endpoint && it->bytes.size()>51 &&
          (it->bytes[4]==4 || it->bytes[4]==9) && it->bytes[49]==uint8_t(ControlOp::ResetMotion)) reset=it;
    assert(reset!=bus.history.end());
    const auto packet=*reset;
    latch(motor.c,0,MotionError::KickTimeout);
    motor.h.receive(packet.from,packet.bytes.data(),packet.bytes.size(),241);
    assert(motor.c.axes[0].motionError==MotionError::KickTimeout); // Retry is deduplicated.
    assert(panel.c.resetMotion(-1,250)==Result::Deferred);bus.tick(250);
    assert(!motor.c.hasDriveError());stopped(motor.c);
    knob.sample(true,true,0,1600,false);knob.sample(true,false,0,1640,false);
    knob.sample(true,false,0,1680,false);knob.sample(true,false,0,2100,false);bus.tick(2100);stopped(motor.c);
    latch(motor.c,1);bus.tick(2200);
    Settings settings;
    auto response=api(panel.c,panel.p,settings,"/api/motion/reset",{{"axis","all"},{"selection",std::to_string(panel.h.selectionEpoch-1)}},2210);
    assert(response.status==409);assert(motor.c.hasDriveError());
    response=api(panel.c,panel.p,settings,"/api/motion/reset",{{"axis","all"},{"selection",std::to_string(panel.h.selectionEpoch)}},2220);
    assert(response.status==202);bus.tick(2220);assert(!motor.c.hasDriveError());stopped(motor.c);
    latch(motor.c,0);bus.tick(2300);
    assert(panel.c.executeDisplayed(2310)==Result::Deferred);bus.tick(2310);
    assert(!motor.c.hasDriveError());stopped(motor.c);
  }
  std::puts("PASS: local and HUD encoder/target fault acknowledgement, no restart, permanent protection, selection token and replay deduplication");
}
