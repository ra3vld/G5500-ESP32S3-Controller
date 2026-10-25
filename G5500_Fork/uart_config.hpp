// Physical UART settings are independent of the selected protocol. GPL-3.0.
#pragma once
#include <cstdint>
#include <initializer_list>
namespace forkrot {
struct UartConfig {
  uint32_t baud = 9600;
  uint8_t parity = 0, stopBits = 1; // 0: none, 1: even, 2: odd; always 8 data bits.
  static bool baudAllowed(uint32_t value) {
    for(auto rate:{1200u,2400u,4800u,9600u,19200u,38400u,57600u,115200u})
      if(rate==value)return true;
    return false;
  }
  bool valid() const {return baudAllowed(baud)&&parity<=2&&(stopBits==1||stopBits==2);}
  bool operator==(const UartConfig &b) const {return baud==b.baud&&parity==b.parity&&stopBits==b.stopBits;}
  unsigned frameBits() const {return 9+unsigned(parity!=0)+stopBits;}
  uint32_t wireUs(unsigned bytes) const {return uint32_t((uint64_t(bytes)*frameBits()*1000000+baud-1)/baud);}
};
}
