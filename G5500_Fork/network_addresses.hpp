// G5500 Fork; GPL-3.0.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstring>
namespace forkrot {
inline bool usableNetworkAddress(const char *ip) {
  return ip && *ip && std::strcmp(ip, "-") && std::strcmp(ip, "0.0.0.0");
}
inline void formatNetworkAddresses(char *text, size_t capacity, const char *sta, const char *ap,
                                   const char *separator = "\n", const char *eth = nullptr,
                                   bool own = true, bool showAp = true) {
  if(!own){sta=nullptr;eth=nullptr;}
  if(!showAp)ap=nullptr;
  if(!own && !usableNetworkAddress(ap)){if(capacity)*text=0;return;}
  const bool wired=usableNetworkAddress(eth);
  if(wired) {
    const int length=std::snprintf(text,capacity,"ETH: %s",eth);
    if(length<0 || size_t(length)>=capacity) return;
    if(!usableNetworkAddress(sta) && !usableNetworkAddress(ap)) return;
    const int sep=std::snprintf(text+length,capacity-size_t(length),"%s",separator);
    if(sep<0 || size_t(sep)>=capacity-size_t(length)) return;
    text+=length+sep;capacity-=size_t(length+sep);
  }
  const bool client = usableNetworkAddress(sta), accessPoint = usableNetworkAddress(ap);
  std::snprintf(text, capacity, "%s%s%s%s%s", client ? "STA: " : "", client ? sta : "",
                client && accessPoint ? separator : "", accessPoint ? "AP: " : "",
                accessPoint ? ap : client ? "" : "IP: -");
}
} // namespace forkrot
