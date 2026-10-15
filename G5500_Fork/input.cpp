// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "input.hpp"
namespace forkrot {
bool Button::sample(bool released, uint32_t now) {
  if (released != rawReleased_) {
    rawReleased_ = released;
    rawChangedAt_ = now;
  }
  if (uint32_t(now - rawChangedAt_) < 40) {
    return false;
  }
  if (rawReleased_) {
    armed_ = true;
    stableReleased_ = true;
    return false;
  }
  if (stableReleased_ && armed_) {
    stableReleased_ = false;
    armed_ = false;
    return true;
  }
  stableReleased_ = false;
  return false;
}
bool LineBuffer::feed(char byte, std::string_view &line) {
  if (byte == '\r' || byte == '\n') {
    bool ready = !discardingLine_ && size_ != 0;
    if (ready) {
      line = {buffer_, size_};
    }
    clear();
    return ready;
  }
  if (discardingLine_) {
    return false;
  }
  if ((byte < 32 && byte != '\t') || byte > 126 || size_ >= sizeof buffer_) {
    size_ = 0;
    discardingLine_ = true;
    return false;
  }
  buffer_[size_++] = byte;
  return false;
}
} // namespace forkrot
