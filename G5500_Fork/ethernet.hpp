// Wired network adapter; other board profiles do not link the Ethernet driver. GPL-3.0.
#pragma once
#include "board.hpp"
#include <WiFi.h>
#if defined(ARDUINO_WT32_ETH01) || SELECTED_BOARD == BOARD_WT32_ETH01
// The Arduino variant flag also lets PlatformIO discover this built-in library.
#include <ETH.h>
#endif
namespace forkrot {
inline constexpr bool ETHERNET_SUPPORTED = SELECTED_BOARD == BOARD_WT32_ETH01;
inline bool ethernetLinkUp() {
#if SELECTED_BOARD == BOARD_WT32_ETH01
  return ETH.linkUp();
#else
  return false;
#endif
}
inline bool ethernetConnected() {
#if SELECTED_BOARD == BOARD_WT32_ETH01
  return ETH.linkUp() && ETH.hasIP();
#else
  return false;
#endif
}
inline IPAddress ethernetIP() {
#if SELECTED_BOARD == BOARD_WT32_ETH01
  if(ethernetConnected()) return ETH.localIP();
#endif
  return IPAddress();
}
class EthernetAdapter {
public:
  bool begin() {
#if SELECTED_BOARD == BOARD_WT32_ETH01
    started_ = ETH.begin(ETH_PHY_LAN8720, board::ETH_ADDRESS, board::ETH_MDC,
                         board::ETH_MDIO, board::ETH_POWER, ETH_CLOCK_GPIO0_IN);
    // Prefer the cable without pinning a dead default interface after link loss.
    if(started_) ETH.setRoutePrio(150);
#endif
    return started_;
  }
  bool started() const { return started_; }
private:
  bool started_=false;
};
} // namespace forkrot
