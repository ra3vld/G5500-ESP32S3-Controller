#pragma once
#include <lwip/tcpip.h>
#include <lwip/sockets.h>
inline constexpr unsigned TCP_SND_QUEUELEN=8;
struct tcp_seg { tcp_seg *next=nullptr;uint16_t len=0; };
struct tcp_pcb {
  tcp_pcb *next=nullptr;
  ip_addr_t local_ip{},remote_ip{};
  uint16_t local_port=0,remote_port=0;
  uint16_t snd_queuelen=0,mss=1436;
  uint32_t snd_buf=5744,queuedBytes=0;
  int fd=-1;
  tcp_pcb **netconn=nullptr;
  bool aborted=false;
  tcp_seg *unsent=nullptr,*unacked=nullptr;
};
inline tcp_pcb *tcp_active_pcbs=nullptr;
inline unsigned fakeRawAborts=0;
inline void tcp_abort(tcp_pcb *pcb) {
  assert(fakeCoreLocked && !pcb->aborted);
  auto **link=&tcp_active_pcbs;
  while(*link && *link!=pcb)link=&(*link)->next;
  assert(*link==pcb);*link=pcb->next;
  pcb->queuedBytes=0;pcb->aborted=true;++fakeRawAborts;
  if(pcb->netconn)*pcb->netconn=nullptr;
  assert(fakeNativeReset(pcb->fd)==0);
}
