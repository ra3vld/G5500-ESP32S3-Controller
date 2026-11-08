// Bounded TCP replies; one owner, no allocations or waits. GPL-3.0.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
namespace forkrot {
template<size_t Capacity> class TcpReply {
  char bytes_[Capacity]{};
  size_t read_=0,used_=0;
  uint32_t started_=0,progress_=0;
public:
  uint32_t retries=0,timeouts=0,maxPendingMs=0;
  bool empty() const {return !used_;}
  size_t size() const {return used_;}
  size_t available() const {return Capacity-used_;}
  void clear(){read_=used_=0;}
  bool push(std::string_view text,uint32_t now) {
    if(text.size()>Capacity-used_)return false;
    if(!used_)started_=progress_=now;
    const auto end=(read_+used_)%Capacity,first=std::min(text.size(),Capacity-end);
    if(first)std::memcpy(bytes_+end,text.data(),first);
    if(text.size()>first)std::memcpy(bytes_,text.data()+first,text.size()-first);
    used_+=text.size();return true;
  }
  // Send returns -2 for temporary backpressure and -1 for a fatal error.
  template<class Send> bool service(uint32_t now,Send send) {
    if(empty())return true;
    if(uint32_t(now-progress_)>=5000){++timeouts;return false;}
    const auto count=std::min({used_,Capacity-read_,size_t(128)});
    const int n=send(bytes_+read_,count);
    if(n==-1 || n< -2 || n>int(count))return false;
    if(n<=0){++retries;return true;}
    read_=(read_+size_t(n))%Capacity;used_-=size_t(n);
    progress_=now;
    maxPendingMs=std::max(maxPendingMs,uint32_t(now-started_));
    return true;
  }
};
}
