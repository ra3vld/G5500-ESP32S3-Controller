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
  {
    Bus stream;Node motor(stream,1,1),panel(stream,2,2,true);
    motor.controller.hardware.drive=DRIVE_BTS7960;motor.controller.speedEnabled=true;
    motor.controller.motionSettings[0].kickEnabled=true;
    panel.save(motor);panel.hud.select(1);stream.tick(100);
    assert(panel.hud.active()->wireVersion==5);
    uint32_t previous=0;
    for(unsigned i=1;i<=200;++i){
      assert(panel.controller.target({20+.1f*i,0,true,false},false,100)==Result::Deferred);
      if(previous)assert(panel.hud.result(previous).result==Result::Cancelled);
      previous=panel.hud.requestId();
    }
    stream.tick(110);assert(motor.controller.axes[0].target==40);
    assert(panel.hud.result(previous).result==Result::Ok);
    assert(motor.controller.axes[0].motionPhase==MotionPhase::Kick);
    assert(panel.controller.motionSettings[0].ready&&panel.controller.motionSettings[0].kickEnabled);
    assert(panel.controller.axes[0].motionPhase==MotionPhase::Kick);
    assert(panel.controller.target({9999,0,true,false},false,110)==Result::Invalid);
    auto &held=motor.controller.axes[0];held.current=held.target=40;
    held.tracking=false;held.wanted=held.direction=0;held.speedOutputPercent=0;
    held.holdArmed=true;held.motionPhase=MotionPhase::Holding;
    motor.controller.motionSettings[0].mode=RegulatorMode::Cascade;
    motor.controller.motionSettings[0].holdEnabled=true;stream.tick(200);
    assert(panel.controller.axes[0].holdArmed&&!panel.controller.moving());
    HudKnob knob(panel.hud,panel.controller);
    knob.sample(true,false,0,200);knob.sample(true,true,0,240);
    knob.sample(true,false,0,280);knob.sample(true,false,0,320);stream.tick(320);
    assert(!motor.controller.axes[0].holdArmed&&!motor.controller.moving());
    knob.sample(true,false,0,360);knob.sample(true,true,0,400);
    knob.sample(true,false,0,440);knob.sample(true,false,0,480);
    knob.sample(true,false,0,840);stream.tick(840);
    assert(motor.controller.axes[0].holdArmed);
    for(unsigned at=860;at<=1240;at+=20) stream.tick(at);
    assert(panel.controller.axes[0].holdArmed&&!panel.controller.axes[0].tracking);
    panel.controller.axes[0].current=40.2f;panel.controller.axes[0].tracking=false;
    HudKnob detent(panel.hud,panel.controller);
    detent.sample(true,false,0,1300);detent.sample(true,false,-1,1340);stream.tick(1340);
    assert(motor.controller.axes[0].target==60);
  }

  Bus bus;
  Node rotor(bus, 1, 0x0100007f), other(bus, 2, 0x0200007f), a(bus, 10, 0x0a00007f, true),
      b(bus, 11, 0x0b00007f, true);
  a.save(rotor);
  a.save(other);
  b.save(rotor);
  assert(a.hud.select(1) && b.hud.select(1));
  const auto changes = a.selectionChanges;
  assert(a.hud.select(2));
  assert(a.selectionChanges == changes + 1);
  assert(a.hud.select(2) && a.selectionChanges == changes + 1);
  assert(a.hud.select(1) && a.selectionChanges == changes + 2);
  bus.tick(100);
  assert(a.hud.available() && b.hud.available());
  assert(a.controller.axes[0].current == 20 && a.controller.remote);
  Settings scopeSettings;
  assert(api(a.controller, a.protocol, scopeSettings, "/api/move", {{"d", "S"}, {"selection", "0"}}, 100)
             .status == 409);
  assert(api(a.controller, a.protocol, scopeSettings, "/api/settings",
             {{"n1mmPort", "14000"}, {"pstPort", "14000"}}, 100)
             .status == 400);
  assert(a.protocol.rotorName == "G5500");
  // Queries use the confirmed position, never locally simulated feedback.
  a.controller.feedback(90, 90, true, true);
  a.controller.tick(999);
  assert(a.controller.axes[0].current == 20);
  assert(a.protocol.line("C", 100).reply == "AZ=020\r\n");
  auto p = a.protocol.line("M100", 100);
  assert(p.result == Result::Deferred);
  auto request = a.hud.requestId();
  bus.tick(110);
  assert(a.hud.result(request).result == Result::Pending && rotor.controller.pendingValid &&
         !rotor.controller.moving());
  assert(b.controller.execute(110) == Result::Deferred);
  request = b.hud.requestId();
  bus.tick(120);
  assert(b.hud.result(request).result == Result::Ok && rotor.controller.axes[0].target == 100 &&
         rotor.controller.moving());
  // Equal rights: a directly connected logger replaces the HUD target in auto.
  rotor.controller.setMode(Mode::Auto, 120);
  rotor.protocol.line("M120", 120);
  assert(rotor.controller.axes[0].target == 120);
  bus.tick(220);
  assert(a.controller.axes[0].target == 120);
  Settings settings;
  const auto apiReply = api(a.controller, a.protocol, settings, "/api/target", {{"az", "150"}}, 220);
  assert(apiReply.status == 202 && apiReply.body.find("requestId") != std::string::npos);
  bus.tick(230);
  assert(rotor.controller.axes[0].target == 150);
  assert(api(a.controller, a.protocol, settings, "/api/settings", {{"model", "G800DXA"}}, 230).status == 400);
  assert(a.protocol.line("X4", 230).result == Result::Invalid);
  // Preset priority applies identically to a command arriving via HUD.
  assert(rotor.controller.preset(1, 230) == Result::Ok);
  assert(a.controller.target({200, 0, true, false}, true, 230) == Result::Deferred);
  request = a.hud.requestId();
  bus.tick(240);
  assert(a.hud.result(request).result == Result::Ignored);
  a.controller.stopNow(-1, 240);
  request = a.hud.requestId();
  a.hud.service(250);
  assert(!bus.packets.empty());
  auto stopPacket = bus.packets.front();
  const auto oldStop = rotor.controller.stopEpoch;
  bus.packets.push_back(stopPacket);
  bus.now = 250;
  bus.pump();
  assert(rotor.controller.stopEpoch == oldStop + 1);
  assert(a.hud.result(request).result == Result::Ok && rotor.hud.duplicates);
  // Older datagrams cannot replace a newer command, including after STOP.
  assert(a.controller.target({70, 0, true, false}, false, 250) == Result::Deferred);
  const auto first = a.hud.requestId();
  assert(a.controller.target({80, 0, true, false}, false, 250) == Result::Deferred);
  const auto second = a.hud.requestId();
  a.hud.service(260);
  std::reverse(bus.packets.begin(), bus.packets.end());
  bus.now = 260;
  bus.pump();
  assert(rotor.controller.axes[0].target == 80 && a.hud.result(second).result == Result::Ok &&
         a.hud.result(first).result == Result::Cancelled);
  // Switching keeps a sent transaction bound to its original destination.
  assert(a.controller.target({90, 0, true, false}, false, 260) == Result::Deferred);
  request = a.hud.requestId();
  a.hud.service(270);
  assert(a.hud.select(2));
  bus.now = 270;
  bus.pump();
  assert(rotor.controller.axes[0].target == 90 && other.controller.axes[0].target != 90);
  assert(a.hud.result(request).peer == 1);
  bus.tick(370);
  assert(a.hud.available());
  // Queued (unsent) old-selection commands are cancelled, never redirected.
  assert(a.controller.target({110, 0, true, false}, false, 370) == Result::Deferred);
  request = a.hud.requestId();
  assert(a.hud.select(1));
  assert(a.hud.result(request).result == Result::Cancelled);
  bus.tick(470);
  assert(other.controller.axes[0].target != 110);
  // The same STOP works from another HUD. Jog renewal belongs to its initiating source.
  a.controller.jog(0, 1, false, 470);
  request = a.hud.requestId();
  bus.tick(480);
  assert(a.hud.result(request).result == Result::Ok);
  bus.tick(730);
  assert(rotor.controller.axes[0].wanted == 1);
  bus.drop = true;
  bus.tick(1000);
  bus.tick(1800);
  assert(rotor.controller.axes[0].wanted == 0 && rotor.controller.axes[0].direction == 0);
  bus.drop = false;
  bus.tick(1900);
  // A subsequent direct command has its own epoch and survives the old HUD timeout.
  a.controller.jog(0, 1, false, 1900);
  bus.tick(1910);
  rotor.controller.jog(0, -1, false, 1910);
  bus.drop = true;
  bus.tick(3000);
  assert(rotor.controller.axes[0].wanted == -1);
  bus.drop = false;
  bus.tick(3100);
  // Losing the panel never cancels a numerical target or sequence already accepted.
  a.controller.target({160, 0, true, false}, false, 3100);
  bus.tick(3110);
  bus.drop = true;
  bus.tick(5300);
  assert(rotor.controller.axes[0].target == 160 && rotor.controller.axes[0].tracking);
  assert(!a.hud.available());
  assert(a.protocol.line("C", 5300).reply == "?>\r\n");
  assert(a.protocol.datagram("<PST>AZ?</PST>", 5300).reply.empty());
  assert(a.controller.target({200, 0, true, false}, false, 5300) == Result::Unavailable);
  bus.drop = false;
  bus.tick(5400);
  assert(rotor.controller.axes[0].target == 160);
  // Expired requests and requests addressed to a previous controller boot are rejected.
  a.controller.target({210, 0, true, false}, false, 5400);
  a.hud.service(5410);
  auto late = bus.packets.front();
  bus.packets.clear();
  bus.now = 7500;
  rotor.hud.receive(late.source, late.bytes.data(), late.bytes.size(), bus.now);
  assert(rotor.controller.axes[0].target == 160);
  rotor.hud.begin(1, 9999);
  rotor.hud.receive(late.source, late.bytes.data(), late.bytes.size(), 7510);
  assert(rotor.controller.axes[0].target == 160);
  bus.tick(7600);
  assert(a.hud.peer(1)->boot == 9999);
  // Every frame boundary is CRC checked; UART COBS resynchronizes after malformed input.
  uint8_t encoded[HUD_FRAME_MAX + 8], decoded[HUD_FRAME_MAX];
  HudUartDecoder decoder;
  const auto encodedSize =
      hudUartEncode(stopPacket.bytes.data(), stopPacket.bytes.size(), encoded, sizeof encoded);
  assert(encodedSize);
  for (size_t i = 0; i + 1 < encodedSize; ++i)
    assert(decoder.feed(encoded[i], decoded, sizeof decoded) == 0);
  const auto decodedSize = decoder.feed(0, decoded, sizeof decoded);
  assert(decodedSize == stopPacket.bytes.size() &&
         std::equal(decoded, decoded + decodedSize, stopPacket.bytes.begin()));
  decoder.feed(255, decoded, sizeof decoded);
  assert(decoder.feed(0, decoded, sizeof decoded) == 0);
  size_t got = 0;
  for (size_t i = 0; i < encodedSize; ++i)
    got = decoder.feed(encoded[i], decoded, sizeof decoded);
  assert(got == decodedSize);
  auto damaged = stopPacket.bytes;
  damaged[5] ^= 1;
  const auto rejects = rotor.hud.rejected;
  rotor.hud.receive(stopPacket.source, damaged.data(), damaged.size(), 7600);
  assert(rotor.hud.rejected == rejects + 1);
  // Discovery is read-only: an unsaved peer cannot be selected or commanded.
  Node c(bus, 12, 0x0c00007f, true);
  c.hud.service(bus.now);
  assert(c.hud.discover({0xffffffffu, 4534, false}));
  bus.pump();
  assert(c.hud.count() == 0 && c.hud.count(false) == 2 && !c.hud.select(1));
  // No catalogue count constant: pagination and fair bounded polling beyond small fixed arrays.
  for (unsigned i = 0; i < 80; ++i)
    assert(c.hud.add(100 + i, {0x1000007fu + i, 4534, false}, "Stored"));
  char json[4096];
  JsonWriter j(json, sizeof json);
  assert(c.hud.writePeers(j, 72, 8));
  assert(std::string(j.data()).find("\"total\":80") != std::string::npos);
  c.hud.select(100);
  bus.queries = 0;
  for (uint32_t at = 8000; at < 9000; at += 5)
    c.hud.service(at);
  assert(bus.queries <= 20);
  bus.packets.clear();
  // Knob hold is temporary; the name-button action opens selection.
  HudKnob knob(a.hud, a.controller);
  knob.sample(true, false, 0, 9000);
  knob.sample(true, false, 0, 9031);
  const auto before = rotor.controller.stopEpoch;
  const auto initialSelection = a.hud.config.selected;
  knob.sample(true, true, 0, 9040);
  knob.sample(true, true, 0, 10240);
  assert(a.hud.config.selected == initialSelection && !knob.menu && rotor.controller.stopEpoch == before);
  knob.sample(true, false, 0, 10241);
  knob.sample(true, false, 0, 10280);
  knob.open();
  assert(a.hud.config.selected == 2);
  a.hud.add(3, {0x0300007f, 4534, false}, "Third");
  knob.sample(true, true, 0, 10300);
  knob.sample(true, true, 0, 11500);
  assert(!knob.menu);
  knob.open();
  assert(knob.menu);
  const auto selected = a.hud.config.selected;
  knob.sample(true, false, 0, 11501);
  knob.sample(true, false, 0, 11540);
  knob.sample(true, false, 1, 11550);
  assert(a.hud.config.selected == selected);
  knob.sample(true, true, 0, 11560);
  knob.sample(true, false, 0, 11570);
  knob.sample(true, false, 0, 11610);
  assert(!knob.menu && a.hud.config.selected != selected);
  // millis wrap: tickets and lease expiry use unsigned elapsed time.
  Bus wrap;
  Node wr(wrap, 50, 50), wh(wrap, 51, 51, true);
  wh.save(wr);
  wh.hud.select(50);
  wrap.tick(UINT32_MAX - 500);
  assert(wh.hud.available());
  wh.controller.jog(0, 1, false, wrap.now);
  wrap.tick(UINT32_MAX - 400);
  wrap.drop = true;
  wrap.tick(700);
  assert(!wr.controller.axes[0].wanted);
  // Lost ACK: retry the identical command without executing the move twice.
  {
    Bus loss;
    Node motor(loss, 1, 1), panel(loss, 2, 2, true);
    panel.save(motor);
    panel.hud.select(1);
    loss.tick(100);
    panel.controller.target({160, 0, true, false}, false, 100);
    auto requestId = panel.hud.requestId();
    panel.hud.service(110);
    loss.now = 110;
    auto frame = loss.packets.front();
    loss.packets.pop_front();
    motor.hud.receive(frame.source, frame.bytes.data(), frame.bytes.size(), 110);
    loss.packets.clear(); // ACK lost, command already accepted.
    const auto epoch = motor.controller.motionEpoch[0];
    loss.tick(260);
    assert(panel.hud.result(requestId).result == Result::Ok);
    assert(motor.controller.motionEpoch[0] == epoch && motor.hud.duplicates == 1);
    // A replayed position response cannot refresh a disconnected drive.
    panel.hud.service(400);
    loss.now = 400;
    auto query = loss.packets.front();
    loss.packets.pop_front();
    motor.hud.receive(query.source, query.bytes.data(), query.bytes.size(), 400);
    auto stale = loss.packets.back();
    loss.packets.clear();
    loss.drop = true;
    loss.tick(3000);
    panel.hud.receive(stale.source, stale.bytes.data(), stale.bytes.size(), 3000);
    assert(!panel.hud.available());
  }
  // Switching with the jog ACK still in flight sends an owned release.
  {
    Bus switching;
    Node motor(switching, 1, 1), next(switching, 2, 2), panel(switching, 3, 3, true);
    panel.save(motor);
    panel.save(next);
    panel.hud.select(1);
    switching.tick(100);
    panel.controller.jog(0, 1, false, 100);
    panel.hud.service(110);
    assert(panel.hud.select(2));
    switching.tick(120);
    assert(!motor.controller.axes[0].wanted);
  }
  // Bounded command pressure is explicit, while STOP retains a reserved slot.
  {
    Bus load;
    Node motor(load, 1, 1);
    std::vector<std::unique_ptr<Node>> panels;
    for (unsigned i = 0; i < 40; ++i) {
      panels.emplace_back(new Node(load, i + 2, i + 2, true));
      panels.back()->save(motor);
      panels.back()->hud.select(1);
    }
    load.tick(100);
    for (auto &panel : panels)
      assert(panel->controller.target({150, 0, true, false}, false, 100) == Result::Deferred);
    load.tick(110);
    assert(motor.hud.busy > 0);
    auto &panel = *panels.back();
    panel.controller.stopNow(-1, 110);
    const auto requestId = panel.hud.requestId();
    load.tick(120);
    assert(panel.hud.result(requestId).result == Result::Ok && !motor.controller.moving());
    assert(panel.controller.target({160, 0, true, false}, false, 120) == Result::Deferred);
    assert(panel.controller.target({170, 0, true, false}, false, 120) == Result::Deferred);
    assert(panel.controller.target({180, 0, true, false}, false, 120) == Result::Deferred);
    const auto displaced=panel.hud.requestId();
    assert(panel.controller.target({190, 0, true, false}, false, 120) == Result::Deferred);
    assert(panel.hud.result(displaced).result==Result::Cancelled);
    panel.controller.stopNow(-1, 120);
    assert(panel.hud.lastResult() == Result::Deferred);
    load.tick(130);
    assert(!motor.controller.moving());
  }
  // Whole HUD/1 transactions travel through the UART codec; noise does not execute commands.
  {
    Bus serial;
    Node motor(serial, 1, 1), panel(serial, 2, 2, true);
    motor.endpoint.uart = panel.endpoint.uart = true;
    panel.save(motor);
    panel.hud.select(1);
    serial.tick(100);
    panel.controller.target({160, 0, true, false}, false, 100);
    panel.hud.service(110);
    auto wire = serial.packets.front();
    serial.packets.pop_front();
    HudUartDecoder framing;
    uint8_t encoded[HUD_FRAME_MAX + 8], frame[HUD_FRAME_MAX];
    auto length = hudUartEncode(wire.bytes.data(), wire.bytes.size(), encoded, sizeof encoded);
    for (size_t i = 0; i < length / 2; ++i)
      assert(!framing.feed(encoded[i], frame, sizeof frame));
    // A truncated COBS packet can end on a block boundary; protocol CRC must reject it.
    const auto partial=framing.feed(0, frame, sizeof frame);
    if(partial) motor.hud.receive(panel.endpoint,frame,partial,110);
    assert(!motor.controller.moving());
    size_t size = 0;
    for (size_t i = 0; i < length; ++i)
      size = framing.feed(encoded[i], frame, sizeof frame);
    assert(size == wire.bytes.size());
    motor.hud.receive(panel.endpoint, frame, size, 110);
    serial.now = 110;
    serial.pump();
    assert(motor.controller.axes[0].target == 160 &&
           panel.hud.result(panel.hud.requestId()).result == Result::Ok);
    serial.drop = true;
    serial.tick(2200);
    assert(!panel.hud.available() && motor.controller.axes[0].tracking);
  }
  // Address changes require discovery, preserve saved identity and never add a duplicate.
  {
    Bus dhcp;
    Node motor(dhcp, 1, 1), panel(dhcp, 2, 2, true);
    panel.save(motor);
    panel.hud.select(1);
    dhcp.tick(100);
    motor.endpoint.address = 20;
    dhcp.tick(2200);
    assert(!panel.hud.available());
    assert(panel.hud.discover({0xffffffffu, 4534, false}));
    dhcp.pump();
    assert(panel.hud.available() && panel.hud.count() == 1 && panel.hud.active()->endpoint.address == 20);
  }
  // A single-entry long press never stops a target; a short release can pause and resume.
  {
    Bus gestures;
    Node motor(gestures, 1, 1), panel(gestures, 2, 2, true);
    panel.save(motor);
    panel.hud.select(1);
    gestures.tick(100);
    panel.controller.target({180, 0, true, false}, false, 100);
    gestures.tick(110);
    HudKnob knob(panel.hud, panel.controller);
    knob.sample(true, false, 0, 110);
    knob.sample(true, false, 0, 150);
    knob.sample(true, true, 0, 160);
    gestures.tick(1360);
    knob.sample(true, true, 0, 1360);
    assert(panel.hud.config.selected == 1 && !knob.menu && motor.controller.moving());
    knob.sample(true, false, 0, 1370);
    knob.sample(true, false, 0, 1410);
    knob.sample(true, true, 0, 1420);
    assert(motor.controller.moving());
    knob.sample(true, false, 0, 1430);
    assert(motor.controller.moving());
    knob.sample(true, false, 0, 1470);
    gestures.tick(1480);
    assert(!motor.controller.moving());
    knob.sample(true, false, 0, 1490);
    knob.sample(true, true, 0, 1500);
    knob.sample(true, false, 0, 1510);
    knob.sample(true, false, 0, 1550);
    knob.sample(true, false, 0, 1910);
    gestures.tick(1920);
    assert(motor.controller.axes[0].tracking && motor.controller.axes[0].target == 180);
  }
  // A fully transmitted program sequence survives link loss; switching clears an unsent sequence.
  {
    Bus sequence;
    Node motor(sequence, 1, 1), other(sequence, 3, 3), panel(sequence, 2, 2, true);
    panel.save(motor);
    panel.save(other);
    panel.hud.select(1);
    sequence.tick(100);
    motor.controller.setMode(Mode::Auto, 100);
    sequence.tick(200);
    assert(panel.protocol.line("W001 100 20 120 30", 200).result == Result::Deferred);
    sequence.tick(210);
    assert(panel.protocol.line("T", 210).result == Result::Deferred);
    sequence.tick(220);
    assert(motor.controller.sequenceCount() == 2);
    sequence.drop = true;
    sequence.tick(3000);
    assert(motor.controller.sequenceCount() == 2);
    sequence.drop = false;
    sequence.tick(3100);
    assert(panel.hud.select(3));
    sequence.tick(3200);
    assert(panel.protocol.line("T", 3200).result == Result::Invalid);
    sequence.tick(3210);
    assert(other.controller.sequenceCount() == 0);
  }
  // Queue rejection expires without another command, even while normal state
  // updates continue. It must not hide an offline or newly selected rotator.
  {
    Bus notices;
    Node motor(notices, 1, 1), other(notices, 3, 3), panel(notices, 2, 2, true);
    panel.save(motor); panel.save(other); panel.hud.select(1); notices.tick(100);
    HudKnob knob(panel.hud, panel.controller);
    auto fill = [&] {
      assert(panel.controller.target({90,0,true,false},false,notices.now)==Result::Deferred);
      ControlCommand mode;mode.op=ControlOp::Mode;mode.mode=Mode::Auto;assert(panel.hud.submit(mode)==Result::Deferred);
      ControlCommand save;save.op=ControlOp::SavePreset;save.slot=0;save.target={120,0,true,false};std::memcpy(save.label,"test",5);
      assert(panel.hud.submit(save)==Result::Deferred);
    };
    fill();
    knob.setBearing(180, 110);
    assert(knob.lastResult == Result::Busy && knob.busyNotice(110));
    for (uint32_t now = 120; now < 1109; now += 100) notices.tick(now);
    notices.tick(1109);
    assert(knob.busyNotice(1109));
    notices.tick(1110);
    assert(!knob.busyNotice(1110));
    assert(knob.lastResult == Result::Busy); // No new input was needed to clear the UI.
    assert(panel.hud.available() && panel.controller.moving());
    fill(); knob.toggleMode(1120);
    assert(knob.busyNotice(1120));
    knob.choose(3);
    assert(!knob.busyNotice(1121));
    knob.choose(1); notices.tick(1200);
    assert(!knob.busyNotice(1200));
    uint32_t offlineAt = notices.now;
    while (panel.hud.active()->fresh(offlineAt)) ++offlineAt;
    notices.drop = true; notices.tick(offlineAt - 100);
    fill(); knob.setBearing(180, notices.now);
    assert(knob.busyNotice(notices.now));
    notices.tick(offlineAt);
    assert(!panel.hud.available() && !knob.busyNotice(offlineAt));
    notices.drop = false; notices.tick(offlineAt + 100);
    assert(panel.hud.available() && !knob.busyNotice(offlineAt + 100));
  }
  // The notification's deadline also works across millis() wraparound.
  {
    Bus notices;
    Node motor(notices, 1, 1), panel(notices, 2, 2, true);
    panel.save(motor); panel.hud.select(1);
    const uint32_t start = UINT32_MAX - 100;
    notices.tick(start);
    HudKnob knob(panel.hud, panel.controller);
    assert(panel.controller.target({90,0,true,false},false,start)==Result::Deferred);
    ControlCommand mode;mode.op=ControlOp::Mode;mode.mode=Mode::Auto;assert(panel.hud.submit(mode)==Result::Deferred);
    ControlCommand save;save.op=ControlOp::SavePreset;save.target={120,0,true,false};std::memcpy(save.label,"test",5);assert(panel.hud.submit(save)==Result::Deferred);
    knob.setBearing(180,start);
    assert(knob.lastResult == Result::Busy && knob.busyNotice(start));
    for (uint32_t elapsed = 100; elapsed < 1000; elapsed += 100) notices.tick(start + elapsed);
    assert(knob.busyNotice(start + 999));
    notices.tick(start + 1000);
    assert(!knob.busyNotice(start + 1000));
  }
  // Ring taps are local user targets: forward immediately even in remote manual mode.
  {
    Bus taps;
    Node motor(taps, 1, 1), panel(taps, 2, 2, true);
    panel.save(motor);
    panel.hud.select(1);
    taps.tick(100);
    HudKnob knob(panel.hud, panel.controller);
    knob.setBearing(90, 110);
    assert(knob.lastResult == Result::Deferred);
    assert(panel.controller.axes[0].current == 20);
    taps.tick(120);
    assert(motor.controller.axes[0].target == 90 && motor.controller.axes[0].tracking);
    assert(motor.controller.mode == Mode::Manual && !motor.controller.pendingValid);
    knob.menu = true;
    knob.setBearing(180, 130);
    taps.tick(140);
    assert(motor.controller.axes[0].target == 90); // Menu taps cannot retarget the rotator.
    knob.menu = false;
    taps.drop = true;
    taps.tick(3000);
    knob.setBearing(180, 3010);
    assert(knob.lastResult == Result::Unavailable);
    assert(motor.controller.axes[0].target == 90);
  }
  // Tapping the pending goal executes it remotely; a queued knob click cannot stop it.
  {
    Bus taps;
    Node motor(taps, 1, 1), panel(taps, 2, 2, true);
    panel.save(motor); panel.hud.select(1); taps.tick(100);
    HudKnob knob(panel.hud, panel.controller);
    knob.sample(true, false, 0, 140);
    assert(panel.protocol.line("M100", 150).result == Result::Deferred);
    taps.tick(160);
    assert(panel.controller.pendingValid && motor.controller.pendingValid);
    knob.menu = true; knob.confirmPending(170); taps.tick(180);
    assert(motor.controller.pendingValid);
    knob.menu = false;
    knob.sample(true, true, 0, 200);
    knob.sample(true, false, 0, 210); knob.sample(true, false, 0, 250);
    knob.confirmPending(260);
    assert(knob.lastResult == Result::Deferred);
    taps.tick(270);
    assert(motor.controller.axes[0].target == 100 && !motor.controller.pendingValid);
    knob.sample(true, false, 0, 700); taps.tick(710);
    assert(motor.controller.axes[0].tracking);
  }
  // External AZ interpretation belongs to the receiving HUD. The controller
  // receives the resolved mechanical coordinate without resolving it again.
  for (const char *mode : {"shortest", "mechanical"}) {
    for (unsigned source = 0; source < 4; ++source) {
      Bus routes;
      Node motor(routes, 1, 0x0100007f), panel(routes, 10, 0x0a00007f, true);
      motor.controller.feedback(350, 30, true, true);
      panel.save(motor); assert(panel.hud.select(1)); routes.tick(100);
      Settings local;
      assert(api(panel.controller, panel.protocol, local, "/api/settings",
                 {{"externalAzMode", mode}}, 100).status == 200);
      const char *command[] = {"M10", "P 10 45", "<PST><AZIMUTH>10</AZIMUTH></PST>",
                              "<N1MMRotor><rotor>*</rotor><goazi>10</goazi></N1MMRotor>"};
      assert((source < 2 ? panel.protocol.line(command[source], 110, true)
                         : panel.protocol.datagram(command[source], 110)).result == Result::Deferred);
      const auto id = panel.hud.requestId(); routes.tick(120);
      assert(panel.hud.result(id).result == Result::Pending);
      assert(motor.controller.pending.az == (std::string(mode) == "shortest" ? 370 : 10));
      assert(!motor.controller.moving());
    }
  }
  // Per-protocol manual following belongs to the receiving HUD and travels with
  // its command. The selected controller stays manual and enforces preset priority.
  for (bool follow : {false, true}) {
    Bus policies;
    Node motor(policies, 1, 0x0100007f), panel(policies, 10, 0x0a00007f, true);
    panel.save(motor); assert(panel.hud.select(1)); policies.tick(100);
    Settings local;
    const std::string enabled = follow ? "true" : "false";
    assert(api(panel.controller, panel.protocol, local, "/api/settings",
               {{"n1mmManualFollow", enabled}, {"pstManualFollow", enabled},
                {"gs232ManualFollow", enabled}, {"rotctldManualFollow", enabled}}, 100).status == 200);
    const auto before = stateJson(panel.controller, panel.protocol, local, 100);
    assert(api(panel.controller, panel.protocol, local, "/api/settings",
               {{"n1mmManualFollow", "false"}, {"pstManualFollow", "bad"}}, 100).status == 400);
    assert(stateJson(panel.controller, panel.protocol, local, 100) == before);
    uint32_t at = 110;
    // Distinct targets must pass the existing repeated-target noise filter.
    for (const char *text : {"<N1MMRotor><rotor>*</rotor><goazi>100</goazi></N1MMRotor>",
                            "<PST><AZIMUTH>130</AZIMUTH></PST>", "M160", "P 190 45"}) {
      assert((text[0] == '<' ? panel.protocol.datagram(text, at) : panel.protocol.line(text, at, true))
                 .result == Result::Deferred);
      const auto id = panel.hud.requestId(); policies.tick(at += 10);
      assert(panel.hud.result(id).result == (follow ? Result::Ok : Result::Pending));
      assert(motor.controller.mode == Mode::Manual && motor.controller.pendingValid == !follow);
      assert(motor.controller.moving() == follow);
      panel.controller.stopNow(-1, at); policies.tick(at += 10);
    }
    assert(panel.protocol.line("M1 90 180", at).result == Result::Deferred);
    policies.tick(at += 10);
    assert(panel.protocol.line("T", at).result == Result::Deferred);
    const auto id = panel.hud.requestId(); policies.tick(at += 10);
    assert(panel.hud.result(id).result == (follow ? Result::Ok : Result::Pending));
    if (follow) { policies.tick(at += 1000); assert(motor.controller.axes[0].target == 180); }
    assert(motor.controller.mode == Mode::Manual);
    motor.controller.stopNow(-1, at);
    assert(motor.controller.preset(1, at) == Result::Ok);
    policies.tick(at += 10);
    assert(panel.protocol.line("M100", at).result == Result::Deferred);
    const auto ignored = panel.hud.requestId(); policies.tick(at += 10);
    assert(panel.hud.result(ignored).result == Result::Ignored && motor.controller.presetActive);
  }
  // Removing records, including the selected head, preserves the rest of the catalogue.
  assert(c.hud.remove(100)&&c.hud.config.selected==0&&c.hud.count()==79);
  for(unsigned id=101;id<180;++id)assert(c.hud.remove(id));
  assert(c.hud.count()==0);
  std::cout << "HUD: multi-peer arbitration, ACKs, identity, dedup/order/expiry, selection, leases, UART, "
               "discovery, 80 peers and bounded polling PASS\n";
}
