#pragma once
#include <set>
#include <cstddef>
#include <functional>
#include <lwip/sockets.h>
inline std::set<int> fakeTcpPending;
inline size_t fakeTcpCapacity=size_t(-1);
inline std::function<void()> fakeTcpCapacityHook;
namespace forkrot {
inline bool limitTcpSocketMss(int,uint16_t) { return true; }
inline bool abortTcpSocket(int fd) {
 linger reset{1,0};return ::setsockopt(fd,SOL_SOCKET,SO_LINGER,&reset,sizeof reset)==0;
}
inline size_t tcpPeerFrameCapacity(int fd) {
 if(fakeTcpCapacityHook){auto hook=std::move(fakeTcpCapacityHook);fakeTcpCapacityHook={};hook();}
 return fakeTcpPending.count(fd)?0:fakeTcpCapacity;
}
}
