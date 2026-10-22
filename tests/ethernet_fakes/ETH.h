#pragma once
#include <WiFi.h>
#include <array>
inline constexpr int ETH_PHY_LAN8720=1,ETH_CLOCK_GPIO0_IN=2;
struct FakeEthernet {
  bool success=true,link=false,ip=false;
  std::array<int,6> arguments{};
  int routePriority=0;
  bool begin(int type,int address,int mdc,int mdio,int power,int clock) {
    arguments={type,address,mdc,mdio,power,clock};return success;
  }
  int setRoutePrio(int priority) {routePriority=priority;return 0;}
  bool linkUp() const {return link;}
  bool hasIP() const {return ip;}
  IPAddress localIP() const {return IPAddress(0x0a01000a);}
};
inline FakeEthernet ETH;
