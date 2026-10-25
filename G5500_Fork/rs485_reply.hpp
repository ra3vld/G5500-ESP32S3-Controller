// Wait for the PC to release its half-duplex line, including a trailing LF.
// Bounded staging, no waits in the control loop. GPL-3.0.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "uart_config.hpp"
namespace forkrot {
class Rs485Reply {
  uint8_t bytes_[2048]{};
  size_t read_=0,used_=0;
  uint32_t receivedAt_=0,quietMs_=3;
public:
  void configure(const UartConfig &line) {quietMs_=std::max<uint32_t>(3,(line.wireUs(2)+999)/1000);}
  void received(uint32_t now) { receivedAt_=now; }
  size_t write(const uint8_t *data,size_t size) {
    const size_t n=std::min(size,sizeof bytes_-used_);
    for(size_t i=0;i<n;++i) bytes_[(read_+used_+i)%sizeof bytes_]=data[i];
    used_+=n;return n;
  }
  template<class Write> void service(uint32_t now,size_t available,Write write) {
    if(uint32_t(now-receivedAt_)<quietMs_ || !used_) return;
    const size_t n=std::min({used_,available,sizeof bytes_-read_});
    const size_t sent=write(bytes_+read_,n);
    read_=(read_+sent)%sizeof bytes_;used_-=sent;
  }
};
}
