// Fixed-capacity form decoder shared by HTTP control and management. GPL-3.0.
#pragma once
#include "api.hpp"
namespace forkrot {
inline bool decodeForm(std::string_view text, char *bytes, size_t capacity, FormArgument *entries, size_t entryCapacity, size_t &count, size_t limit) {
  auto hex=[](unsigned char c) {return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;};
  count = 0;
  if (limit>=capacity || text.size() > limit) return false;
  size_t used = 0;
  while (!text.empty()) {
    if (count == entryCapacity) return false;
    const auto amp = text.find('&');
    const auto pair = text.substr(0, amp);
    const auto equal = pair.find('=');
    if (equal == pair.npos || !equal) return false;
    std::string_view values[2];
    for (unsigned part = 0; part < 2; ++part) {
      const auto raw = part ? pair.substr(equal+1) : pair.substr(0,equal);
      const size_t start = used;
      for (size_t i = 0; i < raw.size(); ++i) {
        unsigned char c = raw[i];
        if (c == '%') {
          if (i+2 >= raw.size() || hex(raw[i+1]) < 0 || hex(raw[i+2]) < 0) return false;
          c = unsigned(hex(raw[i+1])*16 + hex(raw[i+2])); i += 2;
        } else if (c == '+') c = ' ';
        if (c < 32 || c == 127 || used == limit) return false;
        bytes[used++] = char(c);
      }
      values[part] = {bytes+start,used-start};
      if (values[part].size() > (part ? 128u : 32u)) return false;
    }
    for (size_t i = 0; i < count; ++i) if (entries[i].first == values[0]) return false;
    entries[count++] = {values[0],values[1]};
    if (amp == text.npos) break;
    text.remove_prefix(amp+1);
    if (text.empty()) return false;
  }
  return true;
}
}
