// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <WebServer.h>
#include <lwip/sockets.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include "write_budget.hpp"
#include "http_request_gate.hpp"
#include "heap_metrics.hpp"
#include "http_diagnostics.hpp"
namespace forkrot {
class BoundedWebServer : public WebServer {
public:
  using WebServer::WebServer;
  void setRequestBuffer(char *data, size_t capacity) {
    requestBuffer_ = data;
    requestCapacity_ = std::min(capacity, size_t(4096));
  }
  struct StopResult {
    int status = 200;
    uint32_t requestId = 0;
  };
  using StopHandler = StopResult (*)(void *, const StopRequest &);
  void setStopHandler(StopHandler handler, void *context) { stopHandler_ = handler; stopContext_ = context; }
  using RequestHandler = bool (*)(void *, std::string_view);
  void setRequestHandler(RequestHandler handler, void *context) { requestHandler_ = handler; requestContext_ = context; }
  std::string_view argumentName(size_t i) const { return {_currentArgs[i].key.c_str(), _currentArgs[i].key.length()}; }
  std::string_view argumentValue(size_t i) const { return {_currentArgs[i].value.c_str(), _currentArgs[i].value.length()}; }
  // Set only after OTA has stopped motion and accepted the upload.
  void holdTransferConnection() { transferConnection_ = true; }
  uint32_t readFailures() const { return diagnostics_.readFailures; }
  uint32_t rejectedRequests() const { return diagnostics_.rejected; }
  uint32_t acceptedRequests() const { return diagnostics_.accepted; }
  const HttpDiagnostics &diagnostics() const { return diagnostics_; }
  void setLowMemoryPaths(const char *const *paths, size_t count) {
    lowMemoryPaths_ = paths; lowMemoryPathCount_ = count;
  }
  // data must remain valid until the response finishes (our pages are in flash).
  void sendPage(int code, const char *type, const char *data, size_t size) {
    String header;
    _prepareHeader(header, code, type, size);
    keepAliveResponse_ = persistentCandidate_ && code == 200;
    if (keepAliveResponse_) header.replace("\r\nConnection: close\r\n", "\r\nConnection: keep-alive\r\n");
    if (_currentClientWrite(header.c_str(), header.length()) != header.length()) return;
    _clearResponseHeaders(); // response header Strings are no longer needed
    if (!size) return;
    page_ = data; pageSize_ = size; pageSent_ = 0;
    htmlTransfer_ = type && std::strncmp(type, "text/html", 9) == 0;
    pageStartedMs_ = pageProgressMs_ = millis();
    pageAttemptMs_ = pageStartedMs_ - 20;
    pageRetryErrno_ = 0;
  }
  void handleClient() override {
    started_ = failed_ = false;
    if (page_) { servicePage(); return; }
    if (_currentStatus == HC_NONE) {
      if (heapFreeBytes() < 2048 || heapMaxBlockBytes() < 256) return;
      _currentClient = _server.accept();
      if (!_currentClient) return;
      ++diagnostics_.accepted;
      _currentClient.setNoDelay(true);
      _currentStatus = HC_WAIT_READ;
      _statusChange = millis();
      requestUsed_ = 0;
      persistentCandidate_ = keepAliveResponse_ = idlePersistent_ = false;
    }
    if (_currentStatus == HC_WAIT_READ) {
      if (!requestBuffer_ || !requestCapacity_) { reject(); return; }
      const int count = ::recv(client().fd(), requestBuffer_ + requestUsed_, requestCapacity_ - requestUsed_, MSG_DONTWAIT);
      if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        ++diagnostics_.readFailures; reject(); return;
      }
      if (count == 0) {
        if (idlePersistent_) releaseClient(); // normal peer close between requests
        else reject();
        return;
      }
      if (idlePersistent_ && count < 0) {
        // Keep one polling connection, but do not make another browser/control
        // request wait behind it. Never block the rotator loop while idle.
        if ((!transferConnection_ && _server.hasClient()) ||
            uint32_t(millis() - _statusChange) >= (transferConnection_ ? 10000u : 1500u)) releaseClient();
        return;
      }
      if (count > 0) {
        if (idlePersistent_) { idlePersistent_ = false; _statusChange = millis(); }
        requestUsed_ += size_t(count);
      }
      // Browsers preconnect without sending a request. Let queued control
      // requests past an empty socket instead of waiting the full read timeout.
      // Once any bytes arrived, retain the bounded fragmented-request parser.
      if (count < 0 && !requestUsed_ && !transferConnection_ && _server.hasClient()) {
        releaseClient(); return;
      }
      size_t requestSize = 0;
      const auto state = inspectRequest({requestBuffer_, requestUsed_}, requestCapacity_, &requestSize);
      if (state == RequestState::Invalid || state == RequestState::TooLarge) { reject(); return; }
      if (state == RequestState::Incomplete) {
        if (uint32_t(millis() - _statusChange) >= (transferConnection_ ? 10000u : 1000u)) reject();
        return; // wait across loop iterations, never inside the stock parser
      }
      // Leave headroom for request strings and handlers. The parser reads the
      // fixed snapshot, avoiding NetworkClient's separate 1436-byte RX malloc.
      const auto stop = emergencyStopRequest({requestBuffer_, requestSize});
      if (stopHandler_ && stop.axis != -2) {
        const auto result = stopHandler_(stopContext_, stop); // no parser/header allocation
        ++diagnostics_.requests;
        char body[96], reply[256];
        const int n =
            result.status == 202
                ? std::snprintf(body, sizeof body, "{\"ok\":false,\"result\":\"deferred\",\"requestId\":%lu}",
                                static_cast<unsigned long>(result.requestId))
                : std::snprintf(body, sizeof body, "{\"ok\":%s,\"result\":\"%s\"}",
                                result.status == 200 ? "true" : "false",
                                result.status == 200   ? "ok"
                                : result.status == 409 ? "cancelled"
                                                       : "unavailable");
        const int length =
            std::snprintf(reply, sizeof reply,
                          "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: "
                          "%d\r\nConnection: close\r\n\r\n%s",
                          result.status,
                          result.status == 200   ? "OK"
                          : result.status == 202 ? "Accepted"
                          : result.status == 409 ? "Conflict"
                                                 : "Service Unavailable",
                          n, body);
        if (length > 0 && size_t(length) < sizeof reply)
          _currentClientWrite(reply, size_t(length));
        releaseClient();
        return;
      }
      if (requestHandler_) {
        persistentCandidate_ = persistentOtaRequest({requestBuffer_, requestUsed_}, requestSize) ||
                               persistentStatusRequest({requestBuffer_, requestUsed_}, requestSize);
        keepAliveResponse_ = false;
        _contentLength = CONTENT_LENGTH_NOT_SET;
        _responseCode = 0;
        _clearResponseHeaders();
        if (requestHandler_(requestContext_, {requestBuffer_, requestSize})) {
          ++diagnostics_.requests;
          if (page_) servicePage();
          else finishResponse();
          return;
        }
      }
      // A binary OTA chunk must never reach WebServer's form parser.
      if (std::string_view(requestBuffer_, requestSize).substr(0, 27) == "POST /api/ota/write?offset=") { reject(); return; }
      if (!requestFitsMemory(requestSize)) { reject(); return; }
      persistentCandidate_ = persistentStatusRequest({requestBuffer_, requestUsed_}, requestSize);
      keepAliveResponse_ = false;
      RequestClient snapshot(*this, requestSize);
      snapshot.setTimeout(10);
      if (_parseRequest(snapshot)) {
        ++diagnostics_.requests;
        _contentLength = CONTENT_LENGTH_NOT_SET;
        _responseCode = 0;
        _clearResponseHeaders();
        if (_chain) _chain->runChain(*this, [this]() { return _handleRequest(); });
        else _handleRequest();
      }
      clearArguments(); // handlers have consumed the views; do not retain form Strings while idle
      if (page_) { servicePage(); return; }
      if (client().isSSE()) {
        _currentStatus = HC_WAIT_CLOSE;
        _statusChange = millis();
        return;
      }
      finishResponse();
      return;
    }
    WebServer::handleClient();
  }
  bool writeFailed() const { return failed_; }
  void sendBuffer(int code, const char *type, const char *data, size_t size) {
    sendPage(code, type, data, size); // caller-owned status buffer stays valid across loop iterations
  }
  // Recovery must not depend on String header allocation or the normal parser.
  // Payload is flash or the existing status buffer, valid across service calls.
  void sendFixed(int code, const char *type, const char *data, size_t size, bool allowKeepAlive = false, bool gzip = false) {
    char header[256];
    keepAliveResponse_ = allowKeepAlive && persistentCandidate_ && code == 200;
    const int count = std::snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: no-store\r\n%sConnection: %s\r\n\r\n",
        code, code == 200 ? "OK" : "Error", type, unsigned(size),
        gzip ? "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n" : "",
        keepAliveResponse_ ? "keep-alive" : "close");
    if (count <= 0 || size_t(count) >= sizeof header ||
        _currentClientWrite(header, size_t(count)) != size_t(count)) return;
    page_ = data; pageSize_ = size; pageSent_ = 0;
    htmlTransfer_ = std::strncmp(type,"text/html",9) == 0;
    pageStartedMs_ = pageProgressMs_ = millis(); pageAttemptMs_ = pageStartedMs_-20;
    pageRetryErrno_ = 0;
  }
