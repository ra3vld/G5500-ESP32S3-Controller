#pragma once
#include <lwip/sockets.h>
namespace forkrot {
inline bool abortTcpSocket(int fd) {
 linger reset{1,0};return ::setsockopt(fd,SOL_SOCKET,SO_LINGER,&reset,sizeof reset)==0;
}
}
