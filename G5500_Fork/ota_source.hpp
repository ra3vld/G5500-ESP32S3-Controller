// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
namespace forkrot {
inline constexpr size_t OtaUrlLimit=384;
inline bool otaUrl(std::string_view url) {
  const size_t prefix=url.substr(0,8)=="https://"?8:url.substr(0,7)=="http://"?7:0;
  if(!prefix || url.size()<=prefix || url.size()>OtaUrlLimit)return false;
  const auto host=url.substr(prefix,url.find('/',prefix)-prefix);
  if(host.empty() || host.front()==':' || host.find_first_of("@?#")!=host.npos)return false;
  for(unsigned char c:url)if(c<=32 || c>=127 || c=='"' || c=='\\' || c=='#')return false;
  return true;
}
struct OtaSource {
  // Empty URL selects this project's GitHub Releases.
  char url[OtaUrlLimit+1]{};
  bool automatic=false;
  bool valid() const {return std::memchr(url,0,sizeof url) && (!url[0] || otaUrl(url));}
};
using OtaSourceBytes=std::array<uint8_t,OtaUrlLimit+8>;
inline uint32_t otaSourceHash(const OtaSourceBytes &b) {
  uint32_t h=2166136261u;for(size_t i=0;i<b.size()-4;++i)h=(h^b[i])*16777619u;return h;
}
inline OtaSourceBytes encodeOtaSource(const OtaSource &s) {
  OtaSourceBytes b{};b[0]=1;b[1]=s.automatic;
  std::memcpy(b.data()+2,s.url,sizeof s.url);
  const auto h=otaSourceHash(b);for(unsigned i=0;i<4;++i)b[b.size()-4+i]=uint8_t(h>>(i*8));return b;
}
inline bool decodeOtaSource(const OtaSourceBytes &b,OtaSource &s) {
  const auto h=otaSourceHash(b);if(b[0]!=1 || b[1]>1)return false;
  for(unsigned i=0;i<4;++i)if(b[b.size()-4+i]!=uint8_t(h>>(i*8)))return false;
  OtaSource next;next.automatic=b[1];std::memcpy(next.url,b.data()+2,sizeof next.url);
  if(!next.valid())return false;
  s=next;return true;
}
struct OtaVersion {
  uint32_t part[3]{};bool development=false;
  bool parse(std::string_view s) {
    if(s.substr(0,1)=="v")s.remove_prefix(1);
    for(unsigned i=0;i<3;++i){
      const auto start=s.size();const bool zero=!s.empty() && s.front()=='0';uint32_t n=0;
      while(!s.empty() && s.front()>='0' && s.front()<='9'){
        if(n>100000000u)return false;
        n=n*10+unsigned(s.front()-'0');s.remove_prefix(1);
      }
      const auto count=start-s.size();if(!count || (count>1 && zero))return false;
      part[i]=n;if(i<2){if(s.empty() || s.front()!='.')return false;s.remove_prefix(1);}
    }
    development=!s.empty() && s.front()=='-';
    if(s.empty())return true;
    if(s.front()!='-' && s.front()!='+')return false;
    bool build=s.front()=='+';s.remove_prefix(1);bool label=false;
    for(unsigned char c:s) {
      if(c=='.'){if(!label)return false;label=false;}
      else if(c=='+'){if(build || !label)return false;build=true;label=false;}
      else if((c>='0'&&c<='9') || (c>='a'&&c<='z') || (c>='A'&&c<='Z') || c=='-')label=true;
      else return false;
    }
    return label;
  }
};
inline bool otaNewer(std::string_view candidate,std::string_view installed) {
  OtaVersion a,b;if(!a.parse(candidate)||a.development||!b.parse(installed))return false;
  for(unsigned i=0;i<3;++i)if(a.part[i]!=b.part[i])return a.part[i]>b.part[i];
  return b.development;
}
} // namespace forkrot
