// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstddef>
#include <string_view>
namespace forkrot {
// Retain complete recent lines within one fixed RAM budget. No heap growth.
template<size_t Capacity, size_t LineLimit = 220> class ByteLog {
  static_assert(Capacity > LineLimit);
public:
  void clear() { start_ = used_ = pending_ = 0; }
  void append(std::string_view text, bool newline = false) {
    for (char ch : text) {
      if (ch == '\r') continue;
      if (ch == '\n') flush();
      else {
        pendingBytes_[pending_++] = ch;
        if (pending_ == LineLimit) flush();
      }
    }
    if (newline) flush();
  }
  size_t size() const { return used_ + pending_; }
  size_t copyTo(char *out, size_t capacity) const {
    if (capacity <= size()) { if (capacity) out[0] = 0; return 0; }
    size_t count = 0;
    for (size_t i = 0; i < used_; ++i) out[count++] = bytes_[(start_ + i) % Capacity];
    for (size_t i = 0; i < pending_; ++i) out[count++] = pendingBytes_[i];
    out[count] = 0;
    return count;
  }
  template<class Output> void copyTo(Output &out) const {
    for (size_t i = 0; i < used_; ++i) out += bytes_[(start_ + i) % Capacity];
    for (size_t i = 0; i < pending_; ++i) out += pendingBytes_[i];
  }
private:
  void flush() {
    while (used_ + pending_ + 1 > Capacity) {
      char ch;
      do {
        ch = bytes_[start_];
        start_ = (start_ + 1) % Capacity;
        --used_;
      } while (used_ && ch != '\n');
    }
    for (size_t i = 0; i < pending_; ++i) put(pendingBytes_[i]);
    put('\n');
    pending_ = 0;
  }
  void put(char ch) { bytes_[(start_ + used_++) % Capacity] = ch; }
  char bytes_[Capacity];
  char pendingBytes_[LineLimit];
  size_t start_ = 0, used_ = 0, pending_ = 0;
};
} // namespace forkrot
