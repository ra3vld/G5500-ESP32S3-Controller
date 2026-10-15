#include "diagnostic_stream.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
using namespace forkrot;
int main() {
  DiagnosticStream stream;
  unsigned fills = 0, writes = 0;
  std::string output;
  auto fill = [&](uint8_t page, char *buffer, size_t capacity) {
    ++fills;
    if (page == 7 || page == 8) return 0; // no OOM yet
    return std::snprintf(buffer, capacity, "line %u\r\n", unsigned(page));
  };
  auto write = [&](DiagnosticStream::Port port, const char *data, size_t size) {
    assert(port == DiagnosticStream::Port::Usb);
    // UART can accept only three bytes, and sometimes cannot accept any.
    const size_t count = ++writes % 3 == 0 ? 0 : std::min(size, size_t(3));
    output.append(data, count);
    return count;
  };
  stream.start(DiagnosticStream::Port::Usb, 10);
  for (uint32_t now = 10; stream.active(); now += 5) {
    const unsigned before = writes;
    stream.service(now, fill, write);
    assert(writes <= before + 1); // no spin/wait for capacity
    // A concurrent H4 must not restart the reply or redirect it to Bluetooth.
    if (stream.active()) stream.start(DiagnosticStream::Port::Bluetooth, now);
  }
  assert(fills == 10);
  assert(output == "line 0\r\nline 1\r\nline 2\r\nline 3\r\nline 4\r\nline 5\r\nline 6\r\nline 9\r\n");
  fills = writes = 0; output.clear();
  stream.start(DiagnosticStream::Port::Bluetooth, UINT32_MAX - 999);
  auto blocked = [&](DiagnosticStream::Port port, const char *, size_t) {
    assert(port == DiagnosticStream::Port::Bluetooth); ++writes; return size_t(0);
  };
  stream.service(UINT32_MAX - 999, fill, blocked);
  stream.service(3999, fill, blocked);
  assert(stream.active() && fills == 1 && writes == 2); // preserve line under backpressure
  stream.service(4000, fill, blocked);
  assert(!stream.active() && writes == 2); // deadline handles clock wrap
  stream.start(DiagnosticStream::Port::Bluetooth, 5000);
  auto btWrite = [&](DiagnosticStream::Port port, const char *data, size_t size) {
    assert(port == DiagnosticStream::Port::Bluetooth);
    if (++writes % 2) return size_t(0); // full SPP queue, then available
    output.append(data, size); return size;
  };
  for (uint32_t now = 5000; stream.active(); now += 5) stream.service(now, fill, btWrite);
  assert(output == "line 0\r\nline 1\r\nline 2\r\nline 3\r\nline 4\r\nline 5\r\nline 6\r\nline 9\r\n");
  std::cout << "PASS: diagnostic partial UART writes, SPP backpressure, no reply restart/redirection, wrap-safe deadline\n";
}
