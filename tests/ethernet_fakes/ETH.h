#pragma once
#include <WiFi.h>
#include <array>
inline constexpr int ETH_PHY_LAN8720=1,ETH_CLOCK_GPIO0_IN=2;
inline constexpr int ETH_PHY_W5500=3,SPI2_HOST=2;
struct FakeEthernet {
  bool success=true,link=false,ip=false;
  std::array<int,6> arguments{};
  std::array<int,10> spiArguments{};
  int routePriority=0;
  bool begin(int type,int address,int mdc,int mdio,int power,int clock) {
    arguments={type,address,mdc,mdio,power,clock};return success;
  }
  bool begin(int type,int address,int cs,int irq,int rst,int host,int sck,int miso,int mosi,int frequency) {
    spiArguments={type,address,cs,irq,rst,host,sck,miso,mosi,frequency};return success;
  }
  std::array<uint32_t,5> staticIp{};
  bool config(IPAddress a,IPAddress g,IPAddress m,IPAddress d1,IPAddress d2){staticIp={uint32_t(a),uint32_t(g),uint32_t(m),uint32_t(d1),uint32_t(d2)};return success;}
  int setRoutePrio(int priority) {routePriority=priority;return 0;}
  bool linkUp() const {return link;}
  bool hasIP() const {return ip;}
  IPAddress localIP() const {return IPAddress(0x0a01000a);}
};
inline FakeEthernet ETH;
