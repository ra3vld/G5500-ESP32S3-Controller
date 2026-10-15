// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "http_request_gate.hpp"
namespace forkrot {
inline std::string_view trimHttpSpace(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
  return text;
}
inline bool acceptsGzip(std::string_view header) {
  while (!header.empty()) {
    const auto comma = header.find(',');
    const auto token = trimHttpSpace(header.substr(0, comma));
    const auto semicolon = token.find(';');
    if (headerEquals(trimHttpSpace(token.substr(0, semicolon)), "gzip")) {
      if (semicolon == token.npos) return true;
      auto parameter = trimHttpSpace(token.substr(semicolon + 1));
      const auto equal = parameter.find('=');
      if (equal == parameter.npos || !headerEquals(trimHttpSpace(parameter.substr(0, equal)), "q")) return false;
      auto quality = trimHttpSpace(parameter.substr(equal + 1));
      if (quality.empty() || (quality.front() != '0' && quality.front() != '1')) return false;
      bool positive = quality.front() == '1';
      if (quality.size() == 1) return positive;
      if (quality.size() < 3 || quality.size() > 5 || quality[1] != '.') return false;
      for (size_t i = 2; i < quality.size(); ++i) {
        if (quality[i] < '0' || quality[i] > '9' || (quality.front() == '1' && quality[i] != '0')) return false;
        positive = positive || quality[i] != '0';
      }
      return positive;
    }
    if (comma == header.npos) break;
    header.remove_prefix(comma + 1);
  }
  return false; // identity remains available to clients not requesting gzip
}
} // namespace forkrot
