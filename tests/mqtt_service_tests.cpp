// Exercise the production ESP-MQTT adapter with a deterministic SDK boundary.
// The SDK/RTOS fakes do not perform a cryptographic handshake.
#include "device_service_fixture.hpp"
#include "ethernet.hpp"
#define ARDUINO_ARCH_ESP32 1
#define ARDUINO 1
#include "../G5500_Fork/mqtt_service.cpp"
static std::time_t fakeMqttTime=1800000000;
extern "C" std::time_t time(std::time_t *value) noexcept {
  if(value)*value=fakeMqttTime;
  return fakeMqttTime;
}

static std::string status(MqttService &service){char bytes[4096];JsonWriter j(bytes,sizeof bytes);service.writeStatus(j);assert(j.good());return {bytes,j.size()};}
static void expect(MqttService &service,const char *text){assert(status(service).find(text)!=std::string::npos);}
int main(){
  Fixture f;
  auto &r=f.runtime;r.hud.begin(1,0x1234);r.controller.remote=nullptr;
  MqttService service(r,f.actions,f.hardware);
  fakeHeapFree=180000;fakeHeapBlock=100000;WiFi.connectionStatus=WL_CONNECTED;
  service.service();assert(!fakeMqttInit);expect(service,"\"clientAllocated\":false");
  MqttConfig config;config.enabled=true;config.tls=true;config.port=8883;std::strcpy(config.host,"broker.example");
  service.configure(config);
  {
    DeviceApi api{r,f.actions,f.calibration,f.hardware,f.diagnostics,f.network,f.storage,f.ota,f.restart,&service};
    char bytes[4096];JsonWriter j(bytes,sizeof bytes);int code=200;const char *type=nullptr;
    api.execute("GET","/api/mqtt",{},j,code,type,false);
    const std::string json(bytes,j.size());const auto at=json.find("\"tls\":");
    assert(code==200&&j.good()&&at!=std::string::npos&&json.find("\"tls\":",at+1)==std::string::npos);
  }
  r.controller.axes[0].direction=1;assert(r.controller.controlActive());service.service();assert(!fakeMqttClient);
  r.controller.axes[0].direction=0;
#if MQTT_TLS_SUPPORT
  fakeMqttTime=0;service.service();expect(service,"mqtt_tls_time_required");assert(!fakeMqttInit);
  service.service();assert(fakeMqttNtpRequests==1); // Late Wi-Fi, no repeated NTP setup.
  fakeMqttTime=1800000000;service.service();
  assert(fakeMqttClient&&fakeMqttInit==1&&fakeMqttScheme=="mqtts");
  const auto &s=fakeMqttClient->settings;
  assert(s.broker.address.transport==MQTT_TRANSPORT_OVER_SSL);
  assert(s.broker.verification.crt_bundle_attach==esp_crt_bundle_attach);
  assert(!s.broker.verification.skip_cert_common_name_check&&!s.broker.verification.certificate);
  assert(s.network.disable_auto_reconnect&&s.buffer.size==1024&&s.buffer.out_size==1024&&s.task.stack_size==6144);
  assert(s.network.reconnect_timeout_ms==100); // Bound the SDK's idle stop wait, not the guarded retry interval.
  fakeMqttEvent(MQTT_EVENT_CONNECTED);assert(service.connected());
  // Failed verification releases the entire client; every retry is guarded.
  FakeMqttError error;error.esp_tls_stack_err=-9984;error.esp_tls_cert_verify_flags=8;error.connect_return_code=5;
  fakeHardware.now=UINT32_MAX-5000;
  FakeMqttEvent event;event.error_handle=&error;fakeMqttEvent(MQTT_EVENT_ERROR,event);
  service.service();assert(!fakeMqttClient&&fakeMqttDestroy==1&&!service.connected());
  expect(service,"\"tlsError\":-9984");expect(service,"\"tlsVerifyFlags\":8");
  expect(service,"\"brokerError\":5"); // Refusal survives destruction of the SDK client.
  service.service();assert(fakeMqttInit==1);
  fakeHardware.now+=10000;fakeHeapFree=90000;service.service();assert(fakeMqttInit==1);expect(service,"mqtt_tls_memory_limited");
  fakeHeapFree=180000;fakeHeapBlock=16000;service.service();assert(fakeMqttInit==1);
  fakeHeapBlock=100000;service.service();assert(fakeMqttInit==2);
  // Runtime disable joins publisher before destroying SDK and frees all state.
  fakeMqttEvent(MQTT_EVENT_CONNECTED);
  config.enabled=false;service.configure(config);assert(!fakeMqttClient&&fakeMqttDestroy==2&&!r.mqtt.enabled);
  expect(service,"\"brokerError\":0");
  assert(fakeMqttOffline==1);
  service.service();assert(fakeMqttInit==2);expect(service,"\"clientAllocated\":false");
  config.enabled=true;service.configure(config);service.service();assert(fakeMqttClient);
  r.recovery=true;service.service();assert(!fakeMqttClient);expect(service,"\"error\":\"recovery\"");
  service.service();assert(fakeMqttInit==3);
  r.recovery=false;
#else
  service.service();
  assert(!fakeMqttClient&&!fakeMqttInit);expect(service,"mqtt_tls_unavailable");
#endif
  config.tls=false;config.port=1883;std::strcpy(config.host,"local.example");service.configure(config);service.service();
  assert(fakeMqttClient&&fakeMqttScheme=="mqtt");
  assert(fakeMqttClient->settings.broker.address.transport==MQTT_TRANSPORT_OVER_TCP);
  assert(!fakeMqttClient->settings.broker.verification.crt_bundle_attach&&!fakeMqttClient->settings.network.disable_auto_reconnect);
  {
    DeviceApi api{r,f.actions,f.calibration,f.hardware,f.diagnostics,f.network,f.storage,f.ota,f.restart,&service};
    const auto read=[&](bool wire){
      char bytes[4096];JsonWriter j(bytes,sizeof bytes);int code=200;const char *type=nullptr;
      api.execute("GET","/api/mqtt",{},j,code,type,wire);assert(code==200&&j.good());return std::string(bytes,j.size());
    };
    const auto first=read(true);fakeHardware.now+=1000;const auto second=read(true);
    assert(first==second&&first.size()<=672&&first.find("outboxAgeMs")==std::string::npos);
    assert(first.find("\"clientAllocated\":true")!=std::string::npos);
    assert(read(false).find("outboxAgeMs")!=std::string::npos);
  }
  // Changing credentials destroys the old adapter before mutating its inputs.
  std::strcpy(config.host,"changed.example");service.configure(config);assert(!fakeMqttClient);
  fakeMqttInitFailure=true;service.service();const auto attempts=fakeMqttInit;
  for(unsigned i=0;i<20;++i)service.service();
  assert(fakeMqttInit==attempts);
  fakeMqttInitFailure=false;fakeHardware.now+=10000;service.service();assert(fakeMqttClient);
  service.pause();assert(!fakeMqttClient);
  fakeMqttTaskFailure=true;service.service();assert(!fakeMqttClient);fakeMqttTaskFailure=false;
  service.pause();fakeMqttTasks.clear();
  assert(fakeMqttDestroy==fakeMqttInit-1); // One init was deliberately rejected.
  std::cout<<"PASS: production MQTT adapter TLS/CA/hostname settings, lazy allocation, retry guards, release on disable/recovery, credential lifetime and start failure cleanup\n";
}
