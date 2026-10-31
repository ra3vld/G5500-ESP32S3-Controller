#include "mqtt_service.hpp"
#include "runtime.hpp"
#if MQTT_SUPPORT
#include "mqtt_bridge.hpp"
#include "mqtt_publish_mailbox.hpp"
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
#include "device_actions.hpp"
#include "hardware.hpp"
#include "heap_metrics.hpp"
#include "wifi_access.hpp"
#include "ethernet.hpp"
#include <mqtt_client.h>
#include <esp_transport.h>
#include <tcp_peer.hpp>
#include <nvs.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
namespace forkrot {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
struct MqttService::Impl {
  MqttService &owner;
  esp_mqtt_client_handle_t client=nullptr;
  std::atomic<bool> connected{false};
  std::atomic<int> connectError{0};
  std::atomic<uint32_t> stackFree{0};
  std::atomic<uint32_t> epoch{0},outboxBytes{0},outboxAt{0};
  TaskHandle_t worker=nullptr;
  std::atomic<bool> stopping{false},workerStopped{false};
  std::atomic<uint32_t> publishStackFree{0};
  std::atomic<uint32_t> disconnects{0},tcpRetired{0},tcpMssLimit{0};
  esp_transport_handle_t transport=nullptr;
  TcpPeerEndpoint peer;
  MqttPublishMailbox outgoing;
  bool offlineQueued=false;
  nvs_handle_t registry=0;
  nvs_iterator_t iterator=nullptr;
  bool registryStarted=false;
  std::atomic<bool> paused{false};
  char clientId[40]{},willTopic[64]{};
  MqttBridge bridge;
  void retirePeer() { if(abortTcpPeerEndpoint(peer))tcpRetired.fetch_add(1);peer={}; }
  void schedule() { if(worker)xTaskNotifyGive(worker); }
  static void transmitTask(void *context) {
    auto &self=*static_cast<Impl *>(context);
    while(!self.stopping.load()) {
      ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(250));
      if(self.stopping.load())break;
      self.serviceOutgoing();
      self.publishStackFree.store(uxTaskGetStackHighWaterMark(nullptr));
    }
    self.workerStopped.store(true);vTaskDelete(nullptr);
  }
  void serviceOutgoing() {
    const uint32_t now=uint32_t(esp_timer_get_time()/1000);
    outgoing.drain(epoch.load(),now,[&](const auto &message) {
      if(!connected.load() || heapFreeBytes()<MemoryGuard::HealthyFree || heapMaxBlockBytes()<MemoryGuard::HealthyBlock)return false;
      const auto queued=esp_mqtt_client_get_outbox_size(client);
      if(size_t(queued)+std::strlen(message.topic)+message.size+16>2048)return false;
      const bool ok=esp_mqtt_client_enqueue(client,message.topic,message.size?message.payload:"",message.size,message.qos,message.retain,true)>=0;
      outboxBytes.store(esp_mqtt_client_get_outbox_size(client));outboxAt.store(now);
      return ok;
    });
    outboxBytes.store(esp_mqtt_client_get_outbox_size(client));outboxAt.store(now);
  }
  static bool publish(void *context,const char *topic,std::string_view payload,bool retain,int qos) {
    auto &self=*static_cast<Impl *>(context);
    if(!self.client || !self.connected.load() || heapFreeBytes()<MemoryGuard::HealthyFree ||
        heapMaxBlockBytes()<MemoryGuard::HealthyBlock)return false;
    const bool queued=self.outgoing.offer(topic,payload,retain,qos,self.epoch.load(),uint32_t(esp_timer_get_time()/1000));
    if(queued)self.schedule();
    return queued;
  }
  static bool idle(void *context) {
    auto &self=*static_cast<Impl *>(context);
    return self.client && self.outgoing.empty() && self.outboxBytes.load()==0;
  }
  static bool allocate(void *,size_t n) {
    return heapFreeBytes()>=n+MemoryGuard::HealthyFree && heapMaxBlockBytes()>=n+MemoryGuard::HealthyBlock;
  }
  static void key(uint64_t id,char (&out)[16]) {
    std::snprintf(out,sizeof out,"p%012llx",static_cast<unsigned long long>(id));
  }
  static bool registryNext(void *context,uint64_t &id) {
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
    auto &self=*static_cast<Impl *>(context);
    if(!self.registry)return false;
    if(!self.registryStarted) {
      self.registryStarted=true;
      if(nvs_entry_find("nvs","mqtt_reg",NVS_TYPE_STR,&self.iterator)!=ESP_OK)return false;
    } else if(!self.iterator || nvs_entry_next(&self.iterator)!=ESP_OK)return false;
    do {
      nvs_entry_info_t info{};
      if(nvs_entry_info(self.iterator,&info)!=ESP_OK)return false;
      if(info.key[0]=='p' && std::strlen(info.key)==13) {
        char *end=nullptr;id=std::strtoull(info.key+1,&end,16);
        if(end && !*end)return true;
      }
    } while(nvs_entry_next(&self.iterator)==ESP_OK);
    return false;
#else
    (void)context;(void)id;return false;
#endif
  }
  static bool registryRead(void *context,uint64_t id,char *prefix,size_t capacity) {
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
    auto &self=*static_cast<Impl *>(context);if(!self.registry)return false;
    char name[16];key(id,name);size_t size=capacity;
    return nvs_get_str(self.registry,name,prefix,&size)==ESP_OK && size>1 && size<=capacity;
#else
    (void)context;(void)id;(void)prefix;(void)capacity;return false;
#endif
  }
  static bool registryWrite(void *context,uint64_t id,const char *prefix) {
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
    auto &self=*static_cast<Impl *>(context);if(!self.registry)return false;
    // Metadata writes wait for stopped outputs; ordinary telemetry never writes NVS.
    if(self.owner.runtime_.controller.controlActive())return false;
    char name[16];key(id,name);
    const auto result=prefix?nvs_set_str(self.registry,name,prefix):nvs_erase_key(self.registry,name);
    return (result==ESP_OK || (!prefix && result==ESP_ERR_NVS_NOT_FOUND)) && nvs_commit(self.registry)==ESP_OK;
#else
    (void)context;(void)id;(void)prefix;return true;
#endif
  }
  static void beforeCommand(void *context) {static_cast<Impl *>(context)->owner.actions_.updatePosition();}
  static void afterCommand(void *context,bool persist) {
    auto &self=*static_cast<Impl *>(context);
    self.owner.hardware_.applyOutputs();
    if(persist)self.owner.actions_.saveForkSettings();
  }
  explicit Impl(MqttService &service):owner(service),bridge(service.runtime_,service.runtime_.hud.id,
      {this,publish,idle,registryNext,registryRead,registryWrite,allocate,beforeCommand,afterCommand}) {
    std::snprintf(clientId,sizeof clientId,"g5500_%012llx",static_cast<unsigned long long>(service.runtime_.hud.id));
    std::snprintf(willTopic,sizeof willTopic,"%s/availability",bridge.baseTopic());
  }
  ~Impl() {
    if(worker) {
      stopping.store(true);schedule();
      while(!workerStopped.load())vTaskDelay(1);
    }
    if(client){esp_mqtt_client_stop(client);retirePeer();esp_mqtt_client_destroy(client);}
    if(iterator)nvs_release_iterator(iterator);
    if(registry)nvs_close(registry);
  }
  static void event(void *context,esp_event_base_t,int32_t eventId,void *data) {
    auto &self=*static_cast<Impl *>(context);
    const auto *event=static_cast<esp_mqtt_event_handle_t>(data);
    self.stackFree.store(uxTaskGetStackHighWaterMark(nullptr));
    if(eventId==MQTT_EVENT_BEFORE_CONNECT) {
      self.retirePeer();self.tcpMssLimit.store(0);
      char scheme[]="mqtt";self.transport=esp_mqtt_client_get_transport(self.client,scheme);
    } else if(eventId==MQTT_EVENT_CONNECTED) {
      const int fd=self.transport?esp_transport_get_socket(self.transport):-1;
      self.peer=tcpPeerEndpoint(fd);
#if FORK_CLASSIC_ESP32
      if(limitTcpSocketMss(fd,536))self.tcpMssLimit.store(536);
#endif
      self.epoch.fetch_add(1);
      self.connected.store(true);self.connectError.store(0);
      self.bridge.connection(!self.paused);
      char topic[80];std::snprintf(topic,sizeof topic,"%s/+/set/+",self.bridge.baseTopic());
      esp_mqtt_client_subscribe(self.client,topic,0);
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
      if(self.owner.runtime_.mqtt.discovery) {
        std::snprintf(topic,sizeof topic,"%s/status",self.owner.runtime_.mqtt.discoveryPrefix);
        esp_mqtt_client_subscribe(self.client,topic,0);
      }
#endif
    } else if(eventId==MQTT_EVENT_DISCONNECTED) {
      self.disconnects.fetch_add(1);self.retirePeer();self.tcpMssLimit.store(0);
      self.epoch.fetch_add(1);
      self.connected.store(false);self.bridge.connection(false);
    } else if(eventId==MQTT_EVENT_DATA) {
      // Scalar commands fit one 1 KiB RX buffer. Oversized/fragmented messages
      // are rejected in full, including their trailing fragments.
      if(!self.paused && event->current_data_offset==0 && event->data_len==event->total_data_len &&
          event->topic_len>0 && event->topic_len<192 && event->data_len<=32)
        self.bridge.receive({event->topic,size_t(event->topic_len)},{event->data,size_t(event->data_len)},event->retain,uint32_t(esp_timer_get_time()/1000));
    } else if(eventId==MQTT_EVENT_ERROR) {
      self.connectError.store(event->error_handle?int(event->error_handle->connect_return_code):-1);
      self.connected.store(false);self.bridge.connection(false);
      const auto peer=tcpPeerEndpoint(self.transport?esp_transport_get_socket(self.transport):-1);
      if(peer.valid)self.peer=peer;
      self.retirePeer();
    }
    // The publisher polls every 250 ms. Callbacks only update atomics and the
    // ingress queue; they must not touch a task handle during start/teardown.
  }
  bool begin() {
    const auto &config=owner.runtime_.mqtt;
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
    if(nvs_open("mqtt_reg",NVS_READWRITE,&registry)!=ESP_OK)return false;
#endif
    esp_mqtt_client_config_t settings{};
    settings.broker.address.hostname=config.host;settings.broker.address.port=config.port;
    settings.broker.address.transport=MQTT_TRANSPORT_OVER_TCP;
    settings.credentials.client_id=clientId;
    if(config.username[0])settings.credentials.username=config.username;
    if(config.password[0])settings.credentials.authentication.password=config.password;
    settings.session.protocol_ver=MQTT_PROTOCOL_V_3_1_1;settings.session.keepalive=30;
    settings.session.disable_clean_session=false;
    settings.session.last_will={willTopic,"offline",7,1,1};
    settings.task.stack_size=6144;settings.task.priority=1;
    settings.buffer.size=1024;settings.buffer.out_size=1024;settings.outbox.limit=2048;
    settings.network.timeout_ms=2000;settings.network.reconnect_timeout_ms=10000;
    client=esp_mqtt_client_init(&settings);
    return client && esp_mqtt_client_register_event(client,MQTT_EVENT_ANY,event,this)==ESP_OK &&
        esp_mqtt_client_start(client)==ESP_OK && xTaskCreate(transmitTask,"mqtt-publish",3072,this,1,&worker)==pdPASS;
  }
};
#else
struct MqttService::Impl {};
#endif
MqttService::MqttService(Runtime &r,DeviceActions &a,Hardware &h):runtime_(r),actions_(a),hardware_(h){}
MqttService::~MqttService()=default;
void MqttService::service() {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  if(!runtime_.mqttStorageOk){error_="mqtt_storage_failed";return;}
  if(!runtime_.mqtt.enabled){error_="disabled";return;}
  if(runtime_.recovery){pause();return;}
  if(!impl_) {
    if(!runtime_.hud.isHud() && runtime_.controller.controlActive()){error_="motion_active";return;}
    if(!ethernetConnected() && radioWifi.status()!=WL_CONNECTED && !(radioWifi.getMode()&WIFI_AP)){error_="network_unavailable";return;}
    constexpr size_t Reserve=24576;
    if(heapFreeBytes()<MemoryGuard::HealthyFree+Reserve || heapMaxBlockBytes()<sizeof(Impl)+MemoryGuard::HealthyBlock) {error_="mqtt_memory_limited";return;}
    impl_.reset(new(std::nothrow) Impl(*this));
    if(!impl_ || !impl_->begin()){impl_.reset();error_="mqtt_start_failed";return;}
  }
  if(impl_->paused) {
    if(!impl_->offlineQueued)impl_->offlineQueued=Impl::publish(impl_.get(),impl_->willTopic,"offline",true,1);
    return;
  }
  error_=impl_->connected.load()?"":"connecting";
  impl_->bridge.service(millis());
#endif
}
void MqttService::pause() {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  if(impl_) {
    if(!impl_->paused){impl_->paused=true;impl_->bridge.connection(false);}
    if(!impl_->offlineQueued)impl_->offlineQueued=Impl::publish(impl_.get(),impl_->willTopic,"offline",true,1);
  }
#endif
}
void MqttService::writeStatus(JsonWriter &j, bool compact) const {
  j << "\"supported\":true,\"connected\":";
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  j << (impl_ && impl_->connected.load()?"true":"false") << ",\"error\":" << jsonQuoted(error_);
  if(compact) {
    if(impl_)j << ",\"brokerError\":" << impl_->connectError.load();
    return;
  }
  if(impl_)j << ",\"baseTopic\":" << jsonQuoted(impl_->bridge.baseTopic()) << ",\"nodes\":" << impl_->bridge.nodeCount()
    << ",\"rejected\":" << impl_->bridge.rejected() << ",\"dropped\":" << impl_->bridge.dropped()
    << ",\"commands\":" << impl_->bridge.commands() << ",\"lastResult\":" << jsonQuoted(impl_->bridge.lastResult())
    << ",\"requestId\":" << impl_->bridge.pendingRequest() << ",\"outboxBytes\":" << impl_->outboxBytes.load()
    << ",\"outboxAgeMs\":" << uint32_t(millis()-impl_->outboxAt.load())
    << ",\"stackFreeBytes\":" << impl_->stackFree.load()
    << ",\"publishStackFreeBytes\":" << impl_->publishStackFree.load() << ",\"brokerError\":" << impl_->connectError.load();
  if(impl_)j << ",\"disconnects\":" << impl_->disconnects.load() << ",\"tcpRetired\":" << impl_->tcpRetired.load()
    << ",\"tcpMssLimit\":" << impl_->tcpMssLimit.load();
#else
  (void)compact;
  j << "false,\"error\":" << jsonQuoted(runtime_.mqtt.enabled?"not_started":"disabled");
#endif
}
}
#endif