protected:
  size_t _currentClientWrite(const char *data, size_t size) override {
    if (!size || failed_) return 0;
    if (!started_) { started_ = true; startedMs_ = millis(); }
    const int socket = client().fd();
    int writeError = 0;
    const auto sent = writeWithinBudget(data, size, startedMs_, transferConnection_ ? 100 : 5,
        [socket, &writeError](const char *part, size_t count) {
          const int result = ::send(socket, part, count, MSG_DONTWAIT);
          if (result >= 0) return result;
          writeError = errno;
          return retryableWrite(writeError) ? -2 : -1;
        }, [] { return millis(); }, [] { delay(1); });
    if (sent != size) failWrite("api", writeError ? writeError : ETIMEDOUT, sent, size);
    return sent;
  }
  size_t _currentClientWrite_P(PGM_P data, size_t size) override {
    return _currentClientWrite(data, size); // ESP32 maps PROGMEM into its address space.
  }
private:
  bool requestFitsMemory(size_t size) const {
    const std::string_view request(requestBuffer_, size);
    if (request.substr(0, 4) == "GET ") {
      const auto end = request.find(' ', 4);
      const auto path = request.substr(4, end == request.npos ? 0 : end - 4);
      for (size_t i = 0; i < lowMemoryPathCount_; ++i) if (path == lowMemoryPaths_[i]) {
        // These handlers use fixed buffers/flash. Only header parsing needs
        // contiguous strings; a fragmented heap need not block the whole UI.
        size_t largest = 512;
        for (size_t pos = 0; pos < size;) {
          const auto next = request.find("\r\n", pos);
          if (next == request.npos || next == pos) break;
          largest = std::max(largest, next - pos + 1);
          pos = next + 2;
        }
        return heapFreeBytes() >= std::max(size_t(4096), size * 2 + 2048) &&
               heapMaxBlockBytes() >= largest;
      }
    }
    // Stock POST parsing temporarily holds the body, combined form and decoded
    // arguments together. A constant 8KB threshold is insufficient for a 4KB
    // form, even with a bounded argument count.
    return heapFreeBytes() >= std::max(size_t(8192), size * 4 + 4096) &&
           heapMaxBlockBytes() >= std::max(size_t(4096), size + 1);
  }
  void servicePage() {
    const uint32_t now = millis();
    // ESP32's initial TCP RTO is 3s. A 2s HTML deadline can abort before
    // retransmission gets a chance, especially with shared Wi-Fi/BT radio time.
    // Polling JSON keeps its shorter deadline; every send remains nonblocking.
    const uint32_t idleLimit = htmlTransfer_ ? 10000 : 2000;
    const uint32_t totalLimit = htmlTransfer_ ? 30000 : 15000;
    if (uint32_t(now - pageStartedMs_) >= totalLimit || uint32_t(now - pageProgressMs_) >= idleLimit) {
      failWrite(uint32_t(now - pageStartedMs_) >= totalLimit ? "total" : "idle",
                pageRetryErrno_ ? pageRetryErrno_ : ETIMEDOUT, pageSent_, pageSize_);
      releaseClient(); return;
    }
    if (!networkBulkTxReady()) return;
    // A healthy heap can drain at loop speed. Retain conservative pacing when
    // radio/BT traffic leaves little RAM, and never spin waiting for TCP space.
    const uint32_t interval = heapFreeBytes() >= 32768 && heapMaxBlockBytes() >= 6144 ? 5 : 20;
    if (uint32_t(now - pageAttemptMs_) < interval) return;
    pageAttemptMs_ = now;
    const size_t chunk = std::min(pageSize_ - pageSent_, size_t(512));
    const int count = ::send(client().fd(), page_ + pageSent_, chunk, MSG_DONTWAIT);
    if (count > 0) {
      pageSent_ += size_t(count);
      pageProgressMs_ = now;
      pageRetryErrno_ = 0;
      if (pageSent_ == pageSize_) finishResponse();
    } else if (count < 0) {
      pageRetryErrno_ = errno;
      if (!retryableWrite(pageRetryErrno_)) {
        failWrite("socket", pageRetryErrno_, pageSent_, pageSize_); releaseClient();
      }
    }
    // At most one nonblocking send per loop, including on a slow connection.
  }
  static bool retryableWrite(int error) {
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR || error == ENOMEM || error == ENOBUFS;
  }
  void failWrite(const char *cause, int error, size_t sent, size_t size) {
    if (!failed_) {
      ++diagnostics_.writeFailures;
      diagnostics_.writeCause = cause; diagnostics_.writeErrno = error;
      diagnostics_.writeBytes = sent; diagnostics_.writeSize = size;
    }
    failed_ = true; abortClient();
  }
  void releaseClient() {
    clearArguments();
    page_ = nullptr;
    persistentCandidate_ = keepAliveResponse_ = idlePersistent_ = false;
    transferConnection_ = false;
    _currentClient.stop();
    _currentClient = NetworkClient();
    _currentStatus = HC_NONE;
    _currentUpload.reset();
    _currentRaw.reset();
  }
  void clearArguments() {
    delete[] _currentArgs;
    _currentArgs = nullptr;
    _currentArgCount = 0;
  }
  void finishResponse() {
    if (!keepAliveResponse_ || failed_) { releaseClient(); return; }
    page_ = nullptr;
    requestUsed_ = 0;
    persistentCandidate_ = keepAliveResponse_ = false;
    idlePersistent_ = true;
    _currentStatus = HC_WAIT_READ;
    _statusChange = millis();
    _currentUpload.reset();
    _currentRaw.reset();
  }
  class RequestClient : public NetworkClient {
  public:
    RequestClient(BoundedWebServer &owner, size_t size)
        : NetworkClient(owner.client()), owner_(owner), size_(size) {
      // clear() is not virtual. Prevent the parser's final clear() from
      // draining the live socket and allocating an RX buffer for trailing data.
      _rxBuffer.reset();
    }
    int available() override { return size_ - position_; }
    int read() override { return position_ < size_ ? uint8_t(owner_.requestBuffer_[position_++]) : -1; }
    int peek() override { return position_ < size_ ? uint8_t(owner_.requestBuffer_[position_]) : -1; }
    int read(uint8_t *data, size_t size) override {
      const size_t count = std::min(size, size_ - position_);
      std::memcpy(data, owner_.requestBuffer_ + position_, count);
      position_ += count;
      return count;
    }
    size_t readBytes(char *data, size_t size) override { return read(reinterpret_cast<uint8_t *>(data), size); }
    size_t write(uint8_t byte) override { return owner_._currentClientWrite(reinterpret_cast<const char *>(&byte), 1); }
    size_t write(const uint8_t *data, size_t size) override {
      return owner_._currentClientWrite(reinterpret_cast<const char *>(data), size);
    }
  private:
    BoundedWebServer &owner_;
    size_t size_, position_ = 0;
  };
  void reject() {
    ++diagnostics_.rejected;
    abortClient();
    releaseClient();
    requestUsed_ = 0;
  }
  void abortClient() {
    const int socket = client().fd();
    if (socket >= 0) {
      // Drop unprocessable connections without retaining TCP send queues.
      linger reset{1, 0};
      ::setsockopt(socket, SOL_SOCKET, SO_LINGER, &reset, sizeof reset);
    }
    client().stop();
  }
  uint32_t startedMs_ = 0;
  bool started_ = false, failed_ = false;
  char *requestBuffer_ = nullptr;
  size_t requestCapacity_ = 0, requestUsed_ = 0;
  const char *const *lowMemoryPaths_ = nullptr;
  size_t lowMemoryPathCount_ = 0;
  const char *page_ = nullptr;
  size_t pageSize_ = 0, pageSent_ = 0;
  uint32_t pageStartedMs_ = 0, pageProgressMs_ = 0, pageAttemptMs_ = 0;
  int pageRetryErrno_ = 0;
  bool persistentCandidate_ = false, keepAliveResponse_ = false, idlePersistent_ = false;
  bool htmlTransfer_ = false;
  bool transferConnection_ = false;
  StopHandler stopHandler_ = nullptr;
  void *stopContext_ = nullptr;
  RequestHandler requestHandler_ = nullptr;
  void *requestContext_ = nullptr;
  HttpDiagnostics diagnostics_;
};
} // namespace forkrot
