// Persisted per-interface IPv4 settings, in network byte order. GPL-3.0.
#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <cstdio>
namespace forkrot {
inline bool parseIpv4(std::string_view s,uint32_t &value) {
  value=0;unsigned part=0,digits=0,parts=0;
  for(size_t i=0;i<=s.size();++i) {
    const char c=i<s.size()?s[i]:'.';
    if(c=='.') {if(!digits || parts==4)return false;value=(value<<8)|part;++parts;part=digits=0;}
    else {if(c<'0'||c>'9'||++digits>3)return false;part=part*10+unsigned(c-'0');if(part>255)return false;}
  }
  return parts==4;
}
inline void formatIpv4(uint32_t ip,char (&out)[16]) {
  std::snprintf(out,sizeof out,"%u.%u.%u.%u",unsigned(ip>>24),unsigned((ip>>16)&255),unsigned((ip>>8)&255),unsigned(ip&255));
}
struct IpConfig {
  bool automatic=true;
  uint32_t address=0,mask=0,gateway=0,dns1=0,dns2=0;
  const char *validate() const {
    if(automatic)return nullptr;
    const uint32_t hosts=~mask;
    if(!mask || hosts<3 || (hosts&(hosts+1)))return "invalid_netmask";
    const auto unicast=[](uint32_t a){return (a>>24)>0 && (a>>24)<224 && (a>>24)!=127;};
    if(!unicast(address) || !(address&hosts) || (address&hosts)==hosts)return "invalid_address";
    if(gateway && (!unicast(gateway) || (gateway&mask)!=(address&mask) || gateway==address || !(gateway&hosts) || (gateway&hosts)==hosts))return "invalid_gateway";
    if((dns1&&!unicast(dns1)) || (dns2&&!unicast(dns2)))return "invalid_dns";
    return nullptr;
  }
};
enum class NetworkChoice : uint8_t { Wifi, Ap, SharedHost, SharedClient, Ethernet };
struct NetworkConfig { NetworkChoice choice=NetworkChoice::Wifi; IpConfig wifi,ethernet; };
using NetworkBytes=std::array<uint8_t,48>;
inline NetworkBytes encodeNetwork(const NetworkConfig &n) {
  NetworkBytes b{1,uint8_t(n.choice),uint8_t(n.wifi.automatic),uint8_t(n.ethernet.automatic)};
  const uint32_t values[]={n.wifi.address,n.wifi.mask,n.wifi.gateway,n.wifi.dns1,n.wifi.dns2,n.ethernet.address,n.ethernet.mask,n.ethernet.gateway,n.ethernet.dns1,n.ethernet.dns2};
  for(unsigned i=0;i<10;++i)for(unsigned j=0;j<4;++j)b[4+4*i+j]=uint8_t(values[i]>>(8*j));
  uint32_t hash=2166136261u;for(unsigned i=0;i<44;++i)hash=(hash^b[i])*16777619u;
  for(unsigned j=0;j<4;++j)b[44+j]=uint8_t(hash>>(8*j));
  return b;
}
inline bool decodeNetwork(const NetworkBytes &b,NetworkConfig &n) {
  if(b[0]!=1 || b[1]>4 || b[2]>1 || b[3]>1)return false;
  NetworkConfig c;c.choice=NetworkChoice(b[1]);c.wifi.automatic=b[2];c.ethernet.automatic=b[3];
  uint32_t *values[]={&c.wifi.address,&c.wifi.mask,&c.wifi.gateway,&c.wifi.dns1,&c.wifi.dns2,&c.ethernet.address,&c.ethernet.mask,&c.ethernet.gateway,&c.ethernet.dns1,&c.ethernet.dns2};
  for(unsigned i=0;i<10;++i)for(unsigned j=0;j<4;++j)*values[i]|=uint32_t(b[4+4*i+j])<<(8*j);
  if(c.wifi.validate()||c.ethernet.validate()||encodeNetwork(c)!=b)return false;
  n=c;return true;
}
}
