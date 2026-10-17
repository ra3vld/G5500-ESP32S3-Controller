#include "live_server.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
// IDF 5.5.5 performs the handshake without invoking the URI handler.
static int connect(int fd){const int rc=fakeServer.cfg.open_fn(&fakeServer,fd);if(rc==ESP_OK)fakeServer.websocketFds.insert(fd);return rc;}
static int command(int fd,const std::string &data){httpd_req_t r{fakeServer.uri.user_ctx,HTTP_POST,fd,data};return fakeServer.uri.handler(&r);}
static std::string packet(unsigned id,const char *path,const char *args=""){return "v=1&boot=a&selection=0&cid="+std::to_string(id)+"&path="+path+args;}
int main(){
 Runtime runtime;runtime.hud.begin(1,10);runtime.controller.feedback(20,30,true,true);
 LiveServer server(runtime);assert(server.begin());assert(fakeServer.cfg.max_open_sockets==4);
 assert(fakeServer.cfg.open_fn(&fakeServer,999)==ESP_OK);
 server.publish(5);pumpLive();assert(fakeServer.sent.empty()); // no frame before upgrade
 fakeServer.websocketFds.insert(999);server.publish(10);pumpLive();
 assert(fakeServer.sent.size()==1&&fakeServer.sent.back().second.find("\"type\":\"state\"")!=std::string::npos);
 httpd_req_t legacy{fakeServer.uri.user_ctx,HTTP_GET,999,{}};assert(fakeServer.uri.handler(&legacy)==ESP_OK);
 httpd_sess_trigger_close(&fakeServer,999);fakeServer.sent.clear();
 for(int fd=1000;fd<1004;++fd)assert(connect(fd)==ESP_OK);
 assert(connect(1004)==ESP_FAIL);
 assert(command(1000,packet(1,"/api/target","&az=90"))==ESP_OK);
 assert(runtime.controller.axes[0].target!=90); // HTTPD cannot execute a control command.
 server.commands(5);assert(runtime.controller.axes[0].target==90);
 server.publish(5);pumpLive();server.publish(10);pumpLive();
 assert(!fakeServer.sent.empty());
 assert(command(1000,packet(2,"/api/target","&az=200"))==ESP_OK);
 assert(command(1000,packet(3,"/api/move","&d=S"))==ESP_OK);
 server.commands(15);assert(!runtime.controller.moving()&&runtime.controller.axes[0].target==90);
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
 // Queue saturation is explicit; never grow buffers or silently lose results.
 for(unsigned i=10;i<25;++i)command(1002,packet(i,"/api/target","&az=120"));
 assert(command(1002,packet(30,"/api/move","&d=S"))==ESP_OK);
 server.commands(260);assert(!runtime.controller.moving());server.publish(260);pumpLive();
 assert(fakeServer.closed.back()==1002);
 for(unsigned t=300;t<=360;t+=5){server.publish(t);pumpLive();} // Drain earlier command replies.
 // Position samples coalesce at 5 Hz, while control/STOP remains immediate.
 runtime.controller.stopNow(-1,1000);server.publish(1000);pumpLive();fakeServer.sent.clear();
 for(unsigned t=1005;t<1200;t+=5){runtime.controller.feedback(float(t-1000),30,true,true);server.publish(t);pumpLive();}
 assert(fakeServer.sent.empty());
 server.publish(1200);pumpLive();assert(fakeServer.sent.size()==3);
 for(const auto &message:fakeServer.sent)assert(message.second.find("\"az\":195")!=std::string::npos);
 fakeServer.sent.clear();runtime.controller.setMode(Mode::Auto,1205);server.publish(1205);pumpLive();assert(fakeServer.sent.size()==3);
 fakeServer.sent.clear();runtime.controller.stopNow(-1,1210);server.publish(1210);pumpLive();assert(fakeServer.sent.size()==3);
 std::cout<<"PASS: actual HTTPD adapter: four clients, loop ownership, STOP priority, duplicate IDs, bounded backpressure, reconnect\n";
}
