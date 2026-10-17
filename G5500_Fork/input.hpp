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
  // A selection change must discard the remainder of a partially received line,
  // even if that suffix happens to be a valid command by itself.
  void cancelPending() {
    discardingLine_ = discardingLine_ || size_ != 0;
    size_ = 0;
  }
  void discard(char byte) {
    std::string_view ignored;
    feed(byte, ignored);
  }
  bool feed(char byte, std::string_view &line);
  bool feed(char byte, std::string &line) {
    std::string_view view;
    if (!feed(byte, view)) return false;
    line.assign(view.data(), view.size());
    return true;
  }
};

// Keep a stream connected across selection changes. Freeze the number of bytes
// already queued at the boundary; consume them within the usual receive budget.
// A trailing fragment stays cancelled until its delimiter, even across reads.
class SelectionLineBuffer {
  LineBuffer line_;
  size_t staleBytes_ = 0;

public:
  void clear() {
    line_.clear();
    staleBytes_ = 0;
  }
  void select(size_t queuedBytes) {
    line_.cancelPending();
    staleBytes_ = queuedBytes;
  }
  bool feed(char byte, std::string_view &line) {
    if (staleBytes_) {
      line_.discard(byte);
      if (!--staleBytes_) line_.cancelPending();
      return false;
    }
    return line_.feed(byte, line);
  }
};
} // namespace forkrot
