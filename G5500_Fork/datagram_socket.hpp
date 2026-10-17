// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#ifdef ARDUINO_ARCH_ESP32
#include <lwip/sockets.h>
#else
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <string_view>

namespace forkrot {
struct Datagram {
  struct Free { void operator()(char *data) const { std::free(data); } };
  std::unique_ptr<char, Free> data;
  size_t size = 0;
  uint16_t port = 0;
  uint32_t address = 0; // IPv4 in network byte order, like uint32_t(IPAddress).
  std::string_view text() const { return data ? std::string_view(data.get(), size) : std::string_view(); }
};
enum class DatagramResult { Empty, Ready, Dropped };

// The logger protocols use IPv4 unicast. Unlike NetworkUDP, these sockets have
// no permanent 1460-byte TX buffers or separate RX copy/circular buffers.
class DatagramSocket {
public:
  static constexpr size_t MAX_PACKET = 4096;
  DatagramSocket() = default;
  DatagramSocket(const DatagramSocket &) = delete;
  DatagramSocket &operator=(const DatagramSocket &) = delete;
  ~DatagramSocket() { stop(); }
  bool begin(uint16_t port) {
    stop();
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;
    const int yes = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(port);
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0 ||
        ::bind(fd_, reinterpret_cast<sockaddr *>(&local), sizeof local) < 0) {
      stop(); return false;
    }
    return true;
  }
  void stop() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }
  uint16_t localPort() const {
    sockaddr_in local{};
    socklen_t size = sizeof local;
    return fd_ >= 0 && ::getsockname(fd_, reinterpret_cast<sockaddr *>(&local), &size) == 0
               ? ntohs(local.sin_port) : 0;
  }
  DatagramResult receive(Datagram &packet) {
    packet = Datagram{};
    if (fd_ < 0) return DatagramResult::Empty;
    int available = 0;
    if (::ioctl(fd_, FIONREAD, &available) < 0) return DatagramResult::Empty;
    if (available <= 0) {
      // A zero-length datagram must be consumed so it cannot block the queue.
      char byte;
      if (::recvfrom(fd_, &byte, 1, MSG_PEEK | MSG_DONTWAIT, nullptr, nullptr) != 0)
        return DatagramResult::Empty;
      discard(); return DatagramResult::Dropped;
    }
    // lwIP FIONREAD may include several queued datagrams. recvmsg's MSG_TRUNC
    // output flag and length check prevent parsing an oversized packet's prefix.
    const size_t capacity = std::min(size_t(available), MAX_PACKET);
    packet.data.reset(static_cast<char *>(std::malloc(capacity + 1)));
    if (!packet.data) { discard(); return DatagramResult::Dropped; }
    sockaddr_in sender{};
    iovec part{packet.data.get(), capacity};
    msghdr message{};
    message.msg_name = &sender;
    message.msg_namelen = sizeof sender;
    message.msg_iov = &part;
    message.msg_iovlen = 1;
    const auto count = ::recvmsg(fd_, &message, MSG_DONTWAIT);
    if (count < 0) { packet = Datagram{}; return DatagramResult::Empty; }
    if (!count || size_t(count) > capacity || (message.msg_flags & MSG_TRUNC) ||
        sender.sin_family != AF_INET) {
      packet = Datagram{}; return DatagramResult::Dropped;
    }
    packet.size = size_t(count);
    packet.data.get()[packet.size] = '\0';
    packet.address = sender.sin_addr.s_addr;
    packet.port = ntohs(sender.sin_port);
    return DatagramResult::Ready;
  }
  // Fixed-buffer receive for HUD: no allocation per poll or per remote device.
  int receiveInto(uint8_t *out, size_t capacity, uint32_t &address, uint16_t &port) {
    if (fd_ < 0)
      return -1;
    sockaddr_in sender{};
    iovec part{out, capacity};
    msghdr message{};
    message.msg_name = &sender;
    message.msg_namelen = sizeof sender;
    message.msg_iov = &part;
    message.msg_iovlen = 1;
    const int count = ::recvmsg(fd_, &message, MSG_DONTWAIT);
    if (count < 0)
      return -1;
    if (!count || size_t(count) > capacity || (message.msg_flags & MSG_TRUNC) || sender.sin_family != AF_INET)
      return -2;
    address = sender.sin_addr.s_addr;
    port = ntohs(sender.sin_port);
    return count;
  }
  bool send(uint32_t address, uint16_t port, std::string_view text) {
    if (fd_ < 0 || !port || text.size() > MAX_PACKET) return false;
    sockaddr_in recipient{};
    recipient.sin_family = AF_INET;
    recipient.sin_addr.s_addr = address;
    recipient.sin_port = htons(port);
    return ::sendto(fd_, text.data(), text.size(), MSG_DONTWAIT,
                    reinterpret_cast<sockaddr *>(&recipient), sizeof recipient) == ssize_t(text.size());
  }
private:
  void discard() {
    char byte;
    ::recvfrom(fd_, &byte, 1, MSG_DONTWAIT, nullptr, nullptr);
  }
  int fd_ = -1;
};
} // namespace forkrot
