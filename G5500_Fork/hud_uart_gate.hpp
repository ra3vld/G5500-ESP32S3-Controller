// Nonblocking UART flow control for AUX/ready or active-LOW BUSY. GPL-3.0.
#pragma once
#include "hud_link_config.hpp"
#include <algorithm>
#include <cstddef>
namespace forkrot {
class HudUartGate {
  uint32_t wroteAt_ = 0, settleMs_ = 0;
public:
  template<class Tx, class Write>
  void service(uint32_t now, const HudLinkConfig &config, bool flowControl, bool ready,
               size_t available, Tx &tx, Write write) {
    if (!ready || (flowControl && settleMs_ && uint32_t(now-wroteAt_)<settleMs_)) return;
    tx.drain(flowControl?std::min<size_t>(available,32):available,
        [&](const uint8_t *data,size_t n) {
          const size_t written=write(data,n);
          if (flowControl && written) {
            wroteAt_=now;
            // Let the chunk leave UART and the radio update AUX before feeding more.
            settleMs_=uint32_t((written*10000+config.baud-1)/config.baud)+2;
          }
          return written;
        });
  }
};
}
