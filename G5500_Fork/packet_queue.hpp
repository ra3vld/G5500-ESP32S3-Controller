// G5500 Fork; GPL-3.0.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace forkrot {
// Single producer, single consumer. Bound both packet count and total payload.
// Byte capacity is a power of two, so uint32_t cursor wrap preserves indexing.
template<size_t Bytes, size_t Slots> class PacketQueue {
  static_assert(Bytes && !(Bytes & (Bytes - 1)));
  static_assert(Slots && !(Slots & (Slots - 1)));
  struct Entry { uint32_t start, size, tag; };
  char bytes_[Bytes];
  Entry entries_[Slots];
  std::atomic<uint32_t> head_{0}, tail_{0}, consumed_{0};
  uint32_t written_ = 0;
public:
  bool push(std::string_view text, uint32_t tag = 0) {
    const uint32_t head = head_.load(std::memory_order_relaxed);
    if (uint32_t(head - tail_.load(std::memory_order_acquire)) == Slots ||
        text.size() > Bytes - uint32_t(written_ - consumed_.load(std::memory_order_acquire))) return false;
    for (size_t i = 0; i < text.size(); ++i) bytes_[(written_ + i) % Bytes] = text[i];
    entries_[head % Slots] = {written_, uint32_t(text.size()), tag};
    written_ += text.size();
    head_.store(head + 1, std::memory_order_release);
    return true;
  }
  bool pop(char *out, size_t capacity, size_t &size, uint32_t &tag) {
    const uint32_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    const auto entry = entries_[tail % Slots];
    if (entry.size > capacity) return false;
    for (size_t i = 0; i < entry.size; ++i) out[i] = bytes_[(entry.start + i) % Bytes];
    size = entry.size; tag = entry.tag;
    consumed_.store(entry.start + entry.size, std::memory_order_release);
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }
  bool empty() const { return tail_.load() == head_.load(); }
  // Consumer only; a concurrent producer may append after this snapshot.
  void clear() {
    const auto head = head_.load(std::memory_order_acquire);
    if (head == tail_.load(std::memory_order_relaxed)) return;
    const auto entry = entries_[(head - 1) % Slots];
    consumed_.store(entry.start + entry.size, std::memory_order_release);
    tail_.store(head, std::memory_order_release);
  }
};
}
