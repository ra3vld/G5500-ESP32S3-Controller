// Bounded, allocation-free GS-232 replies on a full-duplex UART. GPL-3.0.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
namespace forkrot {
class UartReply {
  char bytes_[256]{};
  uint16_t read_=0,used_=0;
public:
  bool empty() const { return !used_; }
  void clear() { read_=used_=0; }
  // Reject the complete reply on overload: never leave a truncated line.
  bool push(std::string_view text) {
    if(text.size()>sizeof bytes_-used_)return false;
    const size_t end=(read_+used_)%sizeof bytes_;
    const size_t first=std::min(text.size(),sizeof bytes_-end);
    if(first)std::memcpy(bytes_+end,text.data(),first);
    if(text.size()>first)std::memcpy(bytes_,text.data()+first,text.size()-first);
    used_+=text.size();return true;
  }
  template<class Write> void service(size_t available,Write write) {
    const size_t count=std::min({size_t(used_),available,sizeof bytes_-read_});
    if(!count)return;
    const size_t sent=std::min(count,write(reinterpret_cast<const uint8_t *>(bytes_+read_),count));
    read_=(read_+sent)%sizeof bytes_;used_-=sent;
  }
};
}
