// Wired network adapter; other board profiles do not link the Ethernet driver. GPL-3.0.
#pragma once
#include "board.hpp"
#include "network_config.hpp"
#include <WiFi.h>
#if defined(ARDUINO_WT32_ETH01) || (ETHERNET_SUPPORT && (SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH))
// The Arduino variant flag also lets PlatformIO discover this built-in library.
#include <ETH.h>
#endif
namespace forkrot {
inline constexpr bool ETHERNET_SUPPORTED = (ETHERNET_SUPPORT && (SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH));
inline bool ethernetLinkUp() {
#if (ETHERNET_SUPPORT && (SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH))
  return ETH.linkUp();
#else
  return false;
#endif
}
inline bool ethernetConnected() {
#if (ETHERNET_SUPPORT && (SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH))
  return ETH.linkUp() && ETH.hasIP();
#else
  return false;
#endif
}
inline IPAddress ethernetIP() {
#if (ETHERNET_SUPPORT && (SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH))
  if(ethernetConnected()) return ETH.localIP();
#endif
  return IPAddress();
}
class EthernetAdapter {
public:
  bool begin(const IpConfig &config = {}) {
    (void)config;
#if (ETHERNET_SUPPORT && (SELECTED_BOARD == BOARD_WT32_ETH01 || SELECTED_BOARD == BOARD_WAVESHARE_S3_ETH))
    #if SELECTED_BOARD == BOARD_WT32_ETH01
    started_ = ETH.begin(ETH_PHY_LAN8720, board::ETH_ADDRESS, board::ETH_MDC,
                         board::ETH_MDIO, board::ETH_POWER, ETH_CLOCK_GPIO0_IN);
    #else
    started_ = ETH.begin(ETH_PHY_W5500, 1, board::ETH_CS, board::ETH_IRQ, board::ETH_RESET,
                         SPI2_HOST, board::ETH_SCK, board::ETH_MISO, board::ETH_MOSI, 20);
    #endif
    // Prefer the cable without pinning a dead default interface after link loss.
    if(started_) {
      ETH.setRoutePrio(150);
      const auto ip=[](uint32_t n){return IPAddress(uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n));};
      if(!config.automatic && !ETH.config(ip(config.address),ip(config.gateway),ip(config.mask),ip(config.dns1),ip(config.dns2)))return false;
    }
#endif
    return started_;
  }
  bool started() const { return started_; }
private:
  bool started_=false;
};
} // namespace forkrot
