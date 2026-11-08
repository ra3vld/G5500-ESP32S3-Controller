// G5500 Fork; GPL-3.0.
#pragma once
#include "wifi_access.hpp"
#include "tcp_memory.hpp"
#include "ethernet.hpp"
#include <cstring>
#include <algorithm>
#include <atomic>
#if defined(ARDUINO) && defined(ARDUINO_ARCH_ESP32) && !CONFIG_FREERTOS_UNICORE && (FORK_CLASSIC_ESP32 || FORK_S3)
#include <freertos/FreeRTOS.h>
#define FORK_BACKGROUND_NETWORK_SNAPSHOT 1
#else
#define FORK_BACKGROUND_NETWORK_SNAPSHOT 0
#endif
namespace forkrot {
struct NetworkSnapshot {
  uint32_t staIp=0,apIp=0;
  int rssi=0;
  bool connected=false,ap=false,tcpAvailable=false;
  uint16_t tcpActive=0,tcpClosing=0,tcpTimeWait=0;
  char ssid[33]{};
  TcpMemory tcp;
  uint16_t mqttPort=0;
  uint32_t capturedAt=0,ethIp=0;
  int wifiStatus=0,wifiMode=0;
  unsigned apClients=0;
  bool ethLink=false;
};
inline NetworkSnapshot readNetworkSnapshot(uint16_t mqttPort=0) {
  NetworkSnapshot result;result.capturedAt=millis();result.mqttPort=mqttPort;
  result.wifiStatus=radioWifi.status();result.wifiMode=radioWifi.getMode();
  result.connected=result.wifiStatus==WL_CONNECTED;
  result.ap=(result.wifiMode&WIFI_AP)!=0;
  if(result.connected) {
    result.staIp=uint32_t(radioWifi.localIP());result.rssi=radioWifi.RSSI();
    const auto ssid=radioWifi.SSID();
    std::memcpy(result.ssid,ssid.c_str(),std::min(size_t(ssid.length()),sizeof result.ssid-1));
  }
  if(result.ap) {result.apIp=uint32_t(radioWifi.softAPIP());result.apClients=radioWifi.softAPgetStationNum();}
  result.ethLink=ethernetLinkUp();result.ethIp=uint32_t(ethernetIP());
  result.tcp=tcpMemory(mqttPort);const auto &tcp=result.tcp;result.tcpAvailable=tcp.available;
  result.tcpActive=tcp.active;result.tcpClosing=tcp.closing;result.tcpTimeWait=tcp.timewait;
  return result;
}
#if FORK_BACKGROUND_NETWORK_SNAPSHOT
inline portMUX_TYPE networkSnapshotLock=portMUX_INITIALIZER_UNLOCKED;
inline NetworkSnapshot publishedNetworkSnapshot;
inline bool networkSnapshotReady=false;
inline std::atomic<bool> networkSnapshotBackground{false};
inline std::atomic<uint16_t> networkSnapshotMqttPort{0};
inline void setNetworkSnapshotBackground(bool enabled) {networkSnapshotBackground.store(enabled);}
inline void publishNetworkSnapshot() {
  // Radio queries and the lwIP lock can wait. Only the CPU0 I/O task calls
  // them; the control owner copies the small finished POD under a short lock.
  const auto next=readNetworkSnapshot(networkSnapshotMqttPort.load());
  portENTER_CRITICAL(&networkSnapshotLock);
  publishedNetworkSnapshot=next;networkSnapshotReady=true;
  portEXIT_CRITICAL(&networkSnapshotLock);
}
inline NetworkSnapshot networkStatusSnapshot() {
  portENTER_CRITICAL(&networkSnapshotLock);
  const bool ready=networkSnapshotReady;
  const auto result=publishedNetworkSnapshot;
  portEXIT_CRITICAL(&networkSnapshotLock);
  // Once the worker starts, even the first pending snapshot must not make
  // the control owner wait for Wi-Fi or lwIP's core mutex.
  return ready || networkSnapshotBackground.load()?result:readNetworkSnapshot();
}
inline TcpMemory networkTcpSnapshot(uint16_t mqttPort=0) {
  if(!networkSnapshotBackground.load())return tcpMemory(mqttPort);
  if(mqttPort)networkSnapshotMqttPort.store(mqttPort);
  const auto snapshot=networkStatusSnapshot();
  auto result=snapshot.tcp;
  if(mqttPort && mqttPort!=snapshot.mqttPort)result.available=false;
  return result;
}
inline uint32_t networkSnapshotAgeMs() {
  portENTER_CRITICAL(&networkSnapshotLock);
  const bool ready=networkSnapshotReady;const auto at=publishedNetworkSnapshot.capturedAt;
  portEXIT_CRITICAL(&networkSnapshotLock);
  return ready?uint32_t(millis()-at):UINT32_MAX;
}
#else
inline void setNetworkSnapshotBackground(bool) {}
inline TcpMemory networkTcpSnapshot(uint16_t mqttPort=0) {return tcpMemory(mqttPort);}
inline uint32_t networkSnapshotAgeMs() {return 0;}
inline NetworkSnapshot networkStatusSnapshot() {return readNetworkSnapshot();}
#endif
}
