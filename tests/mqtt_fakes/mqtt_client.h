#pragma once
#include <cassert>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include "esp_transport.h"
using esp_event_base_t=void *;
inline constexpr int MQTT_EVENT_ANY=-1,MQTT_EVENT_BEFORE_CONNECT=1,MQTT_EVENT_CONNECTED=2,
 MQTT_EVENT_DISCONNECTED=3,MQTT_EVENT_DATA=4,MQTT_EVENT_ERROR=5;
inline constexpr int MQTT_TRANSPORT_OVER_TCP=1,MQTT_TRANSPORT_OVER_SSL=2,MQTT_PROTOCOL_V_3_1_1=4;
struct esp_mqtt_client_config_t {
  struct {
    struct {const char *hostname=nullptr;unsigned port=0;int transport=0;} address;
    struct {int (*crt_bundle_attach)(void *)=nullptr;bool skip_cert_common_name_check=false;const char *certificate=nullptr;} verification;
  } broker;
  struct {const char *client_id=nullptr,*username=nullptr;struct {const char *password=nullptr;} authentication;} credentials;
  struct {
    int protocol_ver=0,keepalive=0;bool disable_clean_session=false;
    struct {const char *topic=nullptr,*msg=nullptr;int msg_len=0,qos=0,retain=0;} last_will;
  } session;
  struct {int stack_size=0,priority=0;} task;
  struct {int size=0,out_size=0;} buffer;
  struct {int limit=0;} outbox;
  struct {int timeout_ms=0,reconnect_timeout_ms=0;bool disable_auto_reconnect=false;} network;
};
struct FakeMqttError {int connect_return_code=0,error_type=0,esp_transport_sock_errno=0,
 esp_tls_last_esp_err=0,esp_tls_stack_err=0,esp_tls_cert_verify_flags=0;};
struct FakeMqttEvent {
 const char *topic=nullptr,*data=nullptr;int topic_len=0,data_len=0,total_data_len=0,current_data_offset=0;bool retain=false;
 FakeMqttError *error_handle=nullptr;
};
using esp_mqtt_event_handle_t=FakeMqttEvent *;
struct FakeMqttClient {
  esp_mqtt_client_config_t settings;
  void (*event)(void *,esp_event_base_t,int32_t,void *)=nullptr;void *context=nullptr;
  std::string originalHost;
};
using esp_mqtt_client_handle_t=FakeMqttClient *;
inline FakeMqttClient *fakeMqttClient=nullptr;
inline unsigned fakeMqttInit=0,fakeMqttDestroy=0,fakeMqttStop=0;
inline unsigned fakeMqttOffline=0;
inline bool fakeMqttInitFailure=false,fakeMqttStartFailure=false;
inline std::atomic<bool> fakeMqttHoldStop{false},fakeMqttStopEntered{false};
inline std::atomic<bool> fakeMqttHoldPublish{false},fakeMqttPublishEntered{false};
inline std::string fakeMqttScheme;
inline esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *settings){
  ++fakeMqttInit;if(fakeMqttInitFailure)return nullptr;
  assert(!fakeMqttClient);fakeMqttClient=new FakeMqttClient{*settings,nullptr,nullptr,settings->broker.address.hostname};return fakeMqttClient;
}
inline int esp_mqtt_client_register_event(FakeMqttClient *client,int,void (*event)(void *,esp_event_base_t,int32_t,void *),void *context){
 client->event=event;client->context=context;return 0;
}
inline void fakeMqttEvent(int id,FakeMqttEvent event={}){assert(fakeMqttClient);fakeMqttClient->event(fakeMqttClient->context,nullptr,id,&event);}
inline int esp_mqtt_client_start(FakeMqttClient *){if(fakeMqttStartFailure)return -1;fakeMqttEvent(MQTT_EVENT_BEFORE_CONNECT);return 0;}
inline int esp_mqtt_client_stop(FakeMqttClient *client){
 fakeMqttStopEntered.store(true);
 while(fakeMqttHoldStop.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
 ++fakeMqttStop;assert(client->originalHost==client->settings.broker.address.hostname);return 0;
}
inline int esp_mqtt_client_destroy(FakeMqttClient *client){++fakeMqttDestroy;delete client;fakeMqttClient=nullptr;return 0;}
inline esp_transport_handle_t esp_mqtt_client_get_transport(FakeMqttClient *,char *scheme){fakeMqttScheme=scheme;return nullptr;}
inline int esp_mqtt_client_get_outbox_size(FakeMqttClient *){return 0;}
inline int esp_mqtt_client_publish(FakeMqttClient *,const char *,const char *data,int size,int qos,bool retain){
 fakeMqttPublishEntered=true;
 while(fakeMqttHoldPublish.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
 if(size==7&&std::string(data,size)=="offline"){assert(qos==0&&retain);++fakeMqttOffline;}return 0;
}
inline int esp_mqtt_client_enqueue(FakeMqttClient *,const char *,const char *,int,int,bool,bool){return 0;}
inline int esp_mqtt_client_subscribe(FakeMqttClient *,const char *,int){return 0;}
