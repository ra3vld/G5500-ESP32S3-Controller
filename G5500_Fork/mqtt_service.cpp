#include "mqtt_service.hpp"
#include "runtime.hpp"
#if MQTT_SUPPORT
#include "mqtt_bridge.hpp"
#include "mqtt_publish_mailbox.hpp"
#include "mqtt_crypto_budget.hpp"
#include "mqtt_hud_queue.hpp"
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
#include "device_actions.hpp"
#include "hardware.hpp"
#include "heap_metrics.hpp"
#include "wifi_access.hpp"
#include "ethernet.hpp"
#include <mqtt_client.h>
#if MQTT_TLS_SUPPORT
#include <esp_crt_bundle.h>
#include <esp_sntp.h>
#include <ctime>
#if !CONFIG_MBEDTLS_CERTIFICATE_BUNDLE || !CONFIG_MQTT_TRANSPORT_SSL
#error "MQTT TLS requires SDK certificate bundle and SSL transport support"
#endif
#endif
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
  // Callbacks may outlive a hot configuration change while cleanup runs.
  const MqttConfig config;
  esp_mqtt_client_handle_t client=nullptr;
  std::atomic<bool> connected{false};
  std::atomic<int> connectError{0};
  std::atomic<int> transportErrorType{0},socketError{0},transportError{0};
  std::atomic<int> tlsError{0},tlsVerifyFlags{0};
  std::atomic<bool> retryNeeded{false};
  std::atomic<uint32_t> stackFree{0};
  std::atomic<uint32_t> epoch{0},outboxBytes{0},outboxAt{0};
  TaskHandle_t worker=nullptr;
  std::atomic<bool> stopping{false},workerStopped{false};
  uint32_t stopStartedUs=0,stopDurationUs=0;
  std::atomic<uint32_t> publishStackFree{0};
  std::atomic<uint32_t> disconnects{0},tcpRetired{0},tcpMssLimit{0};
  esp_transport_handle_t transport=nullptr;
  TcpPeerEndpoint peer;
  MqttPublishMailbox outgoing;
  MqttHudQueue hudIncoming,hudOutgoing;
  const bool hudRole;
  const uint64_t hudId;
  char hudTopic[48]{};
  std::atomic<uint32_t> hudRx{0},hudTx{0},hudDropped{0};
  std::atomic<bool> hudSocketReady{false};
  std::atomic<uint32_t> hudPublishMaxUs{0},hudPublishFailures{0};
  uint32_t hudEpoch=0,hudQueryAt=0,hudThrottled=0,hudControlDeferred=0,hudServiceMaxUs=0;
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
    self.closeClient();
    self.stopDurationUs=uint32_t(esp_timer_get_time())-self.stopStartedUs;
    // No access to Impl after publishing completion: the owner can release it.
    self.workerStopped.store(true,std::memory_order_release);vTaskDelete(nullptr);
  }
  void serviceOutgoing() {
    const uint32_t now=uint32_t(esp_timer_get_time()/1000);
    const auto queued=esp_mqtt_client_get_outbox_size(client);
    hudOutgoing.drain(epoch.load(),now,[&](const uint8_t *bytes,size_t size) {
      if(!connected.load() || paused.load())return true;
      if(queued && !mqttHudStop(bytes,size))return false;
      if(!mqttHudStop(bytes,size) && heapFreeBytes()<MemoryGuard::HealthyFree)return false;
      char topic[48];const auto target=mqttHudId(bytes+13);
      if(target)std::snprintf(topic,sizeof topic,"g5500/hud/%012llx/rx",static_cast<unsigned long long>(target));
      else std::strcpy(topic,"g5500/hud/discover");
      // Only the existing publisher task may call the SDK. QoS 0 sends HUD
      // frames directly, without accumulating them in the retry outbox.
      const auto started=esp_timer_get_time();
      const int sent=esp_mqtt_client_publish(client,topic,reinterpret_cast<const char *>(bytes),int(size),0,false);
      hudPublishMaxUs.store(std::max(hudPublishMaxUs.load(),uint32_t(esp_timer_get_time()-started)));
      if(sent<0){hudPublishFailures.fetch_add(1);return false;}
      hudTx.fetch_add(1);return true;
    });
    if(!hudOutgoing.pending())outgoing.drain(epoch.load(),now,[&](const auto &message) {
      if(paused.load())return true;
      if(!connected.load() || heapFreeBytes()<MemoryGuard::HealthyFree || heapMaxBlockBytes()<MemoryGuard::HealthyBlock)return false;
      const auto queued=esp_mqtt_client_get_outbox_size(client);
      // Serialize all QoS 0 traffic on this worker. Do not put periodic HA
      // state in the SDK retry outbox alongside the time-sensitive HUD path.
      if(message.qos==0)return !queued && esp_mqtt_client_publish(client,message.topic,message.size?message.payload:"",message.size,0,message.retain)>=0;
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
  explicit Impl(MqttService &service):owner(service),config(service.runtime_.mqtt),hudRole(service.runtime_.hud.isHud()),hudId(service.runtime_.hud.id),bridge(service.runtime_,service.runtime_.hud.id,
      {this,publish,idle,registryNext,registryRead,registryWrite,allocate,beforeCommand,afterCommand}) {
    std::snprintf(clientId,sizeof clientId,"g5500_%012llx",static_cast<unsigned long long>(service.runtime_.hud.id));
    std::snprintf(willTopic,sizeof willTopic,"%s/availability",bridge.baseTopic());
    std::snprintf(hudTopic,sizeof hudTopic,"g5500/hud/%012llx/rx",static_cast<unsigned long long>(hudId));
  }
  ~Impl() {
    // Normal retirement has already joined the SDK on the publisher. Only a
    // failed startup (before a worker exists) needs local cleanup here.
    if(!worker)closeClient();
  }
  void requestStop() {
    paused.store(true);
    bridge.connection(false);
    if(!stopping.load()) {
      stopStartedUs=uint32_t(esp_timer_get_time());
      stopping.store(true);schedule();
    }
  }
  void closeClient() {
    if(client) {
      // stop() sends a clean DISCONNECT, which suppresses the broker's LWT.
      // This worker has finished queued publications: send offline first.
      if(connected.load())esp_mqtt_client_publish(client,willTopic,"offline",7,0,true);
      esp_mqtt_client_stop(client);endMqttCryptoBudget();retirePeer();esp_mqtt_client_destroy(client);client=nullptr;
    }
    if(iterator){nvs_release_iterator(iterator);iterator=nullptr;}
    if(registry){nvs_close(registry);registry=0;}
  }
  static void event(void *context,esp_event_base_t,int32_t eventId,void *data) {
    auto &self=*static_cast<Impl *>(context);
    const auto *event=static_cast<esp_mqtt_event_handle_t>(data);
    self.stackFree.store(uxTaskGetStackHighWaterMark(nullptr));
    if(eventId==MQTT_EVENT_BEFORE_CONNECT) {
      if(self.config.tls)beginMqttCryptoBudget();
      self.retirePeer();self.tcpMssLimit.store(0);
      char plain[]="mqtt",secure[]="mqtts";
      self.transport=esp_mqtt_client_get_transport(self.client,self.config.tls?secure:plain);
    } else if(eventId==MQTT_EVENT_CONNECTED) {
      endMqttCryptoBudget();
      const int fd=self.transport?esp_transport_get_socket(self.transport):-1;
      self.peer=tcpPeerEndpoint(fd);
#if HUD_SUPPORT
      // Small control frames must not wait for Nagle or compete with bulk
      // HTTP in the default Wi-Fi queue. This adds no task or packet buffer.
      const int yes=1,priority=4<<5;
      const bool noDelay=fd>=0 && ::setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof yes)==0;
      const bool prioritized=fd>=0 && ::setsockopt(fd,IPPROTO_IP,IP_TOS,&priority,sizeof priority)==0;
      self.hudSocketReady.store(noDelay && prioritized);
#endif
#if FORK_CLASSIC_ESP32
      if(limitTcpSocketMss(fd,536))self.tcpMssLimit.store(536);
#endif
      self.epoch.fetch_add(1);
      self.connected.store(true);self.connectError.store(0);
      self.tlsError.store(0);self.tlsVerifyFlags.store(0);
      self.bridge.connection(!self.paused);
      char topic[80];std::snprintf(topic,sizeof topic,"%s/+/set/+",self.bridge.baseTopic());
      esp_mqtt_client_subscribe(self.client,topic,0);
#if HUD_SUPPORT
      esp_mqtt_client_subscribe(self.client,self.hudTopic,0);
      if(!self.hudRole)esp_mqtt_client_subscribe(self.client,"g5500/hud/discover",0);
#endif
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
      if(self.config.discovery) {
        std::snprintf(topic,sizeof topic,"%s/status",self.config.discoveryPrefix);
        esp_mqtt_client_subscribe(self.client,topic,0);
      }
#endif
    } else if(eventId==MQTT_EVENT_DISCONNECTED) {
      endMqttCryptoBudget();
      self.hudSocketReady.store(false);
      self.disconnects.fetch_add(1);self.retirePeer();self.tcpMssLimit.store(0);
      self.epoch.fetch_add(1);
      self.connected.store(false);self.bridge.connection(false);
      if(self.config.tls)self.retryNeeded.store(true);
    } else if(eventId==MQTT_EVENT_DATA) {
      const std::string_view topic(event->topic?event->topic:"",size_t(event->topic_len));
      const bool discovery=topic=="g5500/hud/discover";
      if(topic==self.hudTopic || discovery) {
        // A full HUD frame plus topic fits the existing 1 KiB SDK RX buffer.
        // Reject retained/oversized/partial frames in full, including tails.
        const auto *bytes=reinterpret_cast<const uint8_t *>(event->data);
        const size_t n=size_t(event->data_len);
        const bool valid=!self.paused.load() && !event->retain && self.connected.load() &&
            !event->current_data_offset && event->data_len==event->total_data_len && mqttHudFrame(bytes,n) &&
            mqttHudId(bytes+5)!=self.hudId &&
            (discovery ? !self.hudRole && bytes[4]==1 && !mqttHudId(bytes+13) : mqttHudId(bytes+13)==self.hudId);
        if(!valid || !self.hudIncoming.offer(bytes,n,self.epoch.load(),uint32_t(esp_timer_get_time()/1000)))self.hudDropped.fetch_add(1);
        return;
      }
      // Scalar commands fit one 1 KiB RX buffer. Oversized/fragmented messages
      // are rejected in full, including their trailing fragments.
      if(!self.paused && event->current_data_offset==0 && event->data_len==event->total_data_len &&
          event->topic_len>0 && event->topic_len<192 && event->data_len<=32)
        self.bridge.receive({event->topic,size_t(event->topic_len)},{event->data,size_t(event->data_len)},event->retain,uint32_t(esp_timer_get_time()/1000));
    } else if(eventId==MQTT_EVENT_ERROR) {
      endMqttCryptoBudget();
      self.connectError.store(event->error_handle?int(event->error_handle->connect_return_code):-1);
      if(event->error_handle) {
        self.transportErrorType.store(int(event->error_handle->error_type));
        self.socketError.store(event->error_handle->esp_transport_sock_errno);
        self.transportError.store(event->error_handle->esp_tls_last_esp_err);
        self.tlsError.store(event->error_handle->esp_tls_stack_err);
        self.tlsVerifyFlags.store(event->error_handle->esp_tls_cert_verify_flags);
      }
      self.connected.store(false);self.bridge.connection(false);
      const auto peer=tcpPeerEndpoint(self.transport?esp_transport_get_socket(self.transport):-1);
      if(peer.valid)self.peer=peer;
      self.retirePeer();
      if(self.config.tls)self.retryNeeded.store(true);
    }
    // The publisher polls every 250 ms. Callbacks only update atomics and the
    // ingress queue; they must not touch a task handle during start/teardown.
  }
  bool begin() {
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
    if(nvs_open("mqtt_reg",NVS_READWRITE,&registry)!=ESP_OK)return false;
#endif
    esp_mqtt_client_config_t settings{};
    settings.broker.address.hostname=config.host;settings.broker.address.port=config.port;
    settings.broker.address.transport=config.tls?MQTT_TRANSPORT_OVER_SSL:MQTT_TRANSPORT_OVER_TCP;
#if MQTT_TLS_SUPPORT
    // The SDK bundle stays in Flash; do not keep a PEM copy or parsed CA store.
    // Hostname and certificate validity checks remain enabled.
    if(config.tls)settings.broker.verification.crt_bundle_attach=esp_crt_bundle_attach;
#endif
    settings.credentials.client_id=clientId;
    if(config.username[0])settings.credentials.username=config.username;
    if(config.password[0])settings.credentials.authentication.password=config.password;
    settings.session.protocol_ver=MQTT_PROTOCOL_V_3_1_1;settings.session.keepalive=30;
    settings.session.disable_clean_session=false;
    settings.session.last_will={willTopic,"offline",7,1,1};
    settings.task.stack_size=6144;settings.task.priority=1;
    settings.buffer.size=1024;settings.buffer.out_size=1024;settings.outbox.limit=2048;
    settings.network.timeout_ms=2000;
    // Every TLS handshake, including retries, must pass the main-loop memory
    // and motion guards. The SDK must not start one independently under load.
    settings.network.disable_auto_reconnect=config.tls;
    // Even with automatic reconnect disabled, the SDK waits for half this
    // interval between task turns after a disconnect. Keep stop()/destroy()
    // responsive; the owner loop enforces the actual 10-second TLS cooldown.
    settings.network.reconnect_timeout_ms=config.tls?100:10000;
    client=esp_mqtt_client_init(&settings);
    if(!client || esp_mqtt_client_register_event(client,MQTT_EVENT_ANY,event,this)!=ESP_OK)return false;
    if(config.tls)pinMqttTlsClient(client);
    const auto started=esp_mqtt_client_start(client);
    pinMqttTlsClient(nullptr);
    if(started!=ESP_OK)return false;
#if !CONFIG_FREERTOS_UNICORE
    return xTaskCreatePinnedToCore(transmitTask,"mqtt-publish",3072,this,1,&worker,0)==pdPASS;
#else
    return xTaskCreate(transmitTask,"mqtt-publish",3072,this,1,&worker)==pdPASS;
#endif
  }
};
#else
struct MqttService::Impl {};
#endif
MqttService::MqttService(Runtime &r,DeviceActions &a,Hardware &h):runtime_(r),actions_(a),hardware_(h){}
MqttService::~MqttService() {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  // Application destruction is not a controller-loop operation. Keep the
  // adapter alive until its callbacks and worker have exited.
  if(impl_ && impl_->worker) {
    impl_->requestStop();
    while(!impl_->workerStopped.load(std::memory_order_acquire))vTaskDelay(1);
  }
#endif
}
void MqttService::reap() {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  if(impl_ && impl_->workerStopped.load(std::memory_order_acquire)) {
    stopMaxUs_=std::max(stopMaxUs_,impl_->stopDurationUs);
    impl_.reset();
  }
#endif
}
void MqttService::service() {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  reap();
  if(impl_ && impl_->stopping.load()){error_="stopping";return;}
  if(runtime_.recovery){pause();return;}
  if(!runtime_.mqttStorageOk){pause();error_="mqtt_storage_failed";return;}
  if(!runtime_.mqtt.enabled){pause();error_="disabled";return;}
  if(impl_ && impl_->retryNeeded.load()) {
    tlsError_=impl_->tlsError.load();tlsVerifyFlags_=impl_->tlsVerifyFlags.load();
    brokerError_=impl_->connectError.load();
    pause();retryAt_=millis();retryPending_=true;error_="mqtt_tls_retry";
    return;
  }
  if(!impl_) {
    if(retryPending_ && uint32_t(millis()-retryAt_)<10000){error_="mqtt_tls_retry";return;}
    retryPending_=false;
    if(!runtime_.hud.isHud() && runtime_.controller.controlActive()){error_="motion_active";return;}
    if(!ethernetConnected() && radioWifi.status()!=WL_CONNECTED && (runtime_.mqtt.tls || !(radioWifi.getMode()&WIFI_AP))){error_="network_unavailable";return;}
    if(runtime_.mqtt.tls) {
#if !MQTT_TLS_SUPPORT
      error_="mqtt_tls_unavailable";return;
#endif
    }
    // TLS uses the prebuilt SDK's record buffers and handshake workspace.
    // These are conservative admission limits, not a measured TLS heap peak.
    const size_t reserve=runtime_.mqtt.tls?65536:24576;
    const size_t block=runtime_.mqtt.tls?32768:MemoryGuard::HealthyBlock;
    if(heapFreeBytes()<MemoryGuard::HealthyFree+reserve || heapMaxBlockBytes()<std::max<size_t>(sizeof(Impl)+MemoryGuard::HealthyBlock,block)) {
      error_=runtime_.mqtt.tls?"mqtt_tls_memory_limited":"mqtt_memory_limited";return;
    }
#if MQTT_TLS_SUPPORT
    if(runtime_.mqtt.tls && std::time(nullptr)<=1700000000) {
#if NTP_SUPPORT
      // Wi-Fi may have appeared after the boot-time NTP attempt. Start the
      // shared clock service once, without blocking control or creating a task.
      if(!esp_sntp_enabled())configTime(0,0,"pool.ntp.org","time.nist.gov");
#endif
      error_="mqtt_tls_time_required";return;
    }
#endif
    impl_.reset(new(std::nothrow) Impl(*this));
    if(!impl_ || !impl_->begin()){impl_.reset();retryAt_=millis();retryPending_=true;error_="mqtt_start_failed";return;}
  }
  error_=impl_->connected.load()?"":"connecting";
  const auto epoch=impl_->epoch.load();
  if(epoch!=impl_->hudEpoch) {
    impl_->hudEpoch=epoch;runtime_.hud.disconnectMqtt();
  }
  const auto started=micros();
  impl_->hudIncoming.drain(epoch,millis(),[&](const uint8_t *bytes,size_t size) {
    if(!impl_->connected.load())return true;
    const auto kind=bytes[4];const auto at=millis();
    if(!runtime_.hud.isHud() && (kind==1 || kind==2 || kind==11)) {
      // Let the imminent motor step run first. Retain only this one query;
      // commands and the reserved STOP slots never wait for control headroom.
      if(runtime_.controller.controlActive() && runtime_.controller.controlDue(at+1)){++impl_->hudControlDeferred;return false;}
      // Aggregate limit even with several HUDs or a flooding broker client.
      if(impl_->hudQueryAt && uint32_t(at-impl_->hudQueryAt)<(networkLowMemory()?500u:50u)) {
        ++impl_->hudThrottled;return true;
      }
      impl_->hudQueryAt=at;
    }
    runtime_.hud.receive(HudEndpoint::mqttNode(mqttHudId(bytes+5)),bytes,size,at);
    impl_->hudRx.fetch_add(1);
    if(!runtime_.hud.isHud() && (kind==4 || kind==9 || kind==15))hardware_.applyOutputs();
    return true;
  });
  impl_->hudServiceMaxUs=std::max(impl_->hudServiceMaxUs,uint32_t(micros()-started));
  impl_->bridge.service(millis());
#endif
}
bool MqttService::sendHud(const HudEndpoint &endpoint,const uint8_t *bytes,size_t size) {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO) && HUD_SUPPORT
  if(!endpoint.mqtt || !impl_ || !impl_->connected.load() || impl_->paused.load() ||
      !mqttHudFrame(bytes,size) || endpoint.mqttId()!=mqttHudId(bytes+13))return false;
  const bool queued=impl_->hudOutgoing.offer(bytes,size,impl_->epoch.load(),millis());
  if(queued)impl_->schedule();else impl_->hudDropped.fetch_add(1);
  return queued;
#else
  (void)endpoint;(void)bytes;(void)size;return false;
#endif
}
void MqttService::pause() {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  reap();
  if(impl_) {
    if(!impl_->stopping.load())runtime_.hud.disconnectMqtt();
    impl_->requestStop();
    error_="stopping";return;
  }
#endif
  error_=runtime_.recovery?"recovery":"disabled";
}
void MqttService::configure(const MqttConfig &config) {
  // Call only on the owner loop, with outputs stopped and NVS verified.
  // The retiring adapter owns a credentials snapshot until cleanup completes.
  pause();
  runtime_.mqtt=config;runtime_.mqttStorageOk=true;
  retryPending_=false;tlsError_=tlsVerifyFlags_=brokerError_=0;
  error_=runtime_.recovery?"recovery":config.enabled?"not_started":"disabled";
}
bool MqttService::connected() const {
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  return impl_ && !impl_->paused.load() && impl_->connected.load();
#else
  return false;
#endif
}
void MqttService::writeStatus(JsonWriter &j, bool compact) const {
  j << "\"supported\":true,\"connected\":";
#if defined(ARDUINO_ARCH_ESP32) && defined(ARDUINO)
  j << (connected()?"true":"false") << ",\"error\":" << jsonQuoted(impl_ && impl_->stopping.load()?"stopping":error_);
  j << ",\"tlsError\":" << (impl_?impl_->tlsError.load():tlsError_)
    << ",\"tlsVerifyFlags\":" << (impl_?impl_->tlsVerifyFlags.load():tlsVerifyFlags_)
    << ",\"clientAllocated\":" << (impl_?"true":"false")
    << ",\"stopping\":" << (impl_ && impl_->stopping.load()?"true":"false")
    << ",\"brokerError\":" << (impl_?impl_->connectError.load():brokerError_);
  if(compact)return;
  j << ",\"stopMaxUs\":" << stopMaxUs_;
  j << ",\"cryptoCheckpoints\":" << mqttCryptoBudget.checkpoints.load()
    << ",\"cryptoYields\":" << mqttCryptoBudget.yields.load()
    << ",\"cryptoGapMaxUs\":" << mqttCryptoBudget.maxGapUs.load();
  if(impl_)j << ",\"baseTopic\":" << jsonQuoted(impl_->bridge.baseTopic()) << ",\"nodes\":" << impl_->bridge.nodeCount()
    << ",\"rejected\":" << impl_->bridge.rejected() << ",\"dropped\":" << impl_->bridge.dropped()
    << ",\"commands\":" << impl_->bridge.commands() << ",\"lastResult\":" << jsonQuoted(impl_->bridge.lastResult())
    << ",\"requestId\":" << impl_->bridge.pendingRequest() << ",\"outboxBytes\":" << impl_->outboxBytes.load()
    << ",\"outboxAgeMs\":" << uint32_t(millis()-impl_->outboxAt.load())
    << ",\"stackFreeBytes\":" << impl_->stackFree.load()
    << ",\"publishStackFreeBytes\":" << impl_->publishStackFree.load();
  if(impl_)j << ",\"disconnects\":" << impl_->disconnects.load() << ",\"tcpRetired\":" << impl_->tcpRetired.load()
    << ",\"transportErrorType\":" << impl_->transportErrorType.load() << ",\"socketError\":" << impl_->socketError.load()
    << ",\"transportError\":" << impl_->transportError.load()
    << ",\"tcpMssLimit\":" << impl_->tcpMssLimit.load()
    << ",\"hudRxFrames\":" << impl_->hudRx.load() << ",\"hudTxFrames\":" << impl_->hudTx.load()
    << ",\"hudDropped\":" << impl_->hudDropped.load() << ",\"hudThrottled\":" << impl_->hudThrottled
    << ",\"hudControlDeferred\":" << impl_->hudControlDeferred
    << ",\"hudSocketReady\":" << (impl_->hudSocketReady.load()?"true":"false")
    << ",\"hudPublishMaxUs\":" << impl_->hudPublishMaxUs.load() << ",\"hudPublishFailures\":" << impl_->hudPublishFailures.load()
    << ",\"hudBufferBytes\":" << (HUD_SUPPORT?sizeof(MqttHudQueue)*2:0) << ",\"hudServiceMaxUs\":" << impl_->hudServiceMaxUs;
#else
  (void)compact;
  j << "false,\"error\":" << jsonQuoted(runtime_.mqtt.enabled?"not_started":"disabled");
#endif
}
}
#endif
