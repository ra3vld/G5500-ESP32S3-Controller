#pragma once
#include <lwip/sockets.h>
namespace forkrot {
inline size_t fakeTcpPendingBytes=0;
inline bool limitTcpSocketMss(int,uint16_t) { return true; }
inline size_t tcpPeerPendingBytes(int) { return fakeTcpPendingBytes; }
inline size_t tcpPeerFrameCapacity(int) { return size_t(-1); }
inline bool abortTcpSocket(int fd) {
 linger reset{1,0};return ::setsockopt(fd,SOL_SOCKET,SO_LINGER,&reset,sizeof reset)==0;
}
}
