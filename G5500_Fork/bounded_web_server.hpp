// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <WebServer.h>
#include <lwip/sockets.h>
#include <cerrno>
#include "write_budget.hpp"
namespace forkrot {
class BoundedWebServer : public WebServer {
public:
  using WebServer::WebServer;
  void handleClient() override {
    started_ = failed_ = false;
    WebServer::handleClient();
  }
  bool writeFailed() const { return failed_; }
  void sendBuffer(int code, const char *type, const char *data, size_t size) {
    String header;
    _prepareHeader(header, code, type, size);
    if (_currentClientWrite(header.c_str(), header.length()) == header.length())
      _currentClientWrite(data, size);
  }
protected:
  size_t _currentClientWrite(const char *data, size_t size) override {
    if (!size || failed_) return 0;
    if (!started_) { started_ = true; startedMs_ = millis(); }
    const int socket = client().fd();
    const auto sent = writeWithinBudget(data, size, startedMs_, 250,
        [socket](const char *part, size_t count) {
          const int result = ::send(socket, part, count, MSG_DONTWAIT);
          if (result >= 0) return result;
          return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? -2 : -1;
        }, [] { return millis(); }, [] { delay(1); });
    if (sent != size) { failed_ = true; client().stop(); }
    return sent;
  }
  size_t _currentClientWrite_P(PGM_P data, size_t size) override {
    return _currentClientWrite(data, size); // ESP32 maps PROGMEM into its address space.
  }
private:
  uint32_t startedMs_ = 0;
  bool started_ = false, failed_ = false;
};
} // namespace forkrot
