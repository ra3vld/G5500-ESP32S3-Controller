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
};
// Read-only diagnostic. The lists belong to lwIP and must never be traversed
// without its core lock. Cores built without that lock report unavailable.
inline TcpMemory tcpMemory() {
  TcpMemory result;
#if LWIP_TCPIP_CORE_LOCKING
  if (!sys_mutex_valid(&lock_tcpip_core)) return result;
  LOCK_TCPIP_CORE();
  for (const auto *pcb = tcp_active_pcbs; pcb; pcb = pcb->next) {
    ++result.active;
    if (pcb->state >= FIN_WAIT_1) ++result.closing;
  }
  for (const auto *pcb = tcp_tw_pcbs; pcb; pcb = pcb->next) ++result.timewait;
  UNLOCK_TCPIP_CORE();
  result.available = true;
#endif
  return result;
}
} // namespace forkrot
