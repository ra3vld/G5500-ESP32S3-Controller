#include "motion_fixture.hpp"
#include "hud.hpp"
#include "local_input.hpp"
#include <cassert>
#include <deque>
#include <vector>
#include <iostream>
using namespace forkrot;
struct Node;
struct WirePacket { HudEndpoint from,to;std::vector<uint8_t> bytes; };
struct Bus {unsigned dropAck=0;uint32_t now=0;std::vector<Node*> nodes;std::deque<WirePacket> queue;std::vector<WirePacket> history;void tick(uint32_t);};
struct Node:HudTransport {
 Bus &bus;Controller c = forkrot::testController();Protocol p{c};Hud h{c,p};HudEndpoint endpoint;bool legacy=false;
 Node(Bus &b,int id,bool remote):bus(b),endpoint{uint32_t(id),4534,false}{h.config.role=remote?DeviceRole::Hud:DeviceRole::Controller;h.begin(id,id+100);h.setTransport(this);c.feedback(20,30,true,true);bus.nodes.push_back(this);}
 bool sendHud(const HudEndpoint &e,const uint8_t *p,size_t n) override {if(legacy&&p[4]>6)return true;bus.queue.push_back({endpoint,e,{p,p+n}});return true;}
 void select(Node &n){assert(h.add(n.h.id,n.endpoint,"drive"));assert(h.select(n.h.id));}
};
void Bus::tick(uint32_t at){now=at;for(auto *n:nodes)n->h.service(now);unsigned budget=1000;while(!queue.empty()){assert(budget--);auto msg=std::move(queue.front());queue.pop_front();history.push_back(msg);if(msg.bytes[4]==10&&dropAck){--dropAck;continue;}for(auto *n:nodes)if(n->endpoint==msg.to&&(!n->legacy||msg.bytes[4]<=6))n->h.receive(msg.from,msg.bytes.data(),msg.bytes.size(),now);}}
int main(){
 {
  Bus physical;Node rotor(physical,21,false),other(physical,22,false),hud(physical,23,true);
  hud.select(rotor);
  for(unsigned t=100;t<=800;t+=5)physical.tick(t);
  assert(hud.h.manualAvailable());
  LocalAxisInput input(hud.c,3);
  auto sample=[&](float az,float el,uint8_t neutral,uint32_t now){input.sample(az,el,neutral,hud.h.manualAvailable(),now,hud.h.selectionEpoch);physical.tick(now);};
  sample(0,0,3,810);sample(0,0,3,850);
  for(unsigned t=860;t<=1000;t+=10)sample(.5f,-1,0,t);
  assert(rotor.c.axes[0].wanted==1 && rotor.c.axes[1].wanted==-1);
  sample(0,0,3,1010);physical.tick(1060);
  assert(!rotor.c.axes[0].wanted && !rotor.c.axes[1].wanted);
  sample(0,0,3,1070);sample(1,0,2,1080);physical.tick(1130);
  hud.select(other);physical.tick(1140);
  for(unsigned t=1150;t<=1400;t+=10)sample(1,0,2,t);
  assert(!other.c.axes[0].wanted && !rotor.c.axes[0].wanted);
  sample(0,0,3,1410);sample(0,0,3,1450);sample(1,0,2,1460);physical.tick(1510);
  assert(other.c.axes[0].wanted==1);
  hud.c.target({80,0,true,false},false,1520);physical.tick(1520);
  sample(0,0,3,1530);physical.tick(1580);
  assert(other.c.axes[0].tracking && other.c.axes[0].target==80);
  const auto request=hud.h.requestId();hud.c.configurationLocked=true;
  for(unsigned t=1590;t<1650;t+=10)hud.c.tick(t);
  assert(hud.h.requestId()==request); // Calibration lock must not flood remote STOPs.
  assert(hud.c.jog(0,1,false,1650)==Result::Unavailable);
 }
 Bus bus;Node rotor(bus,1,false),a(bus,2,true),b(bus,3,true),c(bus,4,true),d(bus,5,true),fifth(bus,6,true);
 for(auto *hud:{&a,&b,&c,&d,&fifth})hud->select(rotor);
 for(unsigned t=100;t<=800;t+=5)bus.tick(t);
 assert(a.h.active()->live&&b.h.active()->live&&c.h.active()->live&&d.h.active()->live);
 assert(!fifth.h.active()->live&&fifth.h.available()); // bounded subscription table, legacy fallback
 const auto before=bus.history.size();
 rotor.c.setMode(Mode::Auto,805);rotor.c.target({90,0,true,false},false,805);rotor.c.tick(805);bus.tick(805);
 for(auto *hud:{&a,&b,&c,&d})assert(hud->c.axes[0].target==90);
 WirePacket saved;for(size_t i=before;i<bus.history.size();++i)if(bus.history[i].bytes[4]==8){assert(bus.history[i].bytes.size()<=256);saved=bus.history[i];}
 a.c.stopNow(-1,810);const auto request=a.h.requestId();bus.tick(810);bus.tick(815);
 assert(!rotor.c.moving()&&a.h.result(request).result==Result::Ok);
 bool compactAck=false;for(const auto &p:bus.history)if(p.bytes[4]==10){compactAck=true;assert(p.bytes.size()<=256);}assert(compactAck);
 // Lost compact ACK retries the same transaction, never executes STOP twice.
 const auto epoch=rotor.c.stopEpoch;bus.dropAck=1;
 a.c.stopNow(-1,820);const auto lost=a.h.requestId();bus.tick(820);
 assert(rotor.c.stopEpoch==epoch+1&&a.h.result(lost).result==Result::Deferred);
 for(unsigned t=825;t<=1000;t+=5)bus.tick(t);
 assert(rotor.c.stopEpoch==epoch+1&&a.h.result(lost).result==Result::Ok&&rotor.h.duplicates);
 // An old telemetry packet cannot renew freshness after its local challenge expires.
 const auto age=d.h.active()->receivedAt;d.h.service(3000);d.h.receive(saved.from,saved.bytes.data(),saved.bytes.size(),3000);assert(d.h.active()->receivedAt==age&&!d.h.available());
 rotor.h.begin(1,999);bus.queue.clear();for(unsigned t=3010;t<=4600;t+=5)bus.tick(t);
 assert(a.h.active()->boot==999&&a.h.active()->live); // lower stream revision after reboot is accepted only after new full snapshot
 Bus old;Node legacy(old,10,false),modern(old,11,true);legacy.legacy=true;modern.select(legacy);
 for(unsigned t=100;t<=700;t+=5)old.tick(t);
 assert(modern.h.available()&&!modern.h.active()->live);
 modern.c.target({80,0,true,false},false,705);old.tick(705);assert(legacy.c.axes[0].target==80);
 std::cout<<"PASS: four HUD subscriptions, immediate fanout, compact ACK/loss/dedup, fifth/legacy fallback, stale challenge, reboot\n";
}
