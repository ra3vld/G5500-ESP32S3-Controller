#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
namespace forkrot {
struct TcpPeerEndpoint {bool valid=false;};
inline TcpPeerEndpoint tcpPeerEndpoint(int){return {};}
inline bool abortTcpPeerEndpoint(const TcpPeerEndpoint &){return false;}
inline bool limitTcpSocketMss(int,uint16_t){return true;}
}
