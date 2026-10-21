#include "bounded_socket_write.hpp"
#include <cassert>
#include <iostream>
#include <string>
using forkrot::boundedSocketWrite;
int main() {
  const std::string input="a WebSocket header and payload must never be repeated";
  for(const int retry : {EAGAIN,EINTR,ENOBUFS,ENOMEM}) {
    uint32_t clock=UINT32_MAX-10;unsigned calls=0;std::string wire;
    const int result=boundedSocketWrite(input.data(),input.size(),100,
      [&](const char *p,size_t n) {
        if(++calls%3) {errno=retry;return -1;}
        const auto take=std::min(n,size_t(5));wire.append(p,take);return int(take);
      },[&]{return clock;},[&]{++clock;});
    assert(result==int(input.size()) && wire==input);
  }
  // A progressing peer cannot renew the deadline indefinitely.
  uint32_t clock=0;unsigned written=0;
  assert(boundedSocketWrite(input.data(),input.size(),10,
    [&](const char *,size_t){++written;clock+=3;return 1;},
    [&]{return clock;},[&]{++clock;})==-1);
  assert(errno==ETIMEDOUT && written==4 && clock==12);
  clock=0;
  assert(boundedSocketWrite(input.data(),input.size(),100,
    [](const char *,size_t){errno=EAGAIN;return -1;},
    [&]{return clock;},[&]{++clock;})==-1);
  assert(errno==ETIMEDOUT && clock==100);
  for(const int error : {EPIPE,ECONNRESET,EBADF}) {
    clock=0;
    assert(boundedSocketWrite(input.data(),input.size(),100,
      [=](const char *,size_t){errno=error;return -1;},
      [&]{return clock;},[&]{++clock;})==-1);
    assert(errno==error && clock==0);
  }
  assert(boundedSocketWrite(input.data(),input.size(),100,
    [](const char *,size_t){return 0;},[]{return 0u;},[]{assert(false);})==-1);
  assert(errno==ECONNRESET);
  assert(boundedSocketWrite(nullptr,0,100,
    [](const char *,size_t){assert(false);return -1;},[]{return 0u;},[]{assert(false);})==0);
  std::cout<<"PASS: short writes, transient errors, offset preservation, wraparound and total deadline\n";
}
