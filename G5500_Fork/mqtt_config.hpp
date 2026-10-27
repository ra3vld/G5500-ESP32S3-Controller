// Bounded MQTT configuration, independent of transport and NVS ABI. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
namespace forkrot {
struct MqttConfig {
  bool enabled=false, discovery=HOME_ASSISTANT_DISCOVERY_SUPPORT;
  uint16_t port=1883;
  char host[96]{}, username[64]{}, password[96]{}, discoveryPrefix[48]="homeassistant";
  const char *validate() const {
    if(!port || !std::memchr(host,0,sizeof host) || !std::memchr(username,0,sizeof username) ||
        !std::memchr(password,0,sizeof password) || !std::memchr(discoveryPrefix,0,sizeof discoveryPrefix))return "invalid_mqtt_config";
    if(enabled && !host[0])return "mqtt_host_required";
    for(unsigned char c:std::string_view(host))if(c<=32 || c>=127 || c=='/' || c=='#' || c=='+' || c=='@')return "invalid_mqtt_host";
    const std::string_view prefix(discoveryPrefix);
    if(prefix.empty() || prefix.front()=='/' || prefix.back()=='/')return "invalid_discovery_prefix";
    for(unsigned char c:prefix)if(c<=32 || c=='#' || c=='+')return "invalid_discovery_prefix";
    return nullptr;
  }
  bool operator==(const MqttConfig &o) const {
    return enabled==o.enabled && discovery==o.discovery && port==o.port &&
      !std::strcmp(host,o.host) && !std::strcmp(username,o.username) && !std::strcmp(password,o.password) &&
      !std::strcmp(discoveryPrefix,o.discoveryPrefix);
  }
};
inline constexpr size_t MqttRecordSize=8+96+64+96+48+4;
using MqttRecord=std::array<uint8_t,MqttRecordSize>;
inline MqttRecord encodeMqtt(const MqttConfig &c) {
  MqttRecord b{'M','Q',1,uint8_t(c.enabled),uint8_t(c.discovery),uint8_t(c.port),uint8_t(c.port>>8),0};
  size_t at=8;
  const auto copy=[&](const char *s,size_t size){std::memcpy(b.data()+at,s,size);at+=size;};
  copy(c.host,sizeof c.host);copy(c.username,sizeof c.username);copy(c.password,sizeof c.password);copy(c.discoveryPrefix,sizeof c.discoveryPrefix);
  uint32_t crc=2166136261u;for(size_t i=0;i<b.size()-4;++i)crc=(crc^b[i])*16777619u;
  for(unsigned i=0;i<4;++i)b[b.size()-4+i]=uint8_t(crc>>(i*8));
  return b;
}
inline bool decodeMqtt(const MqttRecord &b,MqttConfig &c) {
  if(b[0]!='M'||b[1]!='Q'||b[2]!=1||b[3]>1||b[4]>1||b[7])return false;
  MqttConfig next;next.enabled=b[3];next.discovery=b[4];next.port=uint16_t(b[5])|(uint16_t(b[6])<<8);
  size_t at=8;
  const auto copy=[&](char *s,size_t size){std::memcpy(s,b.data()+at,size);at+=size;};
  copy(next.host,sizeof next.host);copy(next.username,sizeof next.username);copy(next.password,sizeof next.password);copy(next.discoveryPrefix,sizeof next.discoveryPrefix);
  if(next.validate() || encodeMqtt(next)!=b)return false;
  c=next;return true;
}
template<size_t N> inline bool mqttText(std::string_view text,char (&out)[N]) {
  if(text.size()>=N || text.find('\0')!=text.npos)return false;
  std::memset(out,0,N);std::memcpy(out,text.data(),text.size());return true;
}
}
