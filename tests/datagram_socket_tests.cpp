#include "motion_fixture.hpp"
// Exercise the firmware's actual datagram adapter over localhost UDP.
#include "datagram_socket.hpp"
#include "protocol.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

static bool failAllocation = false;
static unsigned allocations = 0;
static size_t largestAllocation = 0;
extern "C" void *__real_malloc(size_t);
extern "C" void *__wrap_malloc(size_t size) {
  ++allocations;
  largestAllocation = std::max(largestAllocation, size);
  return failAllocation ? nullptr : __real_malloc(size);
}

using namespace forkrot;
static const uint32_t loopback = htonl(INADDR_LOOPBACK);

static DatagramResult receive(DatagramSocket &socket, Datagram &packet) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  do {
    const auto result = socket.receive(packet);
    if (result != DatagramResult::Empty) return result;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < deadline);
  assert(false && "UDP receive deadline");
  return DatagramResult::Empty;
}

int main() {
  static_assert(sizeof(DatagramSocket) == sizeof(int));
  DatagramSocket n1mm, pst, logger;
  const unsigned before = allocations;
  assert(n1mm.begin(0) && pst.begin(0) && logger.begin(0));
  assert(n1mm.localPort() && pst.localPort() && logger.localPort());
  assert(allocations == before); // no permanent application buffers
  Datagram packet;
  assert(n1mm.receive(packet) == DatagramResult::Empty);
  assert(allocations == before); // no RX allocation while idle

  Controller controller = forkrot::testController();
  Protocol protocol(controller);
  const std::string target = "<N1MMRotor><rotor>G5500</rotor><goazi>123</goazi></N1MMRotor>";
  assert(logger.send(loopback, n1mm.localPort(), target));
  assert(allocations == before); // send directly, without a TX copy
  assert(receive(n1mm, packet) == DatagramResult::Ready);
  assert(packet.text() == target && packet.address == loopback);
  assert(allocations == before + 1 && largestAllocation == target.size() + 1);
  assert(protocol.datagram(packet.text(), 0).result == Result::Pending);
  assert(controller.pending.az == 123);

  assert(logger.send(loopback, pst.localPort(), "<PST>AZ?</PST>"));
  assert(receive(pst, packet) == DatagramResult::Ready);
  const auto response = protocol.datagram(packet.text(), 1);
  assert(response.matched && !response.reply.empty());
  assert(pst.send(packet.address, logger.localPort(), response.reply));
  assert(receive(logger, packet) == DatagramResult::Ready);
  assert(packet.text() == response.reply);
  const auto report = protocol.positionReport(137);
  assert(n1mm.send(loopback, logger.localPort(), report));
  assert(receive(logger, packet) == DatagramResult::Ready && packet.text() == "G5500@1370");

  // Several packets in one queue: FIONREAD can report their combined size on
  // lwIP. Each datagram still has its own exact length and sender.
  for (const auto text : {"first", "second", "third"})
    assert(logger.send(loopback, n1mm.localPort(), text));
  for (const auto text : {"first", "second", "third"})
    assert(receive(n1mm, packet) == DatagramResult::Ready && packet.text() == text);

  std::string boundary = target;
  boundary.resize(DatagramSocket::MAX_PACKET, ' ');
  assert(logger.send(loopback, n1mm.localPort(), boundary));
  assert(receive(n1mm, packet) == DatagramResult::Ready && packet.text() == boundary);
  assert(protocol.datagram(packet.text(), 2).result == Result::Pending);

  // Bypass the adapter's TX limit to send an oversized datagram with a valid
  // command prefix. It must be dropped in full, never executed as a prefix.
  const int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
  assert(raw >= 0);
  sockaddr_in destination{};
  destination.sin_family = AF_INET;
  destination.sin_addr.s_addr = loopback;
  destination.sin_port = htons(n1mm.localPort());
  boundary += "x";
  assert(::sendto(raw, boundary.data(), boundary.size(), 0,
                  reinterpret_cast<sockaddr *>(&destination), sizeof destination) == ssize_t(boundary.size()));
  assert(receive(n1mm, packet) == DatagramResult::Dropped && !packet.data);
  assert(logger.send(loopback, n1mm.localPort(), "after oversized"));
  assert(receive(n1mm, packet) == DatagramResult::Ready && packet.text() == "after oversized");

  // A failed allocation and an empty packet must not wedge the receive queue.
  assert(logger.send(loopback, n1mm.localPort(), target));
  failAllocation = true;
  assert(receive(n1mm, packet) == DatagramResult::Dropped && !packet.data);
  failAllocation = false;
  assert(logger.send(loopback, n1mm.localPort(), ""));
  assert(receive(n1mm, packet) == DatagramResult::Dropped);
  assert(logger.send(loopback, n1mm.localPort(), "recovered"));
  assert(receive(n1mm, packet) == DatagramResult::Ready && packet.text() == "recovered");

  int error = 0;
  assert(!logger.send(loopback, n1mm.localPort(), boundary, &error) && error == EMSGSIZE);
  assert(!logger.send(loopback, 0, target, &error) && error == EINVAL);
  for (unsigned i = 0; i < 1000; ++i) {
    n1mm.stop();
    assert(!n1mm.localPort() && !n1mm.send(loopback, logger.localPort(), target, &error) && error == EBADF);
    assert(n1mm.begin(0));
  }
  // Verify the actual outgoing IP traffic class, not just a socket setting.
  assert(logger.setIpPrecedence(4) && !logger.setIpPrecedence(8));
  const int receiveTos = 1;
  assert(::setsockopt(raw, IPPROTO_IP, IP_RECVTOS, &receiveTos, sizeof receiveTos) == 0);
  sockaddr_in local{}; socklen_t localSize = sizeof local;
  assert(::getsockname(raw, reinterpret_cast<sockaddr *>(&local), &localSize) == 0);
  assert(logger.send(loopback, ntohs(local.sin_port), "hud"));
  char received[8], ancillary[CMSG_SPACE(sizeof(int))]{};
  iovec part{received, sizeof received}; msghdr message{};
  message.msg_iov = &part; message.msg_iovlen = 1;
  message.msg_control = ancillary; message.msg_controllen = sizeof ancillary;
  assert(::recvmsg(raw, &message, 0) == 3);
  bool priorityReceived = false;
  for (auto *c = CMSG_FIRSTHDR(&message); c; c = CMSG_NXTHDR(&message, c))
    if (c->cmsg_level == IPPROTO_IP && c->cmsg_type == IP_TOS)
      priorityReceived = *reinterpret_cast<unsigned char *>(CMSG_DATA(c)) == 0x80;
  assert(priorityReceived);
  logger.stop(); assert(!logger.setIpPrecedence(4));
  ::close(raw);
  std::puts("UDP: N1MM/PST receive/reply/report, exact 4KB boundary, oversized/OOM/empty recovery, no idle/TX allocations, 1000 rebinds PASS");
}
