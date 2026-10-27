// No Wi-Fi driver references in wired/serial-only firmware. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include <WiFi.h>
namespace forkrot {
#if WIFI_SUPPORT
inline auto &radioWifi=::WiFi;
#else
struct DisabledWifi {
  int status() const { return 0; }
  int getMode() const { return 0; }
  int RSSI() const { return 0; }
  unsigned softAPgetStationNum() const { return 0; }
  IPAddress localIP() const { return {}; }
  IPAddress softAPIP() const { return {}; }
  IPAddress subnetMask() const { return {}; }
  String SSID() const { return {}; }
};
inline DisabledWifi radioWifi;
#endif
}
