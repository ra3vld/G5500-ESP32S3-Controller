#pragma once
#include "Arduino.h"
#include <cstdio>
inline constexpr int IPv4=4, WIFI_AP=2, WL_CONNECTED=3;
class IPAddress {
  uint32_t value_=0;
public:
  IPAddress()=default;
  explicit IPAddress(uint32_t value):value_(value) {}
  operator uint32_t() const {return value_;}
  int type() const {return IPv4;}
  bool fromString(const String &s) {
    unsigned a,b,c,d;char tail;
    if(std::sscanf(s.c_str(),"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail)!=4 || a>255 || b>255 || c>255 || d>255)return false;
    value_=a|(b<<8)|(c<<16)|(d<<24);return true;
  }
  String toString() const {return String("192.168.4.1");}
};
struct FakeWifi {
  int connectionStatus=0;
  String connectedSsid;
  int status() const {return connectionStatus;}
  String SSID() const {return connectedSsid;}
  int getMode() const {return WIFI_AP;}
  int RSSI() const {return -50;}
  IPAddress localIP() const {return IPAddress(1);}
  IPAddress softAPIP() const {return IPAddress(1);}
};
inline FakeWifi WiFi;
