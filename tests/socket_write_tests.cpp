#include "bounded_socket_write.hpp"
#include "live_frame.hpp"
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
  for(const size_t length : {0u,125u,126u,3072u}) {
    forkrot::LiveFrame<3072> frame;const std::string body(length,'j');
    std::copy(body.begin(),body.end(),frame.payload());assert(frame.begin(length,1,UINT32_MAX-30));
    assert(!frame.begin(length,1,0));std::string wire;uint32_t time=UINT32_MAX-30;unsigned calls=0;
    auto step=decltype(frame)::Step::Pending;
    while(step==decltype(frame)::Step::Pending){
      step=frame.pump(time++,[&](const char *p,size_t n){
        if(++calls%3==0){errno=EAGAIN;return -1;}
        const auto size=std::min(n,size_t(11));wire.append(p,size);return int(size);
      });
    }
    assert(step==decltype(frame)::Step::Complete);
    const size_t header=length<=125?2:4;assert(wire.size()==length+header);
    assert(static_cast<unsigned char>(wire[0])==0x81);
    assert(static_cast<unsigned char>(wire[1])==(length<=125?length:126));
    if(header==4)assert(size_t((static_cast<unsigned char>(wire[2])<<8)|static_cast<unsigned char>(wire[3]))==length);
    assert(wire.substr(header)==body);frame.clear();assert(!frame.active());
  }
  forkrot::LiveFrame<10> stalled;assert(stalled.begin(10,1,0));
  assert(stalled.pump(0,[](const char *,size_t){return 3;})==decltype(stalled)::Step::Pending);
  assert(stalled.pump(200,[](const char *,size_t){errno=EAGAIN;return -1;})==decltype(stalled)::Step::Pending);
  assert(stalled.written()==3);
  assert(stalled.pump(4999,[](const char *,size_t){return 1;})==decltype(stalled)::Step::Pending);
  assert(stalled.pump(5000,[](const char *,size_t){assert(false);return -1;})==decltype(stalled)::Step::Failed&&errno==ETIMEDOUT);
  stalled.clear();assert(stalled.begin(0,10,0));
  assert(stalled.pump(0,[](const char *,size_t){return 0;})==decltype(stalled)::Step::Failed&&errno==ECONNRESET);
  assert(stalled.pump(1,[](const char *,size_t){errno=EPIPE;return -1;})==decltype(stalled)::Step::Failed&&errno==EPIPE);
  std::cout<<"PASS: resumable WebSocket headers/payloads, short writes, backpressure, timer wrap, no duplication and frame deadline\n";
  std::cout<<"PASS: short writes, transient errors, offset preservation, wraparound and total deadline\n";
}
