// G5500 Fork; GPL-3.0.
#pragma once
#include "network_timing.hpp"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
namespace forkrot {
// One complete unmasked server frame. A pump performs only one nonblocking
// write; offset survives short writes and backpressure between HTTPD turns.
template<size_t Capacity> struct LiveFrame {
  static_assert(Capacity < 65536, "16-bit WebSocket length");
  std::array<char, Capacity + 4> bytes{};
  size_t first=0, offset=0, size=0;
  uint32_t started=0, retries=0;
  int retryError=0;
  enum class Step {Pending, Complete, Failed};
  bool active() const {return size!=0;}
  size_t written() const {return offset-first;}
  size_t requested() const {return size-first;}
  char *payload() {return bytes.data()+4;}
  bool begin(size_t n, uint8_t opcode, uint32_t now) {
    if(active()||n>Capacity)return false;
    first=offset=n<=125?2:0;size=n+4;started=now;retries=0;retryError=0;
    bytes[first]=char(0x80|opcode);
    if(n<=125)bytes[3]=char(n);
    else {bytes[1]=126;bytes[2]=char(n>>8);bytes[3]=char(n);}
    return true;
  }
  template<class Send> Step pump(uint32_t now, Send send) {
    if(!active())return Step::Complete;
    if(uint32_t(now-started)>=LiveFrameTimeoutMs){errno=ETIMEDOUT;return Step::Failed;}
    const int n=send(bytes.data()+offset,size-offset);
    if(n>0) {
      if(size_t(n)>size-offset){errno=EIO;return Step::Failed;}
      offset+=size_t(n);
      return offset==size?Step::Complete:Step::Pending;
    }
    if(!n){errno=ECONNRESET;return Step::Failed;}
    retryError=errno;++retries;
    if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR||errno==ENOBUFS||errno==ENOMEM)return Step::Pending;
    return Step::Failed;
  }
  void clear() {first=offset=size=0;}
};
}
