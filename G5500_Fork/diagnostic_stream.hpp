// G5500 Fork; GPL-3.0.
#pragma once
#include <cstddef>
#include <cstdint>
namespace forkrot {
// One diagnostic line at a time, no heap and no wait for UART/SPP capacity.
// The destination is pinned to the requesting transport until completion.
class DiagnosticStream {
public:
  enum class Port : uint8_t { Usb, Bluetooth };
  void start(Port port, uint32_t now) {
    if (active_) return;
    active_ = true; port_ = port; started_ = now;
    page_ = 0; length_ = offset_ = 0;
  }
  bool active() const { return active_; }
  template<class Fill, class Write>
  void service(uint32_t now, Fill fill, Write write) {
    if (!active_) return;
    if (uint32_t(now - started_) >= 5000) { active_ = false; return; }
    while (offset_ == length_) {
      if (page_ == 10) { active_ = false; return; }
      const int count = fill(page_++, buffer_, sizeof buffer_);
      length_ = count <= 0 ? 0 : size_t(count) < sizeof buffer_ ? size_t(count) : sizeof buffer_ - 1;
      offset_ = 0;
    }
    const size_t remaining = length_ - offset_;
    const size_t sent = write(port_, buffer_ + offset_, remaining);
    if (sent <= remaining) offset_ += sent;
  }
private:
  char buffer_[256]{};
  uint32_t started_ = 0;
  size_t length_ = 0, offset_ = 0;
  uint8_t page_ = 0;
  Port port_ = Port::Usb;
  bool active_ = false;
};
} // namespace forkrot
