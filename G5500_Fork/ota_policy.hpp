// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
#include <string_view>
#include "http_request_gate.hpp"
namespace forkrot {
inline constexpr uint32_t OTA_CHUNK_SIZE = 2048;
struct OtaCapacity {
  uint32_t flash = 0, image = 0, slot = 0;
  unsigned slots = 0;
  bool metadata = false, separate = false;
  // Application slots must start on 64 KiB boundaries. Reserve the first
  // 64 KiB for bootloader/NVS/otadata and the last 64 KiB for coredumps.
  uint32_t minimumFlash() const { return 0x20000 + 2 * ((image + 0xffff) & ~0xffffu); }
  uint32_t recommendedFlash() const {
    uint32_t size = 1u << 20;
    while (size < minimumFlash() && size < (1u << 30)) size <<= 1;
    return size;
  }
  const char *reason() const {
    if (!image || !flash) return "unknown_capacity";
    if (flash < minimumFlash()) return "flash_too_small";
    if (!metadata || slots < 2 || !separate) return "partition_layout";
    if (slot < image) return "slot_too_small";
    return "available";
  }
  bool available() const { return std::string_view(reason()) == "available"; }
};
inline std::string_view httpHeader(std::string_view request, std::string_view key) {
  size_t pos = request.find("\r\n");
  if (pos == request.npos) return {};
  pos += 2;
  while (pos < request.size()) {
    const auto end = request.find("\r\n", pos);
    if (end == request.npos || end == pos) return {};
    const auto line = request.substr(pos, end - pos);
    const auto colon = line.find(':');
    if (colon != line.npos && headerEquals(line.substr(0, colon), key)) {
      auto value = line.substr(colon + 1);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
      while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
      return value;
    }
    pos = end + 2;
  }
  return {};
}
} // namespace forkrot
