// ESP-MQTT adapter; optional buffers and task are created lazily. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "json_writer.hpp"
#include "mqtt_config.hpp"
#include <memory>
namespace forkrot {
struct Runtime;
class DeviceActions;
class Hardware;
struct HudEndpoint;
#if MQTT_SUPPORT
class MqttService {
  struct Impl;
  Runtime &runtime_;
  DeviceActions &actions_;
  Hardware &hardware_;
  std::unique_ptr<Impl> impl_;
  const char *error_="disabled";
  uint32_t retryAt_=0;
  bool retryPending_=false;
  uint32_t stopMaxUs_=0;
  void reap();
  int tlsError_=0, tlsVerifyFlags_=0, brokerError_=0;
public:
  MqttService(Runtime &,DeviceActions &,Hardware &);
  ~MqttService();
  void service();
  void pause();
  void configure(const MqttConfig &);
  bool connected() const;
  bool sendHud(const HudEndpoint &,const uint8_t *,size_t);
  void writeStatus(JsonWriter &, bool compact = false) const;
};
#else
class MqttService {
public:
  MqttService(Runtime &,DeviceActions &,Hardware &){}
  void service(){}
  void pause(){}
  void configure(const MqttConfig &){}
  bool connected() const {return false;}
  bool sendHud(const HudEndpoint &,const uint8_t *,size_t){return false;}
  void writeStatus(JsonWriter &j, bool = false) const { j << "\"supported\":false,\"connected\":false,\"error\":\"feature_disabled\""; }
};
#endif
}
