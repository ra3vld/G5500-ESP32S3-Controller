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
                                   const char *separator = "\n") {
  const bool client = usableNetworkAddress(sta), accessPoint = usableNetworkAddress(ap);
  std::snprintf(text, capacity, "%s%s%s%s%s", client ? "STA: " : "", client ? sta : "",
                client && accessPoint ? separator : "", accessPoint ? "AP: " : "",
                accessPoint ? ap : client ? "" : "IP: -");
}
} // namespace forkrot
