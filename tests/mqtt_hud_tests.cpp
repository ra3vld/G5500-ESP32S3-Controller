#include "motion_fixture.hpp"
#include "mqtt_hud_queue.hpp"
#include <cassert>
#include <vector>
#include <iostream>
using namespace forkrot;
struct Node : HudTransport {
  Controller c=testController();Protocol p{c};Hud h{c,p};
  MqttHudQueue incoming,outgoing;
  unsigned frames=0,queries=0,subscriptions=0;
  uint32_t epoch=1;
  bool online=true;
  Node(uint64_t id,bool panel) {
    h.config.role=panel?DeviceRole::Hud:DeviceRole::Controller;
    h.begin(id,id+100);h.setTransport(this);c.feedback(20,30,true,true);
  }
  bool sendHud(const HudEndpoint &e,const uint8_t *bytes,size_t n) override {
    assert(e.mqtt && e.mqttId()==mqttHudId(bytes+13));
    ++frames;queries+=bytes[4]==2 || bytes[4]==11;subscriptions+=bytes[4]==7 || bytes[4]==13;
    return online && outgoing.offer(bytes,n,epoch,h.now);
  }
  void receive(uint32_t at) {
    incoming.drain(epoch,at,[&](const uint8_t *bytes,size_t n) {
      h.receive(HudEndpoint::mqttNode(mqttHudId(bytes+5)),bytes,n,at);return true;
    });
  }
};
int main() {
  constexpr uint64_t controllerId=0xc0e5525c3314ULL,panelId=0x9c70b80af6e8ULL;
  Node controller(controllerId,false),panel(panelId,true);
  assert(sizeof(MqttHudQueue)*2<=2048);
  assert(sizeof(HudEndpoint)==8);
  const auto endpoint=HudEndpoint::mqttNode(controllerId);
  assert(endpoint.mqttId()==controllerId && !endpoint.uart);
  assert(!(endpoint==HudEndpoint{endpoint.address,endpoint.port,false}));
  assert(panel.h.add(controllerId,endpoint,"MQTT"));assert(panel.h.select(controllerId));
  uint32_t now=0;
  auto tick=[&](uint32_t duration) {
    const auto end=now+duration;
    for(;now<end;now+=5) {
      for(auto *node:{&controller,&panel}) {
        node->receive(now);node->h.service(now);node->c.tick(now);
        if(node->online)node->outgoing.drain(node->epoch,now,[&](const uint8_t *bytes,size_t n) {
          auto &other=node==&controller?panel:controller;
          return !other.online || other.incoming.offer(bytes,n,other.epoch,now);
        });
      }
    }
  };
  controller.c.setMode(Mode::Auto,0);tick(4000);
  assert(panel.h.available() && panel.h.manualAvailable() && !panel.h.active()->live);
  assert(!panel.subscriptions && panel.queries>=6 && panel.queries<=8);
  ControlCommand target;target.op=ControlOp::Target;target.target={90,0,true,false};
  assert(panel.h.submitAt(target,now)==Result::Deferred);auto request=panel.h.requestId();tick(100);
  assert(panel.h.result(request).result==Result::Ok && controller.c.axes[0].target==90);
  ControlCommand stop;stop.op=ControlOp::Stop;
  assert(panel.h.submitAt(stop,now)==Result::Deferred);tick(100);assert(!controller.c.moving());
  ControlCommand manual;manual.op=ControlOp::ManualDrive;manual.gesture=1;manual.axesMask=1;manual.manual[0]=.5;
  for(unsigned i=0;i<10;++i){assert(panel.h.submitAt(manual,now)==Result::Deferred);tick(50);}
  assert(controller.c.axes[0].direction!=0);
  manual.release=true;assert(panel.h.submitAt(manual,now)==Result::Deferred);tick(50);assert(!controller.c.moving());
  manual.release=false;manual.gesture=2;
  for(unsigned i=0;i<4;++i){assert(panel.h.submitAt(manual,now)==Result::Deferred);tick(50);}
  assert(controller.c.moving());panel.online=false;++panel.epoch;panel.h.disconnectMqtt();tick(150);
  tick(1000);assert(!controller.c.moving() && !panel.h.available());
  panel.online=true;tick(1000);assert(panel.h.available() && !controller.c.moving());
  // An unanswered TCP query cannot generate five more queries per second.
  const auto queries=panel.queries;controller.online=false;tick(2200);
  assert(panel.queries-queries<=3);
  controller.online=true;tick(1200);assert(panel.h.available());
  // Full normal slot never excludes either independent axis STOP.
  std::vector<std::vector<uint8_t>> sent;
  controller.outgoing.drain(controller.epoch,now,[&](const uint8_t *,size_t){return true;});
  panel.outgoing.drain(panel.epoch,now,[&](const uint8_t *,size_t){return true;});
  target.target.az=130;assert(panel.h.submitAt(target,now)==Result::Deferred);panel.h.service(now);
  std::vector<uint8_t> motion;
  panel.outgoing.drain(panel.epoch,now,[&](const uint8_t *p,size_t n){motion.assign(p,p+n);return true;});
  assert(!motion.empty());MqttHudQueue queue;assert(queue.offer(motion.data(),motion.size(),1,now));
  for(int axis:{0,1}) {
    stop.axis=axis;assert(panel.h.submitAt(stop,now)==Result::Deferred);panel.h.service(now);
    panel.outgoing.drain(panel.epoch,now,[&](const uint8_t *p,size_t n){assert(mqttHudStop(p,n));assert(queue.offer(p,n,1,now));return true;});
  }
  queue.drain(1,now,[&](const uint8_t *p,size_t n){sent.emplace_back(p,p+n);return true;});
  assert(sent.size()==2 && mqttHudStop(sent[0].data(),sent[0].size()) && mqttHudStop(sent[1].data(),sent[1].size()));
  assert(queue.offer(motion.data(),motion.size(),1,now));unsigned calls=0;
  queue.drain(2,now,[&](const uint8_t *,size_t){++calls;return true;});assert(!calls);
  assert(queue.offer(motion.data(),motion.size(),2,now));
  queue.drain(2,now+500,[&](const uint8_t *,size_t){++calls;return true;});assert(!calls);
  assert(!queue.offer(motion.data(),HUD_FRAME_MAX+1,2,now));
  // A single TCP retransmission may take 3s. Accept its state response while
  // keeping manual freshness strict and deduplicating a later numeric retry.
  Node slowController(controllerId+1,false),slowPanel(panelId+1,true);
  slowController.c.setMode(Mode::Auto,0);
  assert(slowPanel.h.add(controllerId+1,HudEndpoint::mqttNode(controllerId+1),"Slow MQTT"));
  assert(slowPanel.h.select(controllerId+1));
  auto query=[&](uint32_t sentAt,uint32_t receivedAt) {
    slowPanel.h.service(sentAt);
    slowPanel.outgoing.drain(1,sentAt,[&](const uint8_t *p,size_t n){slowController.h.receive(HudEndpoint::mqttNode(panelId+1),p,n,sentAt);return true;});
    std::vector<uint8_t> reply;
    slowController.outgoing.drain(1,sentAt,[&](const uint8_t *p,size_t n){reply.assign(p,p+n);return true;});
    assert(!reply.empty());slowPanel.h.receive(HudEndpoint::mqttNode(controllerId+1),reply.data(),reply.size(),receivedAt);
    assert(slowPanel.h.available() && slowPanel.h.active()->receivedAt==receivedAt);
  };
  query(500,3500);query(4000,4005);
  assert(slowPanel.h.submitAt(target,5000)==Result::Deferred);slowPanel.h.service(5000);
  std::vector<uint8_t> delayedCommand;
  slowPanel.outgoing.drain(1,5000,[&](const uint8_t *p,size_t n){delayedCommand.assign(p,p+n);return true;});
  assert(!delayedCommand.empty() && delayedCommand[4]==4);
  slowController.h.receive(HudEndpoint::mqttNode(panelId+1),delayedCommand.data(),delayedCommand.size(),5000);
  slowController.h.receive(HudEndpoint::mqttNode(panelId+1),delayedCommand.data(),delayedCommand.size(),5500);
  assert(slowController.h.duplicates==1);
  assert(slowController.c.target({123,0,true,false},false,5500)==Result::Ok);
  manual.release=false;manual.gesture=9;
  assert(slowPanel.h.submitAt(manual,5500)==Result::Deferred);slowPanel.h.service(5500);
  std::vector<uint8_t> oldManual;
  slowPanel.outgoing.drain(1,5500,[&](const uint8_t *p,size_t n){oldManual.assign(p,p+n);return true;});
  assert(!oldManual.empty() && oldManual[4]==15);
  slowController.h.receive(HudEndpoint::mqttNode(panelId+1),oldManual.data(),oldManual.size(),5500);
  slowController.c.stopNow(-1,6500);const auto stoppedTarget=slowController.c.axes[0].target;
  const auto rejected=slowController.h.rejected;
  slowController.h.receive(HudEndpoint::mqttNode(panelId+1),oldManual.data(),oldManual.size(),6500);
  assert(slowController.h.rejected==rejected+1 && !slowController.c.moving());
  slowController.h.receive(HudEndpoint::mqttNode(panelId+1),delayedCommand.data(),delayedCommand.size(),8500);
  assert(slowController.h.duplicates==1 && slowController.c.axes[0].target==stoppedTarget && !slowController.c.moving());
  slowPanel.h.now=8500;char catalogue[2048];JsonWriter catalogueJson(catalogue,sizeof catalogue);
  assert(slowPanel.h.writePeers(catalogueJson,0,8,false));
  assert(std::string(catalogueJson.data()).find("\"online\":true")!=std::string::npos);
  slowController.h.receive(HudEndpoint::mqttNode(panelId+1),delayedCommand.data(),delayedCommand.size(),9100);
  assert(slowController.h.rejected==rejected+2 && !slowController.c.moving());
  std::cout<<"PASS: bounded MQTT HUD binary queues, 2 Hz polling/no live subscription, target/ACK, independent STOPs, manual release/expiry and reconnect without replay\n";
}
