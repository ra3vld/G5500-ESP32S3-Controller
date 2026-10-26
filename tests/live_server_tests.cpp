#include "motion_fixture.hpp"
#include "live_server.hpp"
#include <cassert>
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <new>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <esp_heap_caps.h>
#include "heap_metrics.hpp"
#include <esp_timer.h>
#include <tcp_peer.hpp>
static size_t allocationCalls=0, releasedCalls=0, allocatedBytes=0;
[[gnu::noinline]] void *operator new(size_t size) {
  if(void *p=std::malloc(size?size:1)){++allocationCalls;allocatedBytes+=size;return p;}
  throw std::bad_alloc();
}
void *operator new[](size_t size) {return ::operator new(size);}
void *operator new(size_t size,const std::nothrow_t &) noexcept {
  try {return ::operator new(size);} catch(...) {return nullptr;}
}
void *operator new[](size_t size,const std::nothrow_t &tag) noexcept {return ::operator new(size,tag);}
[[gnu::noinline]] void operator delete(void *p) noexcept {if(p)++releasedCalls;std::free(p);}
void operator delete[](void *p) noexcept {::operator delete(p);}
void operator delete(void *p,size_t) noexcept {::operator delete(p);}
void operator delete[](void *p,size_t) noexcept {::operator delete(p);}
void operator delete(void *p,const std::nothrow_t &) noexcept {::operator delete(p);}
void operator delete[](void *p,const std::nothrow_t &) noexcept {::operator delete(p);}
using namespace forkrot;
// IDF 5.5.5 performs the handshake without invoking the URI handler.
static int connect(int fd){const int rc=fakeServer.cfg.open_fn(&fakeServer,fd);if(rc==ESP_OK){fakeServer.websocketFds.insert(fd);httpd_req_t req{fakeServer.uri.user_ctx,HTTP_GET,fd,{}};return fakeServer.uri.handler(&req);}return rc;}
static int command(int fd,const std::string &data){httpd_req_t r{fakeServer.uri.user_ctx,HTTP_POST,fd,data};return fakeServer.uri.handler(&r);}
static std::string packet(unsigned id,const char *path,const char *args=""){return "v=1&boot=a&selection=0&cid="+std::to_string(id)+"&path="+path+args;}
int main(){
 fakeEspTimeUs=0;
 MotionTestRuntime runtime;runtime.hud.begin(1,10);runtime.controller.feedback(20,30,true,true);
 LiveServer server(runtime);const auto beforeBegin=allocatedBytes;
 assert(server.begin());assert(fakeServer.registrations==std::vector<std::string>({"/api/live","/api/control"}));assert(fakeServer.cfg.max_open_sockets==5&&fakeServer.cfg.backlog_conn==1);assert(fakeServer.uri.handle_ws_control_frames);
 const auto bufferMetric=[&](const char *name){
   char text[2048];JsonWriter j(text,sizeof text);server.writeDiagnostics(j);assert(j.good());
   const std::string json(j.data(),j.size()),key=std::string("\"")+name+"\":";
   const auto at=json.find(key);assert(at!=std::string::npos);return std::stoul(json.substr(at+key.size()));
 };
 assert(allocatedBytes-beforeBegin<=512); // idle server has no queues or full-state buffer
 const auto beforeLowHeap=allocationCalls;
 fakeHeapFree=MemoryGuard::CriticalFree;
 assert(connect(999)==ESP_FAIL && allocationCalls==beforeLowHeap);
 fakeHeapFree=65536;fakeHeapBlock=MemoryGuard::CriticalBlock;
 assert(connect(999)==ESP_FAIL && allocationCalls==beforeLowHeap);
 fakeHeapFree=MemoryGuard::HealthyFree;fakeHeapBlock=32768;
 assert(connect(999)==ESP_FAIL && allocationCalls==beforeLowHeap);
 fakeHeapFree=65536;fakeHeapBlock=MemoryGuard::HealthyBlock;
 assert(connect(999)==ESP_FAIL && allocationCalls==beforeLowHeap);
 fakeHeapBlock=32768;
 for(unsigned i=0;i<10000;++i) {
   const auto allocations=allocationCalls, releases=releasedCalls, bytes=allocatedBytes;
   const auto aborts=fakeAbortiveCloses;
   assert(fakeServer.cfg.open_fn(&fakeServer,999)==ESP_OK);
   assert(allocationCalls==allocations && allocatedBytes==bytes);
   fakeServer.cfg.close_fn(&fakeServer,999);
   assert(releasedCalls==releases);
   assert(fakeAbortiveCloses==aborts+1 && fakeAbortiveCloseFd==999);
 }
 // Accepted but silent sockets consume no per-client queues or state snapshot.
 assert(fakeServer.cfg.open_fn(&fakeServer,998)==ESP_OK);
 const auto handshakeIdleBytes=bufferMetric("liveBufferBytes");
 fakeEspTimeUs=4999000;server.publish(4999);pumpLive();assert(bufferMetric("liveClients")==1);
 fakeEspTimeUs=5004000;server.publish(5004);pumpLive();
 assert(bufferMetric("liveClients")==0&&bufferMetric("liveBufferBytes")==handshakeIdleBytes);
 assert(bufferMetric("liveHandshakeTimeouts")==1);fakeEspTimeUs=-1;
 assert(fakeServer.cfg.open_fn(&fakeServer,999)==ESP_OK);
 server.publish(5);pumpLive();assert(fakeServer.sent.empty()); // no frame before upgrade
 fakeServer.websocketFds.insert(999);server.publish(10);pumpLive();
 server.publish(15);pumpLive(); // IDF upgraded without calling our URI handler.
 assert(fakeServer.sent.size()==1&&fakeServer.sent.back().second.find("\"type\":\"state\"")!=std::string::npos);
 httpd_req_t legacy{fakeServer.uri.user_ctx,HTTP_GET,999,{}};assert(fakeServer.uri.handler(&legacy)==ESP_OK);
 httpd_sess_trigger_close(&fakeServer,999);fakeServer.sent.clear();
 for(int fd=1000;fd<1004;++fd)assert(connect(fd)==ESP_OK);
 server.publish(20);pumpLive(); // allocate the shared snapshot before inducing pressure
 // Radio allocations happen after send() returns: defer bulk frames while
 // either byte or contiguous headroom is low, retaining their exact offset.
 for(bool fragmented:{false,true}) {
   const auto sent=fakeServer.sent.size();
   runtime.controller.axes[0].target+=1;
   fakeHeapFree=fragmented?65536:24575;fakeHeapBlock=fragmented?8191:32768;
   server.publish(fragmented?25:15);pumpLive();
   assert(fakeServer.sent.size()==sent);
   fakeHeapFree=65536;fakeHeapBlock=32768;
   server.publish(fragmented?30:20);pumpLive();
   assert(fakeServer.sent.size()>sent);
 }
 char pressureText[2048];JsonWriter pressure(pressureText,sizeof pressureText);server.writeDiagnostics(pressure);
 const std::string pressureDetails(pressure.data(),pressure.size());
 assert(pressureDetails.find("\"liveMemoryDeferred\":8")!=std::string::npos);
 assert(pressureDetails.find("\"liveSocketDeferred\":0")!=std::string::npos);
 // An established peer below the burst reserve must keep receiving complete
 // frames, including when pressure starts midway through a frame. Retain
 // independent byte offsets and cap each peer's write to 512 bytes per turn.
 for(bool fragmented:{false,true}) {
   const auto sentBefore=fakeServer.sent.size(),errorsBefore=bufferMetric("liveSendErrors");
   runtime.controller.axes[0].target+=1;
   fakeServer.maxWrite=113;
   server.publish(fragmented?70:40);pumpLive();
   fakeServer.maxWrite=SIZE_MAX;
   fakeHeapFree=fragmented?65536:28672;fakeHeapBlock=fragmented?10240:32768;
   for(unsigned i=0;i<12 && fakeServer.sent.size()<sentBefore+4;++i) {
     size_t bytes[4];for(unsigned peer=0;peer<4;++peer)bytes[peer]=fakeServer.wire.at(1000+peer).size();
     const auto before=fakeEspTimeUs;
     server.publish((fragmented?75:45)+i*5);pumpLive();
     assert(fakeEspTimeUs==before);
     for(unsigned peer=0;peer<4;++peer)assert(fakeServer.wire.at(1000+peer).size()-bytes[peer]<=512);
   }
   assert(fakeServer.sent.size()==sentBefore+4 && bufferMetric("liveSendErrors")==errorsBefore);
   for(int peer=1000;peer<1004;++peer)assert(fakeServer.pendingWire.at(peer).empty());
   fakeHeapFree=65536;fakeHeapBlock=32768;
 }
 // Exercise the production send override with real short writes and a reader
 // paused longer than the previous 5 ms timeout. No radio or motors are used.
 assert(fakeSendOverride);
 int pair[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
 int small=1024;assert(setsockopt(pair[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof small)==0);
 const std::string payload(20000,'x');std::string received;
 std::thread reader([&]{
   std::this_thread::sleep_for(std::chrono::milliseconds(20));
   char chunk[1024];int n;
   while((n=recv(pair[1],chunk,sizeof chunk,0))>0)received.append(chunk,size_t(n));
 });
 const int sent=fakeSendOverride(&fakeServer,pair[0],payload.data(),payload.size(),0);
 shutdown(pair[0],SHUT_WR);reader.join();close(pair[0]);close(pair[1]);
 assert(sent==int(payload.size()) && received==payload);
 // A receiver that stops draining TCP must leave a precise timeout record,
 // including the offset of a partly sent frame, rather than only ESP_FAIL.
 assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
 assert(setsockopt(pair[0],SOL_SOCKET,SO_SNDBUF,&small,sizeof small)==0);
 assert(fakeSendOverride(&fakeServer,pair[0],payload.data(),payload.size(),0)==-1);
 assert(errno==ETIMEDOUT);
 char details[2048];JsonWriter diagnostic(details,sizeof details);server.writeDiagnostics(diagnostic);
 const std::string timeoutDetails(diagnostic.data(),diagnostic.size());
 assert(timeoutDetails.find("\"liveSendTimeouts\":1")!=std::string::npos);
 assert(timeoutDetails.find("\"liveSendErrno\":"+std::to_string(ETIMEDOUT))!=std::string::npos);
 assert(timeoutDetails.find("\"liveSendRetryErrno\":"+std::to_string(EAGAIN))!=std::string::npos);
 assert(timeoutDetails.find("\"liveSendRequested\":20000")!=std::string::npos);
 const auto offset=timeoutDetails.find("\"liveSendWritten\":");assert(offset!=std::string::npos);
 const auto partial=std::stoul(timeoutDetails.substr(offset+18));assert(partial>0&&partial<payload.size());
 const auto elapsed=timeoutDetails.find("\"liveSendElapsedMs\":");assert(elapsed!=std::string::npos);
 assert(std::stoul(timeoutDetails.substr(elapsed+20))>=100);
 close(pair[0]);close(pair[1]);
 assert(fakeSendOverride(&fakeServer,-1,payload.data(),payload.size(),0)==-1&&errno==EBADF);
 JsonWriter fatal(details,sizeof details);server.writeDiagnostics(fatal);
 const std::string fatalDetails(fatal.data(),fatal.size());
 assert(fatalDetails.find("\"liveSendErrno\":"+std::to_string(EBADF))!=std::string::npos);
 assert(fatalDetails.find("\"liveSendWritten\":0")!=std::string::npos);
 assert(fatalDetails.find("\"liveSendTimeouts\":1")!=std::string::npos);

 // Retiring a real TCP session resets the transport instead of leaving a
 // graceful close with queued data behind a replacement browser connection.
 const int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
 sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(bind(listener,reinterpret_cast<sockaddr *>(&address),sizeof address)==0);
 assert(listen(listener,1)==0);socklen_t addressSize=sizeof address;
 assert(getsockname(listener,reinterpret_cast<sockaddr *>(&address),&addressSize)==0);
 const int peer=socket(AF_INET,SOCK_STREAM,0);assert(peer>=0);
 assert(::connect(peer,reinterpret_cast<sockaddr *>(&address),sizeof address)==0);
 const int retired=accept(listener,nullptr,nullptr);assert(retired>=0);
 const timeval receiveDeadline{1,0};
 assert(setsockopt(peer,SOL_SOCKET,SO_RCVTIMEO,&receiveDeadline,sizeof receiveDeadline)==0);
 assert(send(retired,"retired telemetry",17,0)==17);
 fakeServer.cfg.close_fn(&fakeServer,retired);
 char discarded[128];int lastRead;
 do{lastRead=recv(peer,discarded,sizeof discarded,0);}while(lastRead>0);
 assert(lastRead<0 && errno==ECONNRESET);
 close(peer);close(listener);

 const auto beforeExcess=allocationCalls;
 assert(connect(1004)==ESP_FAIL&&allocationCalls==beforeExcess);
 assert(command(1000,packet(1,"/api/target","&az=90"))==ESP_OK);
 assert(runtime.controller.axes[0].target!=90); // HTTPD cannot execute a control command.
 server.commands(5);assert(runtime.controller.axes[0].target==90);
 server.publish(5);pumpLive();server.publish(10);pumpLive();
 assert(!fakeServer.sent.empty());
 assert(command(1000,packet(2,"/api/target","&az=200"))==ESP_OK);
 assert(command(1000,packet(3,"/api/move","&d=S"))==ESP_OK);
 fakeHeapFree=1024;fakeHeapBlock=256; // admitted client's STOP needs no new queue
 server.commands(15);assert(!runtime.controller.moving()&&runtime.controller.axes[0].target==90);
 fakeHeapFree=65536;fakeHeapBlock=32768;
 server.publish(15);pumpLive();server.publish(20);pumpLive();
 assert(command(1000,packet(2,"/api/target","&az=300"))==ESP_OK);
 server.commands(25);assert(runtime.controller.axes[0].target==90);
 assert(command(1000,std::string(513,'a'))==ESP_FAIL);
 // A blocked socket is closed; other clients still receive state/results.
 fakeServer.failedFd=1001;runtime.controller.axes[0].current=25;
 server.publish(250);pumpLive();
 assert(!fakeServer.closed.empty()&&fakeServer.closed.back()==1001);
 assert(fakeAbortiveCloseFd==1001 && fakeServer.websocketFds.count(1000));
 assert(connect(1001)==ESP_OK);fakeServer.failedFd=-1;
 assert(command(1001,packet(1,"/api/target","&az=110"))==ESP_OK);
 server.commands(255);assert(runtime.controller.axes[0].target==110);
 // A target submitted AFTER STOP is newer work and must remain executable.
 assert(command(1001,packet(2,"/api/move","&d=S"))==ESP_OK);
 assert(command(1001,packet(3,"/api/target","&az=115"))==ESP_OK);
 server.commands(256);assert(runtime.controller.axes[0].target==115);
 // A burst retains the latest absolute target with bounded cancellation metadata.
 for(unsigned i=10;i<210;++i) assert(command(1002,packet(i,"/api/target",("&az="+std::to_string(120+(i-10)*.1f)).c_str()))==ESP_OK);
 server.commands(260);assert(std::abs(runtime.controller.axes[0].target-139.9f)<.001f);
 server.publish(260);pumpLive();
 bool cancelledRange=false;for(const auto &message:fakeServer.sent)cancelledRange|=message.second.find("cancelledThrough")!=std::string::npos;
 assert(cancelledRange);assert(fakeServer.closed.back()!=1002);
 assert(command(1002,packet(220,"/api/move","&d=S"))==ESP_OK);
 server.commands(265);assert(!runtime.controller.moving());server.publish(265);pumpLive();
 for(unsigned t=300;t<=360;t+=5){server.publish(t);pumpLive();} // Drain earlier command replies.
 // Position samples coalesce at 5 Hz, while control/STOP remains immediate.
 runtime.controller.stopNow(-1,1000);server.publish(1000);pumpLive();fakeServer.sent.clear();
 for(unsigned t=1005;t<1200;t+=5){runtime.controller.feedback(float(t-1000),30,true,true);server.publish(t);pumpLive();}
 assert(fakeServer.sent.empty());
 server.publish(1200);pumpLive();assert(fakeServer.sent.size()==4);
 for(const auto &message:fakeServer.sent)assert(message.second.find("\"az\":195")!=std::string::npos);
 fakeServer.sent.clear();runtime.controller.setMode(Mode::Auto,1205);server.publish(1205);pumpLive();assert(fakeServer.sent.size()==4);
 fakeServer.sent.clear();runtime.controller.stopNow(-1,1210);server.publish(1210);pumpLive();assert(fakeServer.sent.size()==4);
 // Sustain 200 absolute commands/s while the 20 ms controller keeps running.
 auto &c=runtime.controller;c.hardware.drive=DRIVE_BTS7960;
 c.motionSettings[0].kickEnabled=true;c.motionSettings[0].movementDegrees=.02f;
 c.speeds[0].speedPercent=c.speeds[0].approachSpeedPercent=5;
 c.speeds[0].coastDegrees=.1f;
 c.feedbackAt(0,20,true,1480);server.publish(1480);pumpLive();
 unsigned kicks=0;MotionPhase phase=MotionPhase::Idle;float position=20;
 for(unsigned frame=0;frame<50;++frame) {
   const unsigned at=1500+frame*20;
   position+=c.axes[0].direction*c.axes[0].speedOutputPercent*.134f*.02f;
   c.feedbackAt(0,position,true,at);
   for(unsigned sub=0;sub<4;++sub) {
     const unsigned n=frame*4+sub+1;
     assert(command(1002,packet(300+n,"/api/target",("&az="+std::to_string(20+n*.1f)).c_str()))==ESP_OK);
   }
   server.commands(at);
   // HTTPD services replies between 20 ms control ticks, as in the firmware loop.
   for(unsigned sub=0;sub<4;++sub){server.publish(at+5*sub);pumpLive();}
   if(c.axes[0].motionPhase==MotionPhase::Kick&&phase!=MotionPhase::Kick)++kicks;
   phase=c.axes[0].motionPhase;
   assert(c.axes[0].motionError==MotionError::None);
 }
 assert(kicks==1&&std::abs(c.axes[0].target-40)<.001f);
 assert(c.axes[0].speedOutputPercent<=5.001f);
 assert(command(1002,packet(501,"/api/move","&d=S"))==ESP_OK);server.commands(2500);
 assert(!c.moving());server.publish(2500);pumpLive();
 assert(command(1002,packet(500,"/api/target","&az=40"))==ESP_OK);server.commands(2520);
 assert(!c.moving());server.publish(2520);pumpLive(); // Old delivery cannot rearm.
 assert(command(1002,packet(502,"/api/target","&az=40"))==ESP_OK);server.commands(2540);
 assert(c.axes[0].direction); // A new command may repeat the same coordinate.
 // Speed bursts have bounded independent slots and cannot crowd out STOP/targets.
 c.stopNow(-1,2560);server.publish(2560);pumpLive();
 c.hardware.simulation=true;
 for(unsigned id=600;id<800;++id) {
   const bool az=id%2==0;
   const std::string args=std::string("&axis=")+(az?"az":"el")+"&value="+std::to_string(20+(id-600)*.1f);
   assert(command(1002,packet(id,"/api/speed",args.c_str()))==ESP_OK);
 }
 assert(c.runtimeSpeed(0)!=39.8f);server.commands(2580);
 assert(std::abs(c.runtimeSpeed(0)-39.8f)<.001f&&std::abs(c.runtimeSpeed(1)-39.9f)<.001f&&!c.moving());
 server.publish(2580);pumpLive();server.publish(2585);pumpLive();
 // STOP cancels older speed updates; a newer speed changes only the setting.
 assert(command(1002,packet(800,"/api/speed","&axis=az&value=50"))==ESP_OK);
 assert(command(1002,packet(801,"/api/move","&d=S"))==ESP_OK);
 assert(command(1002,packet(802,"/api/speed","&axis=el&value=60"))==ESP_OK);
 server.commands(2600);assert(std::abs(c.runtimeSpeed(0)-39.8f)<.001f&&c.runtimeSpeed(1)==60&&!c.moving());
 server.publish(2600);pumpLive();server.publish(2605);pumpLive();
 // The final speed slot and an ordinary target can both execute on one tick.
 assert(command(1002,packet(803,"/api/speed","&axis=az&value=45"))==ESP_OK);
 assert(command(1002,packet(804,"/api/target","&az=80"))==ESP_OK);
 server.commands(2620);assert(c.runtimeSpeed(0)==45&&c.axes[0].target==80);
 server.publish(2620);pumpLive();server.publish(2625);pumpLive();
 assert(command(1002,"v=1&boot=b&selection=0&cid=805&path=/api/speed&axis=az&value=70")==ESP_OK);
 server.commands(2640);assert(c.runtimeSpeed(0)==45);server.publish(2640);pumpLive();
 assert(command(1002,packet(806,"/api/speed","&axis=az&value=70&rangeRevision=999"))==ESP_OK);
 server.commands(2660);assert(c.runtimeSpeed(0)==45);server.publish(2660);pumpLive();
 c.stopNow(-1,2680);
 assert(command(1003,packet(1,"/api/speed","&axis=az&value=55"))==ESP_OK);
 assert(command(1003,packet(2,"/api/speed","&axis=az&value=101"))==ESP_OK);
 server.commands(2700);assert(c.runtimeSpeed(0)==55&&!c.moving()); // Invalid update keeps the queued valid value.
 // A queued renewal cannot survive the matching keyboard release.
 for(unsigned t=2720;t<=2780;t+=5){server.publish(t);pumpLive();}
 auto hw=c.hardware;hw.drive=DRIVE_YAESU;hw.simulation=false;c.configureHardware(hw);c.feedback(90,90,true,true);
 const std::string identity="&client=123&session=456&gesture=1&mask=3";
 assert(command(1002,packet(900,"/api/move",("&d=H&az=1&el=-1&serial=1"+identity).c_str()))==ESP_OK);
 server.commands(2800);assert(c.axes[0].wanted==1&&c.axes[1].wanted==-1);server.publish(2800);pumpLive();
 assert(command(1002,packet(901,"/api/move",("&d=H&az=1&el=-1&serial=2"+identity).c_str()))==ESP_OK);
 assert(command(1002,packet(902,"/api/move",("&d=S&az=0&el=0&serial=3"+identity).c_str()))==ESP_OK);
 server.commands(2810);assert(!c.moving());server.publish(2810);pumpLive();
 assert(command(1002,packet(903,"/api/move",("&d=H&az=1&el=-1&serial=2"+identity).c_str()))==ESP_OK);
 server.commands(2820);assert(!c.moving());server.publish(2820);pumpLive();
 // A partly written state survives >100 ms of EAGAIN. Other clients,
 // STOP execution and PONG/result ordering keep working while it is blocked.
 for(int fd=1000;fd<1004;++fd)if(fakeServer.websocketFds.count(fd))httpd_sess_trigger_close(&fakeServer,fd);
 fakeServer.sent.clear();fakeServer.opcodes.clear();fakeServer.maxWrite=7;
 assert(connect(1000)==ESP_OK&&connect(1001)==ESP_OK);fakeEspTimeUs=10000000;
 server.publish(10000);pumpLive();assert(fakeServer.pendingWire[1000].size()==7);
 fakeServer.blockedFd=1000;fakeServer.maxWrite=SIZE_MAX;
 httpd_req_t ping{fakeServer.uri.user_ctx,HTTP_POST,1000,"probe",HTTPD_WS_TYPE_PING};assert(fakeServer.uri.handler(&ping)==ESP_OK);
 assert(command(1000,packet(1,"/api/target","&az=140"))==ESP_OK);
 assert(command(1000,packet(2,"/api/move","&d=S"))==ESP_OK);
 server.commands(10020);assert(!c.moving());
 for(unsigned t=10020;t<=11520;t+=20){fakeEspTimeUs=int64_t(t)*1000;c.axes[0].current=75;server.publish(t);pumpLive();}
 assert(fakeServer.websocketFds.count(1000)&&fakeServer.pendingWire[1000].size()==7);
 assert(fakeServer.sent.size()>=5); // healthy client receives telemetry
 fakeServer.blockedFd=-1;fakeServer.maxWrite=31;
 for(unsigned t=11525;t<=13025;t+=5){fakeEspTimeUs=int64_t(t)*1000;server.publish(t);pumpLive();}
 std::vector<std::string> recovered;std::vector<int> recoveredOpcodes;
 for(size_t i=0;i<fakeServer.sent.size();++i)if(fakeServer.sent[i].first==1000){recovered.push_back(fakeServer.sent[i].second);recoveredOpcodes.push_back(fakeServer.opcodes[i].second);}
 assert(recovered.size()>=5);
 assert(recovered[0].find("\"type\":\"state\"")!=std::string::npos);
 assert(recovered[1]=="probe"&&recoveredOpcodes[1]==HTTPD_WS_TYPE_PONG);
 assert(recovered[2].find("\"type\":\"result\"")!=std::string::npos&&recovered[3].find("\"type\":\"result\"")!=std::string::npos);
 assert(recovered[4].find("\"az\":75")!=std::string::npos); // latest state, no telemetry backlog
 // An incoming PONG is consumed, not treated as a malformed command.
 httpd_req_t pong{fakeServer.uri.user_ctx,HTTP_POST,1000,"peer",HTTPD_WS_TYPE_PONG};assert(fakeServer.uri.handler(&pong)==ESP_OK);
 // Only a permanently stalled client expires; the healthy peer remains live.
 httpd_sess_trigger_close(&fakeServer,1000);httpd_sess_trigger_close(&fakeServer,1001);assert(connect(1000)==ESP_OK&&connect(1001)==ESP_OK);
 fakeServer.maxWrite=SIZE_MAX;fakeServer.blockedFd=1000;fakeEspTimeUs=20000000;
 server.publish(20000);pumpLive();fakeEspTimeUs=24999000;server.publish(24999);pumpLive();assert(fakeServer.websocketFds.count(1000));
 fakeEspTimeUs=25000000;server.publish(25004);pumpLive();assert(!fakeServer.websocketFds.count(1000)&&fakeServer.websocketFds.count(1001));
 assert(fakeAbortiveCloseFd==1000);
 fakeServer.blockedFd=-1;
 // Peer CLOSE at a frame boundary can be echoed; after a partial write it
 // closes without injecting a control header into the unfinished payload.
 httpd_req_t closeReady{fakeServer.uri.user_ctx,HTTP_POST,1001,std::string("\x03\xe8",2),HTTPD_WS_TYPE_CLOSE};assert(fakeServer.uri.handler(&closeReady)==ESP_OK);
 assert(connect(1000)==ESP_OK);fakeServer.maxWrite=7;fakeEspTimeUs=26000000;server.publish(26000);pumpLive();
 const auto sentBeforeClose=fakeServer.sent.size();
 httpd_req_t closePartial{fakeServer.uri.user_ctx,HTTP_POST,1000,std::string("\x03\xe8",2),HTTPD_WS_TYPE_CLOSE};assert(fakeServer.uri.handler(&closePartial)==ESP_FAIL);
 assert(fakeServer.sent.size()==sentBeforeClose);
 fakeServer.maxWrite=SIZE_MAX;fakeEspTimeUs=-1;
 // The manual channel never puts full telemetry ahead of lease ACKs. Block
 // a partly sent state for 1.5s while renewals/STOP use the separate TCP stream.
 for(int fd=1000;fd<1004;++fd)if(fakeServer.websocketFds.count(fd))httpd_sess_trigger_close(&fakeServer,fd);
 fakeServer.sent.clear();fakeServer.pendingWire.clear();fakeServer.maxWrite=7;
 assert(connect(1000)==ESP_OK&&connect(1001)==ESP_OK);
 assert(command(1001,"v=1&control=1")==ESP_OK);
 fakeEspTimeUs=30000000;server.publish(30000);pumpLive();
 assert(fakeServer.pendingWire[1000].size()==7);
 fakeServer.blockedFd=1000;fakeServer.maxWrite=SIZE_MAX;
 fakeEspTimeUs=30005000;server.publish(30005);pumpLive();
 bool ready=false;for(const auto &m:fakeServer.sent)if(m.first==1001){
   assert(m.second.size()<=125);ready|=m.second.find("\"type\":\"control\"")!=std::string::npos;
 }assert(ready);fakeServer.sent.clear();
 const std::string manual="&client=789&session=345&gesture=1&mask=3";
 c.feedbackAt(0,90,true,30020);c.feedbackAt(1,90,true,30020);
 unsigned id=1;
 for(unsigned t=30020;t<=31520;t+=20){
   fakeEspTimeUs=int64_t(t)*1000;
   if((t-30020)%200==0){
     assert(command(1001,packet(id,"/api/move",("&d=H&az=1&el=0&serial="+std::to_string(id)+manual).c_str()))==ESP_OK);++id;
   }
   server.commands(t);c.tick(t);assert(c.axes[0].wanted==1);
   server.publish(t);pumpLive();
 }
 assert(fakeServer.pendingWire[1000].size()==7);unsigned acks=0;
 for(const auto &m:fakeServer.sent)if(m.first==1001){
   assert(m.second.size()<192&&m.second.find("\"type\":\"state\"")==std::string::npos);
   if(m.second.find("\"type\":\"result\"")!=std::string::npos){++acks;assert(m.second.find("\"ok\":true")!=std::string::npos);}
 }assert(acks==8);
 assert(command(1001,packet(id,"/api/move",("&d=S&az=0&el=0&serial="+std::to_string(id)+manual).c_str()))==ESP_OK);
 server.commands(31540);assert(!c.moving());fakeEspTimeUs=31540000;server.publish(31540);pumpLive();
 runtime.hud.selectionEpoch=7;fakeEspTimeUs=31560000;server.publish(31560);pumpLive();
 bool selection=false;for(const auto &m:fakeServer.sent)selection|=m.first==1001&&m.second.find("\"selection\":7")!=std::string::npos;
 assert(selection);runtime.hud.selectionEpoch=0;
 // Switching roles after an upgrade must finish any partial initial frame.
 httpd_sess_trigger_close(&fakeServer,1000);assert(connect(1000)==ESP_OK);
 fakeServer.sent.clear();fakeServer.maxWrite=7;fakeServer.blockedFd=-1;fakeEspTimeUs=32000000;
 server.publish(32000);pumpLive();assert(fakeServer.pendingWire[1000].size()==7);
 assert(command(1000,"v=1&control=1")==ESP_OK);fakeServer.maxWrite=SIZE_MAX;
 for(unsigned t=32005;t<=32020;t+=5){fakeEspTimeUs=int64_t(t)*1000;server.publish(t);pumpLive();}
 std::vector<std::string> switched;for(const auto &m:fakeServer.sent)if(m.first==1000)switched.push_back(m.second);
 assert(switched.size()==2&&switched[0].find("\"type\":\"state\"")!=std::string::npos&&switched[1].find("\"type\":\"control\"")!=std::string::npos);
 // TCP cannot accept the next complete frame until ACKs release space.
 // Hold one full peer while another receives updates; snapshots coalesce.
 for(int fd=1000;fd<1004;++fd)if(fakeServer.websocketFds.count(fd))httpd_sess_trigger_close(&fakeServer,fd);
 fakeServer.sent.clear();fakeServer.maxWrite=SIZE_MAX;
 assert(connect(1000)==ESP_OK&&connect(1001)==ESP_OK);
 auto publishAt=[&](unsigned t){fakeEspTimeUs=int64_t(t)*1000;server.publish(t);pumpLive();};
 publishAt(40000);fakeServer.sent.clear();fakeTcpPending.insert(1000);
 for(unsigned t=40200;t<=41400;t+=200){c.axes[0].current=float(t-40000)/100;publishAt(t);}
 unsigned healthy=0;for(const auto &m:fakeServer.sent){assert(m.first!=1000);if(m.first==1001)++healthy;}assert(healthy==7);
 // Even on the telemetry socket, PONG and STOP results bypass ACK pacing.
 httpd_req_t ackPing{fakeServer.uri.user_ctx,HTTP_POST,1000,"ack-probe",HTTPD_WS_TYPE_PING};assert(fakeServer.uri.handler(&ackPing)==ESP_OK);
 assert(command(1000,packet(1,"/api/move","&d=S"))==ESP_OK);
 server.commands(41405);assert(!c.moving());publishAt(41405);publishAt(41410);
 bool ackPong=false,ackStop=false;
 for(const auto &m:fakeServer.sent)if(m.first==1000){
   ackPong|=m.second=="ack-probe";ackStop|=m.second.find("\"type\":\"result\"")!=std::string::npos;
   assert(m.second.find("\"type\":\"state\"")==std::string::npos);
 }assert(ackPong&&ackStop);
 // Empty RFC control frames must not read another header. A browser's text
 // probe queues a coalesced PONG without executing or acknowledging commands.
 httpd_req_t emptyPing{fakeServer.uri.user_ctx,HTTP_POST,1000,"",HTTPD_WS_TYPE_PING};
 assert(fakeServer.uri.handler(&emptyPing)==ESP_OK&&emptyPing.receiveCalls==1);publishAt(41415);
 assert(fakeServer.sent.back().first==1000&&fakeServer.sent.back().second.empty()&&fakeServer.opcodes.back().second==HTTPD_WS_TYPE_PONG);
 httpd_req_t emptyPong{fakeServer.uri.user_ctx,HTTP_POST,1000,"",HTTPD_WS_TYPE_PONG};
 assert(fakeServer.uri.handler(&emptyPong)==ESP_OK&&emptyPong.receiveCalls==1);
 const auto targetBeforeProbe=c.axes[0].target;
 assert(command(1000,"v=1&ping=1")==ESP_OK&&command(1000,"v=1&ping=1")==ESP_OK);publishAt(41420);
 assert(fakeServer.sent.back().first==1000&&fakeServer.sent.back().second.empty()&&fakeServer.opcodes.back().second==HTTPD_WS_TYPE_PONG);
 assert(c.axes[0].target==targetBeforeProbe&&!c.moving());
 fakeServer.sent.clear();fakeTcpPending.clear();c.axes[0].current=42;publishAt(41650);
 unsigned latest=0;for(const auto &m:fakeServer.sent)if(m.first==1000){++latest;assert(m.second.find("\"az\":42")!=std::string::npos);}assert(latest==1);
 // ACK pacing applies only between frames. A partial frame must finish even
 // if TCP retains the first fragment, preserving the WebSocket boundary.
 httpd_sess_trigger_close(&fakeServer,1000);assert(connect(1000)==ESP_OK);
 fakeServer.maxWrite=7;publishAt(42000);assert(fakeServer.pendingWire[1000].size()==7);
 fakeTcpPending.insert(1000);fakeServer.maxWrite=SIZE_MAX;fakeServer.sent.clear();publishAt(42005);
 bool completed=false;for(const auto &m:fakeServer.sent)completed|=m.first==1000&&m.second.find("\"type\":\"state\"")!=std::string::npos;assert(completed);
 // A permanently unacknowledged peer expires independently. STOP received
 // at its deadline still executes before that connection is discarded.
 c.axes[0].target+=1;publishAt(42200);publishAt(47180);assert(fakeServer.websocketFds.count(1000));
 assert(command(1000,packet(1,"/api/move","&d=S"))==ESP_OK);
 publishAt(47200);assert(fakeServer.websocketFds.count(1000));
 const auto beforePendingStop=fakeServer.sent.size();publishAt(47205);
 assert(fakeServer.websocketFds.count(1000));
 for(size_t i=beforePendingStop;i<fakeServer.sent.size();++i)assert(fakeServer.sent[i].first!=1000);
 server.commands(47206);assert(!c.moving());publishAt(47210);
 assert(!fakeServer.websocketFds.count(1000)&&fakeServer.websocketFds.count(1001));
 char ackText[2048];JsonWriter ackDiagnostic(ackText,sizeof ackText);server.writeDiagnostics(ackDiagnostic);
 assert(std::string(ackDiagnostic.data(),ackDiagnostic.size()).find("\"liveAckTimeouts\":1")!=std::string::npos);
 fakeTcpPending.clear();assert(connect(1000)==ESP_OK);fakeServer.sent.clear();publishAt(47215);
 bool fresh=false;for(const auto &m:fakeServer.sent)fresh|=m.first==1000&&m.second.find("\"type\":\"state\"")!=std::string::npos;assert(fresh);
 // A loop publication can grow the frame while HTTPD snapshots TCP outside
 // the application mutex. Admission must use the current frame's size.
 for(int fd=1000;fd<1004;++fd)if(fakeServer.websocketFds.count(fd))httpd_sess_trigger_close(&fakeServer,fd);
 assert(connect(1000)==ESP_OK);publishAt(50000);
 const auto oldFrameBytes=fakeServer.sent.back().second.size()+4;
 httpd_sess_trigger_close(&fakeServer,1000);assert(connect(1000)==ESP_OK);fakeServer.sent.clear();
 fakeTcpCapacity=oldFrameBytes;
 fakeTcpCapacityHook=[&]{c.fault="longer fault text published while TCP capacity is being read";fakeEspTimeUs=50200000;server.publish(50200);};
 publishAt(50005);assert(fakeServer.sent.empty());
 fakeTcpCapacity=size_t(-1);publishAt(50220);
 assert(fakeServer.sent.size()==1&&fakeServer.sent[0].second.find("longer fault text")!=std::string::npos);
 c.fault="";
 // Four peers can pin different publications. A fast peer keeps receiving
 // new state while the other three retain their exact older frame bytes.
 for(int fd=1000;fd<1004;++fd)if(fakeServer.websocketFds.count(fd))httpd_sess_trigger_close(&fakeServer,fd);
 const auto idleBytes=bufferMetric("liveBufferBytes");assert(bufferMetric("liveStateBuffers")==0);
 for(int fd=1000;fd<1004;++fd)assert(connect(fd)==ESP_OK);
 fakeServer.sent.clear();
 for(unsigned i=0;i<4;++i){
   fakeServer.blockedFds.insert(1000+int(i));c.axes[0].current=30+float(i);publishAt(60000+i*200);
   assert(bufferMetric("liveStateBuffers")==i+1);
 }
 c.axes[0].current=44;publishAt(60800);assert(bufferMetric("liveStateBuffers")==4);
 fakeServer.blockedFds.erase(1003);publishAt(60805);publishAt(61000);
 bool newest=false;for(const auto &message:fakeServer.sent)newest|=message.first==1003&&message.second.find("\"az\":44")!=std::string::npos;
 assert(newest&&bufferMetric("liveStateBuffers")==4);
 fakeServer.blockedFds.clear();publishAt(61005);publishAt(62010);
 assert(bufferMetric("liveStateBuffers")==1); // transient older frames are released
 for(int fd=1000;fd<1004;++fd)httpd_sess_trigger_close(&fakeServer,fd);
 assert(bufferMetric("liveStateBuffers")==0&&bufferMetric("liveBufferBytes")==idleBytes);
 // HUD manual input is a renewable stream, not a job with a completion record.
 // Its deferred admission must not be followed by a synthetic unavailable reply.
 runtime.hud.config.role=DeviceRole::Hud;runtime.hud.begin(1,10);
 assert(runtime.hud.setSimulationEnabled(true)&&runtime.hud.select(HUD_SIMULATION_ID));
 runtime.hud.service(64000);c.tick(64000);
 assert(connect(1000)==ESP_OK&&command(1000,"v=1&control=1")==ESP_OK);
 publishAt(64000);
 unsigned manualSerial=0;
 const auto hudManual=[&](const char *direction,unsigned now) {
   const auto sequence=++manualSerial;
   const auto request="v=1&boot=a&selection="+std::to_string(runtime.hud.selectionEpoch)+
       "&cid="+std::to_string(sequence)+"&path=/api/move&d="+direction+
       "&client=789&session=345&gesture=5&serial="+std::to_string(sequence)+
       "&mask=3&az="+(std::string(direction)=="H"?"1":"0")+"&el=0";
   fakeServer.sent.clear();assert(command(1000,request)==ESP_OK);
   server.commands(now);runtime.hud.service(now);c.tick(now);publishAt(now);
   unsigned replies=0;
   for(const auto &message:fakeServer.sent)if(message.second.find("\"type\":\"result\"")!=std::string::npos) {
     ++replies;assert(message.second.find("\"result\":\"deferred\"")!=std::string::npos);
   }
   assert(replies==1);
 };
 for(unsigned now=64200;now<=66200;now+=200) {hudManual("H",now);assert(c.moving());}
 hudManual("S",66400);assert(!c.moving());
 // Ordinary HUD jobs still wait for, and deliver, their execution result.
 fakeServer.sent.clear();
 assert(command(1000,"v=1&boot=a&selection="+std::to_string(runtime.hud.selectionEpoch)+
     "&cid="+std::to_string(++manualSerial)+"&path=/api/mode&mode=auto")==ESP_OK);
 server.commands(66420);publishAt(66420);
 unsigned modeReplies=0;
 for(const auto &message:fakeServer.sent)if(message.second.find("\"type\":\"result\"")!=std::string::npos) {
   ++modeReplies;assert(message.second.find("\"result\":\"deferred\"")!=std::string::npos);
 }
 assert(modeReplies==1);
 runtime.hud.service(66420);c.tick(66420);server.commands(66440);publishAt(66440);
 modeReplies=0;
 for(const auto &message:fakeServer.sent)if(message.second.find("\"type\":\"result\"")!=std::string::npos) {
   ++modeReplies;if(modeReplies==2)assert(message.second.find("\"ok\":true")!=std::string::npos);
 }
 assert(modeReplies==2);
 httpd_sess_trigger_close(&fakeServer,1000);
 // Local RAM throttling may last longer than a peer timeout. Keep the healthy
 // telemetry socket and its independent compact control heartbeat alive.
 fakeHeapFree=65536;fakeHeapBlock=32768;fakeServer.sent.clear();fakeEspTimeUs=70000000;
 assert(connect(1000)==ESP_OK&&connect(1001)==ESP_OK);
 assert(command(1001,"v=1&control=1")==ESP_OK);
 networkMemoryLimited=true;runtime.hud.setMemoryLimited(true);fakeServer.maxWrite=7;
 publishAt(70000);fakeServer.maxWrite=SIZE_MAX;fakeHeapFree=20000;fakeHeapBlock=8192;
 for(unsigned t=70005;t<=80000;t+=5)publishAt(t);
 assert(fakeServer.websocketFds.count(1000)&&fakeServer.websocketFds.count(1001));
 unsigned heartbeats=0;for(const auto &m:fakeServer.sent)if(m.first==1001&&m.second.find("\"type\":\"control\"")!=std::string::npos){++heartbeats;assert(m.second.find("\"memoryLimited\":true")!=std::string::npos);}
 assert(heartbeats>=19);
 // A genuinely full TCP queue still expires while local RAM is constrained.
 fakeTcpPending.insert(1000);
 for(unsigned t=80005;t<=85010;t+=5)publishAt(t);
 assert(!fakeServer.websocketFds.count(1000)&&fakeServer.websocketFds.count(1001));fakeTcpPending.clear();
 fakeHeapFree=65536;fakeHeapBlock=32768;assert(connect(1000)==ESP_OK);fakeServer.sent.clear();
 publishAt(85020);fakeServer.sent.clear(); // initial cached snapshot is immediate
 for(unsigned t=85025;t<87020;t+=5){c.axes[0].current=float(t)/1000;publishAt(t);}
 unsigned slowStates=0;for(const auto &m:fakeServer.sent)slowStates+=m.first==1000&&m.second.find("\"type\":\"state\"")!=std::string::npos;
 assert(slowStates>=3&&slowStates<=4);
 networkMemoryLimited=false;runtime.hud.setMemoryLimited(false);
 httpd_sess_trigger_close(&fakeServer,1000);httpd_sess_trigger_close(&fakeServer,1001);
 fakeEspTimeUs=-1;
 std::cout<<"PASS: HUD manual renewals and release receive one deferred admission without false completion errors\n";
 std::cout<<"PASS: shared immutable state, four pinned revisions, independent fast peer, bounded pool, idle trim and last-client release\n";
 std::cout<<"PASS: TCP ACK pacing coalesces snapshots, preserves peers/STOP/PONG and partial frames, expires stalled peers and resets on reconnect\n";
 std::cout<<"PASS: separate compact manual stream, lease renewal/STOP ACKs across 1.5s telemetry backpressure, context changes and partial upgrade\n";
 std::cout<<"PASS: partial frames resume after 1.5s, independent STOP/peers, PONG and ACK ordering, latest state and bounded stalled-client timeout\n";
 std::cout<<"PASS: actual HTTPD adapter: four clients, loop ownership, STOP priority, duplicate IDs, bounded backpressure, reconnect\n";
 std::cout<<"PASS: idle live storage <=512 B, 10000 unupgraded sockets without queue allocation, handshake expiry, low-memory admission and existing STOP\n";
 std::cout<<"PASS: 10s local memory pause preserves healthy peers/control heartbeats; stalled TCP expires; telemetry resumes at 2Hz\n";
}
