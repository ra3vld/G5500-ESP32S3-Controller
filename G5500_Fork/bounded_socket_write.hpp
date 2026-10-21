// G5500 Fork; GPL-3.0.
#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdint>
namespace forkrot {
// Send is nonblocking. Retain the byte offset across short writes/backpressure:
// retrying a whole WebSocket frame after a partial write corrupts the stream.
// One deadline covers all retries, including repeated successful short writes.
template<class Send, class Now, class Pause>
int boundedSocketWrite(const char *data, size_t size, uint32_t budgetMs,
                       Send send, Now now, Pause pause) {
  const uint32_t started = now();
  size_t sent = 0;
  while (sent < size) {
    if (uint32_t(now() - started) >= budgetMs) { errno = ETIMEDOUT; return -1; }
    const int n = send(data + sent, size - sent);
    if (n > 0) { sent += size_t(n); continue; }
    if (!n) { errno = ECONNRESET; return -1; }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR &&
        errno != ENOBUFS && errno != ENOMEM) return -1;
    pause();
  }
  return int(sent);
}
} // namespace forkrot
