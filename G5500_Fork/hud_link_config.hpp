// UART settings shared by direct cables and transparent radio bridges. GPL-3.0.
#pragma once
#include <cstdint>
#include <initializer_list>
namespace forkrot {
struct HudLinkConfig {
  uint32_t baud = 115200;
  uint16_t intervalMs = 100;
  bool readyHigh = true;
  static bool baudAllowed(uint32_t value) {
    for (auto rate : {1200u, 2400u, 4800u, 9600u, 19200u, 38400u, 57600u, 115200u})
      if (rate == value) return true;
    return false;
  }
  bool valid() const {
    return baudAllowed(baud) && intervalMs >= 100 && intervalMs <= 10000;
  }
  bool paced() const { return baud!=115200 || intervalMs!=100; }
  uint32_t responseMs() const {
    const uint32_t serialMs=(15600000u+baud-1)/baud;
    return serialMs+500>1000?serialMs+500:1000;
  }
};
}
