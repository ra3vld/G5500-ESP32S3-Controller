// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace forkrot {
// Send returns bytes written, -2 for retryable backpressure, -1 for failure.
// Partial progress never renews the deadline.
template<class Send, class Clock, class Pause>
size_t writeWithinBudget(const char *data, size_t size, uint32_t started, uint32_t budget,
                         Send send, Clock clock, Pause pause) {
  size_t written = 0;
  while (written < size && uint32_t(clock() - started) < budget) {
    const size_t chunk = std::min(size - written, size_t(1024));
    const int count = send(data + written, chunk);
    if (count == -1 || count < -2 || count > int(chunk)) break;
    if (count > 0) written += size_t(count);
    else pause();
  }
  return written;
}
class RestartDelay {
public:
  void request(uint32_t now) { if (!pending_) { started_ = now; pending_ = true; } }
  bool consume(uint32_t now) {
    if (!pending_ || uint32_t(now - started_) < 1500) return false;
    pending_ = false;
    return true;
  }
private:
  uint32_t started_ = 0;
  bool pending_ = false;
};
} // namespace forkrot
