// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <lwip/tcpip.h>
#include <lwip/priv/tcp_priv.h>
#include <cstdint>
namespace forkrot {
struct TcpMemory {
  uint16_t active = 0, closing = 0, timewait = 0;
  uint16_t pcbBytes = sizeof(tcp_pcb);
  bool available = false;
  uint32_t liveUnsent=0, liveUnacked=0, liveBuffer=0, liveWindow=0, liveCwnd=0;
  uint16_t livePeers=0, liveRetries=0;
  uint32_t httpUnsent=0,httpUnacked=0,httpBuffer=0,httpWindow=0,httpCwnd=0;
  uint16_t httpPeers=0,httpRetries=0,httpMss=0;
  uint32_t queued=0,closingQueued=0,mqttQueued=0;
  uint16_t mqttPeers=0,mqttClosing=0,mqttMss=0;
};
// Read-only diagnostic. The lists belong to lwIP and must never be traversed
// without its core lock. Cores built without that lock report unavailable.
inline TcpMemory tcpMemory(uint16_t mqttPort=0) {
  TcpMemory result;
#if LWIP_TCPIP_CORE_LOCKING
  if (!sys_mutex_valid(&lock_tcpip_core)) return result;
  LOCK_TCPIP_CORE();
  for (const auto *pcb = tcp_active_pcbs; pcb; pcb = pcb->next) {
    ++result.active;
    if (pcb->state >= FIN_WAIT_1) ++result.closing;
    uint32_t unsent=0,unacked=0;
    for(const auto *s=pcb->unsent;s;s=s->next)unsent+=s->len;
    for(const auto *s=pcb->unacked;s;s=s->next)unacked+=s->len;
    result.queued+=unsent+unacked;
    if(pcb->state>=FIN_WAIT_1)result.closingQueued+=unsent+unacked;
    if(mqttPort && pcb->remote_port==mqttPort) {
      if(!result.mqttPeers || pcb->mss<result.mqttMss)result.mqttMss=pcb->mss;
      ++result.mqttPeers;result.mqttQueued+=unsent+unacked;
      if(pcb->state>=FIN_WAIT_1)++result.mqttClosing;
    }
    if(pcb->local_port==80) {
      if(!result.httpPeers || pcb->snd_buf<result.httpBuffer)result.httpBuffer=pcb->snd_buf;
      if(!result.httpPeers || pcb->snd_wnd<result.httpWindow)result.httpWindow=pcb->snd_wnd;
      if(!result.httpPeers || pcb->cwnd<result.httpCwnd)result.httpCwnd=pcb->cwnd;
      if(!result.httpPeers || pcb->mss<result.httpMss)result.httpMss=pcb->mss;
      ++result.httpPeers;
      if(pcb->nrtx>result.httpRetries)result.httpRetries=pcb->nrtx;
      result.httpUnsent+=unsent;result.httpUnacked+=unacked;
    }
    if(pcb->local_port==81) {
      if(!result.livePeers || pcb->snd_buf<result.liveBuffer)result.liveBuffer=pcb->snd_buf;
      if(!result.livePeers || pcb->snd_wnd<result.liveWindow)result.liveWindow=pcb->snd_wnd;
      if(!result.livePeers || pcb->cwnd<result.liveCwnd)result.liveCwnd=pcb->cwnd;
      ++result.livePeers;
      if(pcb->nrtx>result.liveRetries)result.liveRetries=pcb->nrtx;
      result.liveUnsent+=unsent;result.liveUnacked+=unacked;
    }
  }
  for (const auto *pcb = tcp_tw_pcbs; pcb; pcb = pcb->next) ++result.timewait;
  UNLOCK_TCPIP_CORE();
  result.available = true;
#endif
  (void)mqttPort;
  return result;
}
} // namespace forkrot
