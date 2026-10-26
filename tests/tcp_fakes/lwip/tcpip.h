#pragma once
#include <cstdint>
#include <cstring>
#include <cassert>
#include <arpa/inet.h>
#ifndef LWIP_TCPIP_CORE_LOCKING
#define LWIP_TCPIP_CORE_LOCKING 1
#endif
#ifndef LWIP_IPV6
#define LWIP_IPV6 1
#endif
using err_t=int;
inline constexpr err_t ERR_OK=0,ERR_MEM=-1;
struct ip6_addr_t {uint32_t addr[4]{};uint8_t zone=0;};
struct ip_addr_t {ip6_addr_t v6{};uint8_t type=4;};
inline void ip_addr_set_ip4_u32(ip_addr_t *out,uint32_t address){*out={};out->v6.addr[0]=address;}
inline bool ip6_addr_isipv4mappedipv6(const ip6_addr_t *ip){return !ip->addr[0]&&!ip->addr[1]&&ip->addr[2]==htonl(0xffff);}
inline void ip6_addr_set_zone(ip6_addr_t *ip,uint32_t zone){ip->zone=uint8_t(zone);}
#define ip_addr_copy_from_ip6(out,in) do{(out).v6=(in);(out).type=6;}while(0)
inline bool ip_addr_cmp(const ip_addr_t *a,const ip_addr_t *b){return a->type==b->type&&a->v6.zone==b->v6.zone&&std::memcmp(a->v6.addr,b->v6.addr,sizeof a->v6.addr)==0;}
inline bool fakeCoreLocked=false;
inline int lock_tcpip_core=1;
inline bool sys_mutex_valid(const int *lock){return *lock!=0;}
inline void fakeLockCore(){assert(!fakeCoreLocked);fakeCoreLocked=true;}
inline void fakeUnlockCore(){assert(fakeCoreLocked);fakeCoreLocked=false;}
#define LOCK_TCPIP_CORE() fakeLockCore()
#define UNLOCK_TCPIP_CORE() fakeUnlockCore()
