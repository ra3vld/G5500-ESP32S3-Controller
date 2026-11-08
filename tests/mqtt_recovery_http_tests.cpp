#include "device_service_fixture.hpp"
#include "bounded_web_server.hpp"
#undef send
#undef recv
#undef setsockopt
#define ARDUINO_ARCH_ESP32 1
#include "live_server.hpp"
#undef ARDUINO_ARCH_ESP32
#include "http_io_task.hpp"
#include "camera.hpp"
#define private public
#include "web_server.hpp"
#undef private
#include "../G5500_Fork/web_hardware.cpp"
#include "../G5500_Fork/web_control.cpp"
struct HttpActions : Actions {
  DeviceApi &api;
  explicit HttpActions(Fixture &f):Actions(f.runtime,f.calibration,f.storage),api(f.api){}
  void deviceRequest(std::string_view method,std::string_view path,FormView form,JsonWriter &out,int &code,const char *&type,bool command) override {
    api.execute(method,path,form,out,code,type,command);
  }
};
int main(){
  Fixture f;HttpActions actions(f);
  assert(f.call("POST","/api/mqtt","enabled=1&tls=1&host=broker.example&port=8883&password=retained-secret").status==200);
  f.runtime.recovery=true;f.runtime.controller.configurationLocked=true;
  WebInterface page(f.runtime,actions,f.calibration,f.hardware,f.diagnostics,f.network,f.storage,f.ota,f.restart);
  page.web_.setRequestHandler([](void *context,std::string_view request){auto &self=*static_cast<WebInterface *>(context);return self.handleMqttRequest(request)||self.handleHardwareRequest(request);},&page);
  const auto call=[&](const std::string &method,const std::string &body,int status=200){
    testSocket={};testSocket.pending=true;
    testSocket.input=method+" /api/mqtt HTTP/1.1\r\nContent-Length: "+std::to_string(body.size())+"\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n"+body;
    for(unsigned i=0;i<100;++i){page.web_.handleClient();fakeHardware.now+=5;}
    assert(testSocket.parsed==0);assert(testSocket.output.find(std::to_string(status)+" ")!=std::string::npos);
    assert(testSocket.output.find("retained-secret")==std::string::npos);
    return testSocket.output;
  };
  fakeHeapFree=32768;fakeHeapBlock=16384;
  assert(call("GET","").find("\"tls\":true")!=std::string::npos);
  fakeHeapBlock=8192;
  assert(call("GET","").find("\"tls\":true")!=std::string::npos); // No decoder allocation for GET.
  assert(call("POST","enabled=0&tls=0",503).find("mqtt_memory_limited")!=std::string::npos);
  assert(f.runtime.mqtt.enabled && f.runtime.mqtt.tls); // Refused before persistence/configuration.
  fakeHeapBlock=16384;
  assert(call("POST","enabled=0&tls=0").find("\"restart\":false")!=std::string::npos);
  MqttConfig saved;assert(f.storage.loadMqtt(saved)&&!saved.enabled&&!saved.tls&&std::string(saved.password)=="retained-secret");
  assert(f.runtime.recovery&&f.runtime.controller.configurationLocked&&!f.restart.pending());
  f.runtime.recovery=false;f.runtime.controller.configurationLocked=false;
  assert(f.call("POST","/api/mode","mode=auto",false).status==200);
  page.web_.setPriorityHandler([](void *context,std::string_view request,char *reply,size_t capacity,size_t &size,int &status,bool &stopsAll) {
    return static_cast<WebInterface *>(context)->handlePriorityRequest(request,reply,capacity,size,status,stopsAll);
  },&page);
  static const char large[4096]={'x'};
  page.web_.setRequestHandler([](void *p,std::string_view){static_cast<BoundedWebServer *>(p)->sendFixed(200,"text/html",large,sizeof large);return true;},&page.web_);
  testSocket={};testSocket.pending=true;testSocket.input="GET / HTTP/1.1\r\n\r\n";
  fakeHeapFree=50000;fakeHeapBlock=24000;page.web_.handleClient();testSocket.blockWrites=true;
  auto &urgent=extraSockets[0];urgent={};urgent.pending=true;
  urgent.input="GET /api/target?az=12%30&el=6%30 HTTP/1.1\r\n\r\n";
  page.web_.handleClient();assert(!urgent.open && testSocket.open);
  assert(urgent.output.find("200 OK")!=std::string::npos);
  assert(f.runtime.controller.axes[0].target==120 && f.runtime.controller.axes[1].target==60);
  // The real POST decoder must work when lwIP exposes the header and body in
  // separate receive buffers, while the main page remains blocked.
  urgent={};urgent.pending=true;
  const std::string stopHeader="POST /api/move HTTP/1.1\r\nContent-Length: 5\r\n\r\n";
  urgent.input=stopHeader+"d=%53";urgent.maxRead=stopHeader.size();
  page.web_.handleClient();assert(urgent.open && urgent.output.empty());
  page.web_.handleClient();assert(!urgent.open && !f.runtime.controller.moving());
  assert(urgent.output.find("200 OK")!=std::string::npos && testSocket.open);
  const auto stoppedEpoch=f.runtime.controller.stopEpoch;
  for(unsigned i=0;i<10;++i)page.web_.handleClient();
  assert(f.runtime.controller.stopEpoch==stoppedEpoch);
  // A short mode command is independent of the blocked main response. A
  // repeated value avoids NVS; a changed mode and a failed-save retry persist.
  auto mode=[&](const std::string &value) {
    urgent={};urgent.pending=true;
    const auto body="mode="+value;
    const auto header="POST /api/mode HTTP/1.1\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n";
    urgent.input=header+body;urgent.maxRead=header.size();
    for(unsigned i=0;i<5;++i)page.web_.handleClient();
    assert(!urgent.open && urgent.output.find("200 OK")!=std::string::npos && testSocket.open);
  };
  const auto saves=f.actions.settingsSaves;mode("auto");assert(f.actions.settingsSaves==saves);
  mode("manual");assert(f.actions.settingsSaves==saves+1 && f.runtime.controller.mode==Mode::Manual);
  f.runtime.controller.fault="motion_storage_failed";mode("manual");assert(f.actions.settingsSaves==saves+2);
  f.runtime.controller.fault.clear();mode("auto");assert(f.actions.settingsSaves==saves+3);
  // A mode change accepted before STOP cannot start a pending target later.
  {
    auto &old=extraSockets[1];old={};old.pending=true;
    old.input="POST /api/mode HTTP/1.1\r\nContent-Length: 11\r\n\r\nmode=";
    page.web_.handleClient();
    urgent={};urgent.pending=true;urgent.input="GET /api/move?d=S HTTP/1.1\r\n\r\n";page.web_.handleClient();
    old.input+="manual";page.web_.handleClient();
    assert(old.output.find("409")!=std::string::npos && f.runtime.controller.mode==Mode::Auto);
  }
  urgent={};urgent.pending=true;urgent.blockWrites=true;
  urgent.input="GET /api/move?d=S HTTP/1.1\r\n\r\n";
  page.web_.handleClient();assert(!f.runtime.controller.moving() && testSocket.open && urgent.open);
  urgent.blockWrites=false;page.web_.handleClient();assert(!urgent.open);
  // The real decoder rejects duplicate arguments without executing a target.
  urgent={};urgent.pending=true;
  urgent.input="POST /api/target?az=150 HTTP/1.1\r\nContent-Length: 6\r\n\r\naz=200";
  page.web_.handleClient();assert(!urgent.open && urgent.output.find("400 Error")!=std::string::npos);
  assert(!f.runtime.controller.moving() && f.runtime.controller.axes[0].target!=200);
  // A late completion of an accepted target must not undo a later STOP,
  // including a POST with URL-encoded arguments from the real browser.
  for(const auto &stopRequest:{std::string("GET /api/move?d=S HTTP/1.1\r\n\r\n"),
      std::string("POST /api/move HTTP/1.1\r\nContent-Length: 5\r\n\r\nd=%53")}) {
    auto &old=extraSockets[1];old={};old.pending=true;
    old.input="GET /api/target?az=150&el=60 HTTP/1.1\r\n";
    page.web_.handleClient();assert(old.open);
    urgent={};urgent.pending=true;urgent.input=stopRequest;
    page.web_.handleClient();assert(!urgent.open && urgent.output.find("200 OK")!=std::string::npos);
    old.input+="\r\n";page.web_.handleClient();
    assert(!old.open && old.output.find("cancelled")!=std::string::npos);
    assert(f.runtime.controller.axes[0].target!=150 && !f.runtime.controller.moving());
  }
  testSocket.blockWrites=false;
  while(testSocket.open){fakeHardware.now+=5;page.web_.handleClient();}
  std::cout<<"PASS: production priority HTTP -> DeviceApi -> Controller target/STOP during slow page, percent decoding and duplicate rejection\n";
  std::cout<<"PASS: recovery MQTT GET/disable through production bounded HTTP handler and API, no stock parser, preserved credentials, no reboot or unlock\n";
}
