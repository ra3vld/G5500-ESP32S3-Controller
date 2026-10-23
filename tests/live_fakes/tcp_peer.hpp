#pragma once
#include <set>
#include <cstddef>
#include <functional>
inline std::set<int> fakeTcpPending;
inline size_t fakeTcpCapacity=size_t(-1);
inline std::function<void()> fakeTcpCapacityHook;
namespace forkrot {
inline size_t tcpPeerFrameCapacity(int fd) {
 if(fakeTcpCapacityHook){auto hook=std::move(fakeTcpCapacityHook);fakeTcpCapacityHook={};hook();}
 return fakeTcpPending.count(fd)?0:fakeTcpCapacity;
}
}
