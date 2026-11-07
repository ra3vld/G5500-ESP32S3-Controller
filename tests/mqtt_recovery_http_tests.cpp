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
  std::cout<<"PASS: recovery MQTT GET/disable through production bounded HTTP handler and API, no stock parser, preserved credentials, no reboot or unlock\n";
}
