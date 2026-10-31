// G5500 Fork; GPL-3.0.
#pragma once
#include "wifi_access.hpp"
#include "tcp_memory.hpp"
#include <cstring>
#include <algorithm>
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
};
inline NetworkSnapshot readNetworkSnapshot() {
  NetworkSnapshot result;
  result.connected=radioWifi.status()==WL_CONNECTED;
  result.ap=(radioWifi.getMode()&WIFI_AP)!=0;
  if(result.connected) {
    result.staIp=uint32_t(radioWifi.localIP());result.rssi=radioWifi.RSSI();
    const auto ssid=radioWifi.SSID();
    std::memcpy(result.ssid,ssid.c_str(),std::min(size_t(ssid.length()),sizeof result.ssid-1));
  }
  if(result.ap)result.apIp=uint32_t(radioWifi.softAPIP());
  const auto tcp=tcpMemory();result.tcpAvailable=tcp.available;
  result.tcpActive=tcp.active;result.tcpClosing=tcp.closing;result.tcpTimeWait=tcp.timewait;
  return result;
}
#if FORK_BACKGROUND_NETWORK_SNAPSHOT
inline portMUX_TYPE networkSnapshotLock=portMUX_INITIALIZER_UNLOCKED;
inline NetworkSnapshot publishedNetworkSnapshot;
inline bool networkSnapshotReady=false;
inline void publishNetworkSnapshot() {
  // Radio queries and the lwIP lock can wait. Only the CPU0 I/O task calls
  // them; the control owner copies the small finished POD under a short lock.
  const auto next=readNetworkSnapshot();
  portENTER_CRITICAL(&networkSnapshotLock);
  publishedNetworkSnapshot=next;networkSnapshotReady=true;
  portEXIT_CRITICAL(&networkSnapshotLock);
}
inline NetworkSnapshot networkStatusSnapshot() {
  portENTER_CRITICAL(&networkSnapshotLock);
  const bool ready=networkSnapshotReady;
  const auto result=publishedNetworkSnapshot;
  portEXIT_CRITICAL(&networkSnapshotLock);
  return ready?result:readNetworkSnapshot();
}
#else
inline NetworkSnapshot networkStatusSnapshot() {return readNetworkSnapshot();}
#endif
}
