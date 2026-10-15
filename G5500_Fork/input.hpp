// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace forkrot {
class Button {
  bool rawReleased_ = true, stableReleased_ = true, armed_ = false;
  uint32_t rawChangedAt_ = 0;

public:
  bool sample(bool released, uint32_t now);
};
class LineBuffer {
  char buffer_[512];
  size_t size_ = 0;
  bool discardingLine_ = false;

public:
  void clear() {
    size_ = 0;
    discardingLine_ = false;
  }
  bool feed(char byte, std::string_view &line);
  bool feed(char byte, std::string &line) {
    std::string_view view;
    if (!feed(byte, view)) return false;
    line.assign(view.data(), view.size());
    return true;
  }
};
} // namespace forkrot
