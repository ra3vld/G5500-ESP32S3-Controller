// G5500 Fork; original by UA1CFM, modifications by RA3VLD. GPL-3.0.
#pragma once
#include <cstdint>
#include <string>
namespace forkrot {
class Button {
  bool rawReleased_ = true, stableReleased_ = true, armed_ = false;
  uint32_t rawChangedAt_ = 0;

public:
  bool sample(bool released, uint32_t now);
};
class LineBuffer {
  std::string buffer_;
  bool discardingLine_ = false;

public:
  void clear() {
    buffer_.clear();
    discardingLine_ = false;
  }
  bool feed(char byte, std::string &line);
};
} // namespace forkrot
