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
#include <unistd.h>
#include <esp_heap_caps.h>
static size_t allocationCalls=0, releasedCalls=0, allocatedBytes=0;
void *operator new(size_t size) {
  if(void *p=std::malloc(size?size:1)){++allocationCalls;allocatedBytes+=size;return p;}
  throw std::bad_alloc();
}
void *operator new[](size_t size) {return ::operator new(size);}
void *operator new(size_t size,const std::nothrow_t &) noexcept {
  try {return ::operator new(size);} catch(...) {return nullptr;}
}
void *operator new[](size_t size,const std::nothrow_t &tag) noexcept {return ::operator new(size,tag);}
void operator delete(void *p) noexcept {if(p)++releasedCalls;std::free(p);}
void operator delete[](void *p) noexcept {::operator delete(p);}
void operator delete(void *p,size_t) noexcept {::operator delete(p);}
void operator delete[](void *p,size_t) noexcept {::operator delete(p);}
void operator delete(void *p,const std::nothrow_t &) noexcept {::operator delete(p);}
void operator delete[](void *p,const std::nothrow_t &) noexcept {::operator delete(p);}
using namespace forkrot;
// IDF 5.5.5 performs the handshake without invoking the URI handler.
static int connect(int fd){const int rc=fakeServer.cfg.open_fn(&fakeServer,fd);if(rc==ESP_OK)fakeServer.websocketFds.insert(fd);return rc;}
static int command(int fd,const std::string &data){httpd_req_t r{fakeServer.uri.user_ctx,HTTP_POST,fd,data};return fakeServer.uri.handler(&r);}
static std::string packet(unsigned id,const char *path,const char *args=""){return "v=1&boot=a&selection=0&cid="+std::to_string(id)+"&path="+path+args;}
int main(){
 MotionTestRuntime runtime;runtime.hud.begin(1,10);runtime.controller.feedback(20,30,true,true);
 LiveServer server(runtime);const auto beforeBegin=allocatedBytes;
 assert(server.begin());assert(fakeServer.cfg.max_open_sockets==4);
 assert(allocatedBytes-beforeBegin<=4096); // idle server holds no per-client queues
 const auto beforeLowHeap=allocationCalls;
 fakeHeapFree=MemoryGuard::CriticalFree;
 assert(connect(999)==ESP_FAIL && allocationCalls==beforeLowHeap);
 fakeHeapFree=65536;fakeHeapBlock=MemoryGuard::CriticalBlock;
 assert(connect(999)==ESP_FAIL && allocationCalls==beforeLowHeap);
 fakeHeapBlock=32768;
 for(unsigned i=0;i<10000;++i) {
   const auto allocations=allocationCalls, releases=releasedCalls, bytes=allocatedBytes;
   assert(fakeServer.cfg.open_fn(&fakeServer,999)==ESP_OK);
   assert(allocationCalls==allocations+1 && allocatedBytes-bytes<=2048);
   fakeServer.cfg.close_fn(&fakeServer,999);
   assert(releasedCalls==releases+1);
 }
 assert(fakeServer.cfg.open_fn(&fakeServer,999)==ESP_OK);
 server.publish(5);pumpLive();assert(fakeServer.sent.empty()); // no frame before upgrade
 fakeServer.websocketFds.insert(999);server.publish(10);pumpLive();
 assert(fakeServer.sent.size()==1&&fakeServer.sent.back().second.find("\"type\":\"state\"")!=std::string::npos);
 httpd_req_t legacy{fakeServer.uri.user_ctx,HTTP_GET,999,{}};assert(fakeServer.uri.handler(&legacy)==ESP_OK);
 httpd_sess_trigger_close(&fakeServer,999);fakeServer.sent.clear();
 for(int fd=1000;fd<1004;++fd)assert(connect(fd)==ESP_OK);
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
 assert(connect(1004)==ESP_FAIL);
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
 std::cout<<"PASS: actual HTTPD adapter: four clients, loop ownership, STOP priority, duplicate IDs, bounded backpressure, reconnect\n";
 std::cout<<"PASS: idle live storage <=4 KiB, 10000 queue allocations/releases, low-memory admission and existing STOP\n";
}
