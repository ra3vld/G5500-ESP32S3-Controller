#include "motion_fixture.hpp"
#include "modbus_registers.hpp"
#include "modbus_token.hpp"
#include "modbus_bus.hpp"
#include <cassert>
#include <deque>
#include <iostream>
using namespace forkrot;
static mb::Frame read(uint16_t start,uint16_t count){mb::Frame f;f.data[0]=1;f.data[1]=3;mb::put16(f.data+2,start);mb::put16(f.data+4,count);f.finish(6);return f;}
static mb::Frame command(uint32_t id,uint16_t op,uint32_t ticket=100){
 mb::Frame f;f.data[0]=1;f.data[1]=16;mb::put16(f.data+2,0x100);mb::put16(f.data+4,16);f.data[6]=32;
 auto *p=f.data+7;mb::put16(p,7);mb::put32(p+2,123);mb::put32(p+6,id);mb::put32(p+10,456);mb::put32(p+14,ticket);
 mb::put16(p+18,op);mb::put16(p+20,3);mb::put32(p+22,120000);mb::put32(p+26,30000);mb::put16(p+30,0);f.finish(39);return f;
}
int main(){
  // HUD/5 absolute input keeps release priority even with its speed flag set.
  {
    uint8_t command[64]{}; command[3]='5'; command[4]=4;
    uint8_t manual[64]{}; manual[3]='5'; manual[4]=15; manual[54]=2;
    mb::Mailbox transport; transport.begin(1);
    assert(transport.enqueue(command,sizeof command,0));
    assert(transport.enqueue(command,sizeof command,0));
    assert(!transport.enqueue(command,sizeof command,0));
    assert(transport.enqueue(manual,sizeof manual,0));
    assert(!transport.enqueue(command,sizeof command,0)); // Running input cannot evict commands.
    manual[54]=3; assert(transport.enqueue(manual,sizeof manual,0));
    assert(transport.enqueue(command,sizeof command,0)); // Release cleared the older commands.
  }
 // Every supported physical format drives framing and slow token exchanges.
 for(uint32_t baud:{1200u,9600u,38400u,115200u})for(uint8_t parity:{0,1,2})for(uint8_t stops:{1,2}) {
   UartConfig line{baud,parity,stops};assert(line.valid());
   assert(line.frameBits()==9+unsigned(parity!=0)+stops);
   mb::Framer receiver;const auto gap=mb::t35(baud,line.frameBits());
   receiver.reset(0);const auto query=read(0,4);uint32_t at=gap+1;unsigned frames=0;
   auto receive=[&](const mb::Frame &frame){assert(frame.valid());++frames;};
   for(unsigned i=0;i<query.size;++i){receiver.feed(query.data[i],at,baud,receive,line.frameBits());at+=line.wireUs(1);}
   receiver.poll(at+gap,baud,receive,line.frameBits());assert(frames==1);
 }
 for(uint32_t baud:{1200u,9600u,38400u}) {
   Controller controllers[3]={testController(),testController(),testController()};
   Protocol protocols[3]={Protocol(controllers[0]),Protocol(controllers[1]),Protocol(controllers[2])};
   Hud huds[3]={Hud(controllers[0],protocols[0]),Hud(controllers[1],protocols[1]),Hud(controllers[2],protocols[2])};
   mb::Bus nodes[3]={mb::Bus(controllers[0],huds[0]),mb::Bus(controllers[1],huds[1]),mb::Bus(controllers[2],huds[2])};
   ModbusConfig config;config.bus=1;config.count=2;config.uart1={baud,2,2};
   for(unsigned i=0;i<3;++i){config.node=i==2?202:201;assert(nodes[i].begin(config,i!=0,42+i,0));}
   struct Packet {mb::Frame frame;unsigned source;uint32_t due;};std::deque<Packet> wire;
   uint32_t busyUntil=0;bool queued=false,answered=false;unsigned passed=0;
   const uint32_t until=config.tokenRecoveryUs()+config.tokenLeaseUs()*20;
   for(uint32_t now=1000;now<until;now+=1000) {
     auto emit=[&](unsigned source,const mb::Frame &f){
       if(now<busyUntil)return false;
       busyUntil=now+config.uart1.wireUs(f.size)+mb::t35(baud,config.uart1.frameBits());
       wire.push_back({f,source,busyUntil});return true;
     };
     if(!wire.empty()&&now>=wire.front().due) {
       auto packet=wire.front();wire.pop_front();packet.frame.at=packet.due-mb::t35(baud,config.uart1.frameBits());
       for(unsigned i=0;i<3;++i)if(i!=packet.source)nodes[i].receive(packet.frame,now,now/1000,
         [&](const mb::Frame &f){return emit(i,f);},[](uint8_t,const uint8_t *,size_t,uint32_t){});
     }
     if(!queued&&nodes[1].owner()){assert(nodes[1].pcRequest(read(0,32),now));queued=true;}
     for(unsigned i=0;i<3;++i)nodes[i].service(now,now/1000,now>=busyUntil,[&](const mb::Frame &f){return emit(i,f);});
     mb::Frame reply;if(nodes[1].pcReply(reply)){assert(reply.valid()&&reply.data[1]==3&&reply.size==69);answered=true;}
     passed=nodes[1].passes()+nodes[2].passes();
     if(answered&&passed>=2)break;
   }
   assert(queued&&answered&&passed>=2&&!nodes[1].fault()&&!nodes[2].fault());
 }
 const uint8_t vector[]={1,3,0,0,0,10};assert(mb::crc(vector,sizeof vector)==0xcdc5);
 Controller c=testController();Protocol p(c);Hud h(c,p);h.begin(1,456);c.feedback(20,30,true,true);c.setMode(Mode::Auto,0);
 mb::Registers registers(c,h);registers.begin(456);
 auto r=registers.process(read(0,32),100);assert(r.valid()&&r.size==69&&mb::get16(r.data+3)==1);
 auto target=command(1,1);r=registers.process(target,100);assert(r.valid()&&r.size==8&&c.axes[0].target==120&&c.axes[1].target==30);
 auto stop=command(2,2);const auto epoch=c.stopEpoch;r=registers.process(stop,101);assert(r.valid()&&c.stopEpoch==epoch+1);
 r=registers.process(stop,102);assert(r.size==8&&c.stopEpoch==epoch+1);
 auto changed=stop;mb::put16(changed.data+7+18,3);changed.finish(39);assert(registers.process(changed,103).data[2]==3);
 assert(registers.process(command(3,1,0),2101).data[2]==3);
 registers.begin(999);assert(registers.process(command(4,1),110).data[2]==3);
 assert(registers.process(read(63,2),100).data[2]==2);
 registers.begin(456);
 auto manual=[&](uint32_t id,int32_t value,bool release,uint16_t op=6){auto f=command(id,op);mb::put16(f.data+27,1);mb::put32(f.data+29,uint32_t(value));mb::put32(f.data+33,0);mb::put16(f.data+37,release?1:0);f.finish(39);return registers.process(f,120+id);};
 assert(manual(10,500,false).data[1]==16&&c.axes[0].wanted==1);
 assert(manual(11,0,true).data[1]==16&&c.axes[0].wanted==0);
 assert(manual(12,-500,false).data[1]==16&&c.axes[0].wanted==-1);
 assert(manual(13,0,false,2).data[1]==16&&c.axes[0].wanted==0);
 assert(manual(14,-500,false).data[1]==16&&c.axes[0].wanted==0); // STOP blocks the held gesture.
 assert(manual(15,0,true).data[1]==16);
 assert(manual(16,500,false).data[1]==16&&c.axes[0].wanted==1);
 mb::Framer framer;framer.reset(0);framer.poll(2000,115200,[](const mb::Frame &){});unsigned delivered=0;uint32_t t=3000;
 auto receive=[&](const mb::Frame &f){assert(f.valid());++delivered;};
 const auto q=read(0,4);for(unsigned i=0;i<q.size;++i){framer.feed(q.data[i],t,115200,receive);t+=96;}
 framer.poll(t+mb::t35(115200),115200,receive);assert(delivered==1);
 framer.feed(1,t+5000,115200,receive);framer.feed(3,t+6000,115200,receive);framer.poll(t+10000,115200,receive);assert(delivered==1&&framer.rejected);
 struct Node {mb::Token token;ModbusConfig config;bool online=true;};Node nodes[3];
 for(unsigned i=0;i<3;++i){nodes[i].config.count=3;nodes[i].config.node=201+i;nodes[i].token.begin(nodes[i].config,true,100+i,0);}
 std::deque<mb::Frame> wire;unsigned grants=0;
 bool dropGrantAck=false,dropGrantOnce=false;mb::Frame lastGrant;
 auto tick=[&](uint32_t now){
   for(auto &n:nodes)if(n.online)n.token.service(now,true,false,[&](const mb::Frame &f){wire.push_back(f);return true;});
   unsigned budget=100;
   while(!wire.empty()){
     assert(budget--);auto f=wire.front();wire.pop_front();if(f.data[3]==mb::Token::Grant){++grants;lastGrant=f;if(dropGrantOnce){dropGrantOnce=false;continue;}}
     for(auto &n:nodes)if(n.online){
       if(f.data[4]==n.config.node && (f.data[3]==mb::Token::Claim || f.data[3]==mb::Token::Grant || f.data[3]==mb::Token::Probe)){n.token.activity(now);continue;}
       if(dropGrantAck&&f.data[3]==mb::Token::GrantAck)continue;
       n.token.observe(f,now,[&](const mb::Frame &reply){wire.push_back(reply);return true;});
     }
   }
   unsigned owners=0;for(auto &n:nodes)if(n.online&&n.token.owns(now))++owners;assert(owners<=1);
 };
 for(uint32_t now=1000;now<1200000;now+=1000)tick(now);
 unsigned owners=0;for(auto &n:nodes)if(n.token.owns(1200000))++owners;assert(owners==1);
 dropGrantAck=true;
 for(uint32_t now=1200000;now<2000000;now+=1000){for(auto &n:nodes)if(n.token.owns(now))n.token.pass();tick(now);}
 assert(grants>3);
 nodes[0].online=false;
 for(uint32_t now=2000000;now<4000000;now+=1000){for(auto &n:nodes)if(n.online&&n.token.owns(now))n.token.pass();tick(now);}
 assert(nodes[1].token.passes+nodes[2].token.passes>3);
 for(auto &n:nodes)if(n.token.owns(4000000))n.online=false;
 for(uint32_t now=4000000;now<6000000;now+=1000)tick(now);
 owners=0;for(auto &n:nodes)if(n.online&&n.token.owns(6000000))++owners;assert(owners==1);

 // Fragmentation, duplicate responses and independent HUD mailboxes.
 mb::Mailbox a,b;a.begin(10);b.begin(20);
 uint8_t packet[HUD_FRAME_MAX]{};for(unsigned i=0;i<sizeof packet;++i)packet[i]=uint8_t(i);packet[4]=2;
 assert(a.enqueue(packet,sizeof packet,0));unsigned got=0,back=0;
 for(unsigned i=0;i<8;++i) {
   auto q=a.request(1,201,i*5);
   auto answer=b.respond(q,201,i*5,[&](const uint8_t *data,size_t size){assert(size==sizeof packet&&!std::memcmp(data,packet,size));++got;assert(b.enqueue(packet,size,i*5));});
   auto cb=[&](const uint8_t *data,size_t size){assert(size==sizeof packet&&!std::memcmp(data,packet,size));++back;};
   assert(a.accept(answer,201,cb));assert(a.accept(answer,201,cb));
 }
 assert(got==1&&back==1&&a.complete());
 // Actual core-to-core discovery and GS-232 forwarding with two token HUDs.
 Controller cc=testController(),hc1=testController(),hc2=testController();
 Protocol cp(cc),hp1(hc1),hp2(hc2);Hud ch(cc,cp),hh1(hc1,hp1),hh2(hc2,hp2);
 ch.begin(11,111);hh1.config.role=hh2.config.role=DeviceRole::Hud;hh1.config.uart=hh2.config.uart=true;hh1.begin(21,222);hh2.begin(31,333);
 cc.feedback(25,30,true,true);cc.setMode(Mode::Auto,0);
 struct Pipe:HudTransport {mb::Bus *bus=nullptr;uint32_t now=0;bool sendHud(const HudEndpoint &e,const uint8_t *p,size_t n)override{return e.uart&&bus->enqueue(uint8_t(e.address),p,n,now);}} pipes[4];
 Controller other=testController();Protocol otherProtocol(other);Hud otherHud(other,otherProtocol);otherHud.begin(12,112);other.feedback(55,15,true,true);other.setMode(Mode::Auto,0);
 mb::Bus otherBus(other,otherHud);
 mb::Bus controllerBus(cc,ch),hud1(hc1,hh1),hud2(hc2,hh2);mb::Bus *buses[]={&controllerBus,&hud1,&hud2,&otherBus};Hud *huds[]={&ch,&hh1,&hh2,&otherHud};
 ModbusConfig cfg;cfg.bus=1;cfg.count=2;
 assert(controllerBus.begin(cfg,false,111,0));assert(hud1.begin(cfg,true,222,0));cfg.node=202;assert(hud2.begin(cfg,true,333,0));
 cfg.address=3;assert(otherBus.begin(cfg,false,112,0));cfg.address=1;
 for(unsigned i=0;i<4;++i){pipes[i].bus=buses[i];huds[i]->setTransport(&pipes[i]);}
 struct Wire {mb::Frame f;unsigned source;uint32_t due;};std::deque<Wire> packets;
 uint32_t busFree=0;bool lost=false,pcFlood=false,pcOutstanding=false;unsigned pcReplies=0;
 auto run=[&](uint32_t from,uint32_t until) {
   for(uint32_t us=from;us<until;us+=1000) {
     for(auto &pipe:pipes)pipe.now=us/1000;
     if(!packets.empty()&&int32_t(us-packets.front().due)>=0) {
       auto p=packets.front();packets.pop_front();p.f.at=us-2000;
       for(unsigned i=0;i<4;++i)if(i!=p.source) {
         auto emit=[&,i](const mb::Frame &f){if(int32_t(us-busFree)<0)return false;packets.push_back({f,i,us+uint32_t(f.size*96+2000)});busFree=packets.back().due;return true;};
         buses[i]->receive(p.f,us,us/1000,emit,[&,i](uint8_t address,const uint8_t *data,size_t n,uint32_t at){huds[i]->receive({address,4534,true},data,n,at);});
       }
     }
     for(unsigned i=0;i<4;++i){
       buses[i]->service(us,us/1000,int32_t(us-busFree)>=0,[&,i](const mb::Frame &f){
         if(int32_t(us-busFree)<0)return false;
         if(!lost&&f.data[1]==mb::Service&&f.data[3]==mb::Mailbox::Reply){lost=true;busFree=us+uint32_t(f.size*96+2000);return true;}
         packets.push_back({f,i,us+uint32_t(f.size*96+2000)});busFree=packets.back().due;return true;
       });huds[i]->service(us/1000);
     }
     if(pcFlood){mb::Frame answer;if(hud2.pcReply(answer)){++pcReplies;pcOutstanding=false;}if(!pcOutstanding)pcOutstanding=hud2.pcRequest(read(0,4),us);}
   }
 };
 run(0,1200000);assert(hh1.discover({1,4534,true}));assert(hh2.discover({1,4534,true}));run(1200000,3000000);
 assert(lost&&hh1.peer(11)&&hh2.peer(11));assert(hh1.add(11,{1,4534,true},"controller"));assert(hh2.add(11,{1,4534,true},"controller"));assert(hh1.select(11)&&hh2.select(11));run(3000000,4200000);
 auto result=hp1.line("W130 040",4200);assert(result.result==Result::Deferred);run(4200000,4800000);
 assert(cc.axes[0].target==130&&cc.axes[1].target==40);
 // The ordinary PC RTU client uses the same ring without knowing token frames.
 auto query=read(0,4);assert(hud2.pcRequest(query,4800000));run(4800000,5300000);mb::Frame pcAnswer;
 assert(hud2.pcReply(pcAnswer)&&pcAnswer.valid()&&pcAnswer.data[1]==3&&pcAnswer.data[2]==8);
 assert(controllerBus.passes()==0&&hud1.passes()&&hud2.passes());
 const auto previousGrants=grants;
 for(auto &n:nodes){n.online=true;n.token.service(6100000,true,false,[](const mb::Frame &){return false;});}
 nodes[0].token.begin(nodes[0].config,true,1,6100000);
 for(uint32_t now=6101000;now<8200000;now+=1000){for(auto &n:nodes)if(n.token.owns(now))n.token.pass();tick(now);}
 assert(grants>previousGrants+3); // Rebooted first node can recover with a lower epoch.
 dropGrantOnce=true;bool recoveredAfterGrant=false;
 for(uint32_t now=8200000;now<10400000;now+=1000){for(auto &n:nodes)if(n.token.owns(now))n.token.pass();tick(now);if(now>9600000)for(auto &n:nodes)if(n.token.owns(now))recoveredAfterGrant=true;}
 assert(!dropGrantOnce&&recoveredAfterGrant);
 // A repeated grant cannot create another owner; mismatched rings fail closed.
 wire.push_back(lastGrant);tick(10400000);
 mb::Token mismatch;ModbusConfig wrong;wrong.count=2;mismatch.begin(wrong,true,1,0);
 assert(mismatch.observe(lastGrant,11000000,[](const mb::Frame &){return true;})&&mismatch.fault&&!mismatch.owns(11000000));
 auto broadcast=read(0,4);broadcast.data[0]=0;broadcast.finish(6);assert(!hud2.pcRequest(broadcast,5300000));
 auto reserved=read(0,4);reserved.data[0]=201;reserved.finish(6);assert(!hud2.pcRequest(reserved,5300000));assert(hud2.pcReply(pcAnswer)&&pcAnswer.data[2]==2);
 mb::Bus abandoned(hc1,hh1);cfg.count=1;cfg.node=201;assert(abandoned.begin(cfg,true,77,0));assert(abandoned.pcRequest(read(0,4),0));
 abandoned.service(1600000,1600,true,[](const mb::Frame &){return false;});assert(abandoned.pcReply(pcAnswer)&&pcAnswer.data[2]==11);
 const auto stopped=cc.stopEpoch;assert(hp1.line("S",5300).result==Result::Deferred);run(5300000,5900000);assert(cc.stopEpoch==stopped+1);
 pcFlood=true;run(5900000,8900000);assert(pcReplies>2&&hh1.available()&&hh2.available());
 // Sequential bus discovery skips an empty address and saves each routed endpoint.
 assert(hh1.discoverRs485(1,4,201,2));run(8900000,13900000);
 assert(hh1.peer(12)&&hh1.peer(12)->endpoint.address==3);
 assert(hh1.add(12,hh1.peer(12)->endpoint,"second"));assert(hh1.select(12));run(13900000,14900000);
 assert(hp1.line("W210 060",14900).result==Result::Deferred);run(14900000,15600000);
 assert(other.axes[0].target==210&&other.axes[1].target==60&&cc.axes[0].target==130);
 const auto firstStop=cc.stopEpoch,otherStop=other.stopEpoch;
 assert(hp1.line("S",15600).result==Result::Deferred);run(15600000,16300000);
 assert(other.stopEpoch==otherStop+1&&cc.stopEpoch==firstStop);
 assert(hh1.select(11));run(16300000,17300000);assert(hh1.available());
 // Scan must never send HUD traffic to master addresses, including the range end.
 assert(hh1.discoverRs485(201,202,201,2));run(17300000,17400000);
 char scanJson[4096];JsonWriter scanOut(scanJson,sizeof scanJson);hh1.writePeers(scanOut,0,8,true);
 assert(std::string(scanJson).find("\"scanActive\":false")!=std::string::npos);
 // RTU microsecond wrap must not reset the controller/HUD millisecond clock.
 Controller longController=testController();Protocol longProtocol(longController);Hud longHud(longController,longProtocol);longHud.begin(51,444);
 mb::Bus longBus(longController,longHud);cfg.count=1;cfg.node=201;
 const uint32_t startUs=uint32_t(5000000000ULL),startMs=5000000;assert(longBus.begin(cfg,true,444,startUs));bool queued=false,transmitted=false;
 for(uint32_t dt=1000;dt<1100000;dt+=1000){
   if(longBus.owner()==201&&!queued){assert(longBus.enqueue(1,packet,sizeof packet,startMs+dt/1000));queued=true;}
   longBus.service(startUs+dt,startMs+dt/1000,true,[&](const mb::Frame &f){if(f.data[1]==mb::Service&&f.data[3]==mb::Mailbox::Exchange&&f.size>25)transmitted=true;return true;});
 }
 assert(queued&&transmitted);
 mb::Bus longSlave(longController,longHud);assert(longSlave.begin(cfg,false,444,startUs));auto clockQuery=read(26,2);clockQuery.at=startUs;
 longSlave.receive(clockQuery,startUs+2000,startMs,[](const mb::Frame &){return false;},[](uint8_t,const uint8_t *,size_t,uint32_t){});
 bool clockRead=false;longSlave.service(startUs+2000,startMs,true,[&](const mb::Frame &f){clockRead=f.data[1]==3&&mb::get32(f.data+3)==startMs;return true;});assert(clockRead);
 std::cout<<"PASS: Modbus CRC/framing/registers, atomic targets, duplicate STOP, stale tickets/boot, token rotation/lost ACK/dead owner/reboot, fragmented HUD discovery/GS-232/STOP and PC gateway\n";
}
