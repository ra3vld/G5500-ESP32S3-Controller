// ESP-MQTT adapter; optional buffers and task are created lazily. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "json_writer.hpp"
#include <memory>
namespace forkrot {
struct Runtime;
class DeviceActions;
class Hardware;
#if MQTT_SUPPORT
class MqttService {
  struct Impl;
  Runtime &runtime_;
  DeviceActions &actions_;
  Hardware &hardware_;
  std::unique_ptr<Impl> impl_;
  const char *error_="disabled";
public:
  MqttService(Runtime &,DeviceActions &,Hardware &);
  ~MqttService();
  void service();
  void pause();
  void writeStatus(JsonWriter &) const;
};
#else
class MqttService {
public:
  MqttService(Runtime &,DeviceActions &,Hardware &){}
  void service(){}
  void pause(){}
  void writeStatus(JsonWriter &j) const { j << "\"supported\":false,\"connected\":false,\"error\":\"feature_disabled\""; }
};
#endif
}
