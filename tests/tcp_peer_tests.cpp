#include "tcp_peer.hpp"
#include <cassert>
#include <iostream>
#include <fcntl.h>
#include <cstdlib>
#include <new>
static unsigned allocations=0;
void *operator new(size_t size){++allocations;if(auto *p=std::malloc(size))return p;throw std::bad_alloc();}
void operator delete(void *p) noexcept{std::free(p);}
void operator delete(void *p,size_t) noexcept{std::free(p);}
struct Connection {
  int listener=-1,peer=-1,server=-1;
  Connection(){
    listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,reinterpret_cast<sockaddr *>(&local),sizeof local)==0);
    assert(listen(listener,1)==0);socklen_t size=sizeof local;
    assert(getsockname(listener,reinterpret_cast<sockaddr *>(&local),&size)==0);
    peer=socket(AF_INET,SOCK_STREAM,0);assert(peer>=0);
    assert(connect(peer,reinterpret_cast<sockaddr *>(&local),sizeof local)==0);
    server=accept(listener,nullptr,nullptr);assert(server>=0);
  }
  ~Connection(){if(server>=0)close(server);close(peer);close(listener);}
  void expectReset(){
    close(server);server=-1;
    char bytes[128];int received;
    do{received=recv(peer,bytes,sizeof bytes,0);}while(received>0);
    assert(received<0 && errno==ECONNRESET);
  }
};
static tcp_pcb descriptor(int fd){
  tcp_pcb pcb;pcb.fd=fd;pcb.queuedBytes=5744;
  sockaddr_storage local{},peer{};socklen_t n=sizeof local;
  assert(getsockname(fd,reinterpret_cast<sockaddr *>(&local),&n)==0);n=sizeof peer;
  assert(getpeername(fd,reinterpret_cast<sockaddr *>(&peer),&n)==0);
  assert(forkrot::tcpSocketEndpoint(local,pcb.local_ip,pcb.local_port));
  assert(forkrot::tcpSocketEndpoint(peer,pcb.remote_ip,pcb.remote_port));return pcb;
}
int main(){
  using namespace forkrot;
  Connection target,other;
  auto wanted=descriptor(target.server),innocent=descriptor(other.server);
  auto wrongPort=wanted,wrongRemote=wanted,wrongLocal=wanted;
  ++wrongPort.remote_port;++wrongRemote.remote_ip.v6.addr[0];++wrongLocal.local_ip.v6.addr[0];
  innocent.next=&wrongPort;wrongPort.next=&wrongRemote;wrongRemote.next=&wrongLocal;wrongLocal.next=&wanted;
  tcp_active_pcbs=&innocent;tcp_pcb *netconn=&wanted;wanted.netconn=&netconn;
  tcp_seg tail{nullptr,100},head{&tail,900},acked{nullptr,250};
  wanted.unsent=&head;wanted.unacked=&acked;
  assert(tcpPeerPendingBytes(target.server)==1250);
  head.len=200;assert(tcpPeerPendingBytes(target.server)==550);
  wanted.unsent=wanted.unacked=nullptr;assert(tcpPeerPendingBytes(target.server)==0);
#if LWIP_TCPIP_CORE_LOCKING
  wanted.snd_buf=1500;wanted.snd_queuelen=7;assert(tcpPeerFrameCapacity(target.server)==1436);
  assert(!fakeCoreLocked);
#else
  assert(tcpPeerFrameCapacity(target.server)==size_t(-1));
#endif
  assert(send(target.server,"stalled telemetry",17,0)==17);
  const auto before=allocations;
  assert(abortTcpSocket(target.server));
  assert(allocations==before && !fakeCoreLocked);
  assert(fakeLingerRequests==1 && fakeTcpApiCalls==4 && fakeRawAborts==1);
  assert(wanted.aborted && wanted.queuedBytes==0 && netconn==nullptr);
  assert(!innocent.aborted&&!wrongPort.aborted&&!wrongRemote.aborted&&!wrongLocal.aborted);
  target.expectReset();
  assert(send(other.server,"ok",2,0)==2);char alive[2];assert(recv(other.peer,alive,2,MSG_WAITALL)==2);
  // A vanished PCB or failed dispatch must never abort a different connection.
  assert(!abortTcpSocket(other.server+10000));
  tcp_active_pcbs=nullptr;assert(!abortTcpSocket(other.server));assert(fakeRawAborts==1);
  tcp_active_pcbs=&innocent;fakeTcpApiResult=ERR_MEM;
  assert(!abortTcpSocket(other.server)&&!innocent.aborted);fakeTcpApiResult=ERR_OK;
  // SDKs with SO_LINGER enabled retain the socket API path, without raw access.
  Connection supported;fakeLingerSupported=true;const auto calls=fakeTcpApiCalls;
  assert(abortTcpSocket(supported.server)&&fakeTcpApiCalls==calls);supported.expectReset();
  // Both IPv4-mapped IPv6 and scoped IPv6 compare as lwIP stores them.
#if LWIP_IPV6
  sockaddr_storage address{};auto &v6=reinterpret_cast<sockaddr_in6 &>(address);
  v6.sin6_family=AF_INET6;v6.sin6_port=htons(81);
  assert(inet_pton(AF_INET6,"::ffff:127.0.0.1",&v6.sin6_addr)==1);
  ip_addr_t parsed{},ipv4{};uint16_t port=0;ip_addr_set_ip4_u32(&ipv4,htonl(INADDR_LOOPBACK));
  assert(tcpSocketEndpoint(address,parsed,port)&&port==81&&ip_addr_cmp(&parsed,&ipv4));
  assert(inet_pton(AF_INET6,"fe80::1",&v6.sin6_addr)==1);v6.sin6_scope_id=3;
  assert(tcpSocketEndpoint(address,parsed,port)&&parsed.type==6&&parsed.v6.zone==3);
#endif
  assert(!abortTcpSocket(-1));
  std::cout<<"PASS: SO_LINGER-disabled TCP retirement frees only the matching PCB, clears netconn, resets the peer, preserves other clients and allocates no heap\n";
}
