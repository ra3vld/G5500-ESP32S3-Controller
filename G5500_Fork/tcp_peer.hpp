// G5500 Fork; GPL-3.0.
#pragma once
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/priv/tcpip_priv.h>
#include <cstring>
#include <algorithm>
#include <cstddef>
namespace forkrot {
// Dual-stack listeners report IPv4 peers as IPv4-mapped IPv6 sockets, while
// lwIP stores their TCP PCB as IPv4. Normalize before comparing endpoints.
inline bool tcpSocketEndpoint(const sockaddr_storage &address,ip_addr_t &ip,uint16_t &port) {
  if(address.ss_family==AF_INET) {
    const auto &v4=reinterpret_cast<const sockaddr_in &>(address);
    ip_addr_set_ip4_u32(&ip,v4.sin_addr.s_addr);port=ntohs(v4.sin_port);return true;
  }
#if LWIP_IPV6
  if(address.ss_family==AF_INET6) {
    const auto &v6=reinterpret_cast<const sockaddr_in6 &>(address);
    ip6_addr_t parsed{};std::memcpy(parsed.addr,&v6.sin6_addr,sizeof parsed.addr);
    if(ip6_addr_isipv4mappedipv6(&parsed))ip_addr_set_ip4_u32(&ip,parsed.addr[3]);
    else {ip6_addr_set_zone(&parsed,v6.sin6_scope_id);ip_addr_copy_from_ip6(ip,parsed);}
    port=ntohs(v6.sin6_port);return true;
  }
#endif
  return false;
}
// tcpip_api_call runs under the TCP core lock (or on its thread when core
// locking is disabled). The request stays on the caller's stack until return.
struct TcpAbortRequest {
  tcpip_api_call_data call{};
  ip_addr_t peer{}, local{};
  uint16_t peerPort=0, localPort=0;
  bool aborted=false;
};
inline err_t abortTcpPeer(tcpip_api_call_data *call) {
  auto &request=*reinterpret_cast<TcpAbortRequest *>(call);
  for(auto *pcb=tcp_active_pcbs;pcb;pcb=pcb->next) {
    if(pcb->local_port==request.localPort && pcb->remote_port==request.peerPort &&
       ip_addr_cmp(&pcb->remote_ip,&request.peer) && ip_addr_cmp(&pcb->local_ip,&request.local)) {
      // tcp_abort also calls netconn's error callback, which clears its PCB
      // pointer before the socket owner closes the descriptor normally.
      tcp_abort(pcb);request.aborted=true;break;
    }
  }
  return ERR_OK;
}
// Call only for a session that has ended or a response already being aborted.
// Stock Arduino ESP32 libraries disable LWIP_SO_LINGER: merely setting that
// option silently failed to release queued telemetry on retired connections.
inline bool abortTcpSocket(int fd) {
  if(fd<0)return false;
  linger reset{1,0};
  if(::setsockopt(fd,SOL_SOCKET,SO_LINGER,&reset,sizeof reset)==0)return true;
  sockaddr_storage peer{},local{};socklen_t peerSize=sizeof peer,localSize=sizeof local;
  TcpAbortRequest request;
  if(getpeername(fd,reinterpret_cast<sockaddr *>(&peer),&peerSize)!=0 ||
     getsockname(fd,reinterpret_cast<sockaddr *>(&local),&localSize)!=0 ||
     !tcpSocketEndpoint(peer,request.peer,request.peerPort) ||
     !tcpSocketEndpoint(local,request.local,request.localPort))return false;
  return tcpip_api_call(abortTcpPeer,&request.call)==ERR_OK && request.aborted;
}
// send() accepting a frame only means lwIP copied it. Admit a new telemetry
// frame only when the entire frame fits. Waiting for every ACK also stalls a
// peer with ample free buffer and prevents subsequent data from eliciting a
// fresh cumulative ACK after packet loss. Coalesce before starting a frame
// that could otherwise be cut short by TCP byte or packet-queue limits.
// Socket addresses identify the exact peer; lwIP owns and protects the PCBs.
inline size_t tcpPeerFrameCapacity(int fd) {
#if LWIP_TCPIP_CORE_LOCKING
  sockaddr_storage peer{},local{};socklen_t peerSize=sizeof peer,localSize=sizeof local;
  ip_addr_t peerIp{},localIp{};uint16_t peerPort=0,localPort=0;
  if(getpeername(fd,reinterpret_cast<sockaddr *>(&peer),&peerSize)!=0 ||
     getsockname(fd,reinterpret_cast<sockaddr *>(&local),&localSize)!=0 ||
     !tcpSocketEndpoint(peer,peerIp,peerPort) || !tcpSocketEndpoint(local,localIp,localPort) ||
     !sys_mutex_valid(&lock_tcpip_core))return size_t(-1);
  size_t capacity=size_t(-1);
  LOCK_TCPIP_CORE();
  for(const auto *pcb=tcp_active_pcbs;pcb;pcb=pcb->next) {
    if(pcb->local_port==localPort && pcb->remote_port==peerPort &&
       ip_addr_cmp(&pcb->remote_ip,&peerIp) && ip_addr_cmp(&pcb->local_ip,&localIp)) {
      const size_t packets=pcb->snd_queuelen<TCP_SND_QUEUELEN?TCP_SND_QUEUELEN-pcb->snd_queuelen:0;
      capacity=std::min(size_t(pcb->snd_buf),packets*size_t(pcb->mss));break;
    }
  }
  UNLOCK_TCPIP_CORE();
  return capacity;
#else
  (void)fd;return size_t(-1);
#endif
}
} // namespace forkrot
