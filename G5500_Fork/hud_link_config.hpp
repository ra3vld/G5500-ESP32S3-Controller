// UART settings shared by direct cables and transparent radio bridges. GPL-3.0.
#pragma once
#include <cstdint>
#include "uart_config.hpp"
namespace forkrot {
struct HudLinkConfig {
  uint32_t baud = 115200;
  uint8_t frameBits = 10; // Derived from the selected physical port, never stored here.
  uint16_t intervalMs = 100;
  bool readyHigh = true;
  static bool baudAllowed(uint32_t value) {
    return UartConfig::baudAllowed(value);
  }
  bool valid() const {
    return baudAllowed(baud) && intervalMs >= 100 && intervalMs <= 10000;
  }
  bool paced() const { return baud!=115200 || frameBits!=10 || intervalMs!=100; }
  uint32_t responseMs() const {
    const uint32_t serialMs=(1560000u*frameBits+baud-1)/baud;
    return serialMs+500>1000?serialMs+500:1000;
  }
};
}
