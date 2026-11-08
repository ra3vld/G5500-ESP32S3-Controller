// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include <WebServer.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <tcp_peer.hpp>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include "write_budget.hpp"
#include "http_request_gate.hpp"
#include "heap_metrics.hpp"
#include "hardware_config.hpp"
#include "http_diagnostics.hpp"
#include "owner_call.hpp"
namespace forkrot {
class BoundedWebServer : public WebServer {
public:
  using WebServer::WebServer;
  using Executor = void (*)(void *, OwnerCall::Function, void *);
  void setExecutor(Executor executor, void *context) { executor_=executor;executorContext_=context; }
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
  using PriorityHandler = bool (*)(void *, std::string_view, char *, size_t, size_t &, int &, bool &);
  void setPriorityHandler(PriorityHandler handler, void *context) { priorityHandler_=handler;priorityContext_=context; }
  // Runs on the controller thread at safe transport boundaries, never from
  // inside a command handler. It must not re-enter this server.
  using ServiceHandler = void (*)(void *);
  void setServiceHandler(ServiceHandler handler, void *context) { serviceHandler_=handler;serviceContext_=context; }
  std::string_view argumentName(size_t i) const { return {_currentArgs[i].key.c_str(), _currentArgs[i].key.length()}; }
  std::string_view argumentValue(size_t i) const { return {_currentArgs[i].value.c_str(), _currentArgs[i].value.length()}; }
  // Set only after OTA has stopped motion and accepted the upload.
  void holdTransferConnection() { transferConnection_ = true; }
  uint32_t readFailures() const { return diagnostics_.readFailures; }
  uint32_t rejectedRequests() const { return diagnostics_.rejected; }
  uint32_t acceptedRequests() const { return diagnostics_.accepted; }
  const HttpDiagnostics &diagnostics() const { return diagnostics_; }
  unsigned pollIntervalMs() const { return _currentStatus==HC_NONE || idlePersistent_ ? 5 : 1; }
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
    pageServicedMs_ = pageStartedMs_ = pageProgressMs_ = millis();
    pageAttemptMs_ = pageStartedMs_ - 20;
    pageRetryErrno_ = 0;
    pageMemoryWaiting_=false;
  }
  void handleClient() override {
    failed_ = false;
    servicePriority();
    if (draining_) { serviceDrain(); return; }
    if (page_) { servicePage(); return; }
    if (_currentStatus == HC_NONE) {
      if (heapFreeBytes() < 2048 || heapMaxBlockBytes() < 256) return;
      // Move the bounded consumed prefix with its socket. Remaining bytes are
      // still in TCP, so large ordinary requests need no per-client heap copy.
      bool promoted=false;requestUsed_=0;cancelMainMotion_=cancelNextMotion_=false;
      for(auto &slot:priority_)if(slot.client && !slot.replying) {
        if(!requestBuffer_ || slot.used>requestCapacity_) {++diagnostics_.rejected;closePriority(slot,true);continue;}
        if(slot.used)std::memcpy(requestBuffer_,slot.reply,slot.used);
        requestUsed_=slot.used;slot.used=0;
        _currentClient=slot.client;slot.client=NetworkClient();promoted=true;
        cancelMainMotion_=slot.cancelMotion;break;
      }
      if(!_currentClient)_currentClient = _server.accept();
      if (!_currentClient) return;
      if(!promoted)++diagnostics_.accepted;
      _currentClient.setNoDelay(true);
#if FORK_CLASSIC_ESP32 || FORK_C3 || FORK_S2
      limitTcpSocketMss(_currentClient.fd(),536);
#endif
      _currentStatus = HC_WAIT_READ;
      _statusChange = millis();
      pageStartedMs_=0;htmlTransfer_=false;
      persistentCandidate_ = keepAliveResponse_ = idlePersistent_ = false;
    }
    if (_currentStatus == HC_WAIT_READ) {
      promoteReadyRequest();
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
        if (idlePersistent_) { idlePersistent_ = false;cancelMainMotion_=cancelNextMotion_;cancelNextMotion_=false; _statusChange = millis(); }
        requestUsed_ += size_t(count);
      }
      size_t requestSize = 0;
      const auto state = inspectRequest({requestBuffer_, requestUsed_}, requestCapacity_, &requestSize);
      if (state == RequestState::Invalid || state == RequestState::TooLarge) { reject(); return; }
      if (state == RequestState::Incomplete) {
        // A first packet may need TCP retransmission. Ready requests use the
        // bounded auxiliary slots instead of closing this peer after 100 ms.
        if (uint32_t(millis() - _statusChange) >= (transferConnection_ ? 10000u : 4000u)) {
          if(requestUsed_)reject();else releaseClient();
        }
        return; // wait across loop iterations, never inside the stock parser
      }
      // Leave headroom for request strings and handlers. The parser reads the
      // fixed snapshot, avoiding NetworkClient's separate 1436-byte RX malloc.
      const auto stop = emergencyStopRequest({requestBuffer_, requestSize});
      if(cancelMainMotion_ && stop.axis==-2 && motionRequest({requestBuffer_,requestSize})) {
        ++diagnostics_.requests;
        static constexpr char cancelled[]="{\"ok\":false,\"result\":\"cancelled\"}";
        sendFixed(409,"application/json",cancelled,sizeof cancelled-1);
        servicePage();return;
      }
      if (stopHandler_ && stop.axis != -2) {
        StopResult result;
        const uint32_t received=micros();
        onOwner([&] {
          result=stopHandler_(stopContext_, stop);
          if(result.status==200 || result.status==202)recordStopOwner(received);
        }); // no parser/header allocation
        if(stop.axis==-1 && (result.status==200 || result.status==202))cancelPendingMotion();
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
        finishResponse();
        return;
      }
      if (requestHandler_) {
        persistentCandidate_ = persistentOtaRequest({requestBuffer_, requestUsed_}, requestSize) ||
                               persistentStatusRequest({requestBuffer_, requestUsed_}, requestSize);
        keepAliveResponse_ = false;
        _contentLength = CONTENT_LENGTH_NOT_SET;
        _responseCode = 0;
        _clearResponseHeaders();
        serviceControl();
        bool handled=false;
        onOwner([&] { handled=requestHandler_(requestContext_, {requestBuffer_, requestSize}); });
        if (handled) {
          ++diagnostics_.requests;
          if (page_) servicePage();
          else finishResponse();
          return;
        }
      }
      // A binary OTA chunk must never reach WebServer's form parser.
      if (std::string_view(requestBuffer_, requestSize).substr(0, 27) == "POST /api/ota/write?offset=") { reject(); return; }
      if (!requestFitsMemory(requestSize)) { ++diagnostics_.memoryRejects;reject(); return; }
      persistentCandidate_ = persistentStatusRequest({requestBuffer_, requestUsed_}, requestSize);
      keepAliveResponse_ = false;
      const uint32_t parsedReceived=micros();
      RequestClient snapshot(*this, requestSize);
      // All bytes and CRLF boundaries were validated above. This is a memory
      // reader: a short stream timeout measures task preemption, not a slow
      // peer, and spuriously rejects complete commands under CPU load.
      snapshot.setTimeout(5000);
      if (_parseRequest(snapshot)) {
        ++diagnostics_.requests;
        _contentLength = CONTENT_LENGTH_NOT_SET;
        _responseCode = 0;
        _clearResponseHeaders();
        serviceControl();
        const bool parsedStop=uri()=="/api/move" && arg("d")=="S";
        onOwner([&] {
          if (_chain) _chain->runChain(*this, [this]() { return _handleRequest(); });
          else _handleRequest();
          if(parsedStop && (_responseCode==200 || _responseCode==202))recordStopOwner(parsedReceived);
        });
        if(parsedStop && (_responseCode==200 || _responseCode==202))cancelPendingMotion();
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
  void sendFixed(int code, const char *type, const char *data, size_t size, bool allowKeepAlive = false, bool gzip = false, bool immutable = false) {
    _responseCode=code;
    char header[256];
    keepAliveResponse_ = allowKeepAlive && persistentCandidate_ && code == 200;
    const int count = std::snprintf(header, sizeof header,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
        "Cache-Control: %s\r\n%sConnection: %s\r\n\r\n",
        code, code == 200 ? "OK" : "Error", type, unsigned(size),
        immutable ? "public,max-age=31536000,immutable" : "no-store",
        gzip ? "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n" : immutable ? "Vary: Accept-Encoding\r\n" : "",
        keepAliveResponse_ ? "keep-alive" : "close");
    if (count <= 0 || size_t(count) >= sizeof header ||
        _currentClientWrite(header, size_t(count)) != size_t(count)) return;
    if(capturing_ && size<=sizeof deferred_ && deferredSize_<=sizeof deferred_-size) {
      // A short API reply fits one MSS with its header. Copy it while the
      // owner is active, then flush one write on the I/O task instead of
      // spending two Wi-Fi packets/buffers on a tiny JSON response.
      _currentClientWrite(data,size);
      htmlTransfer_=std::strncmp(type,"text/html",9)==0;
      pageSize_=pageSent_=size;pageStartedMs_=millis();
      return;
    }
    page_ = data; pageSize_ = size; pageSent_ = 0;
    htmlTransfer_ = std::strncmp(type,"text/html",9) == 0;
    pageServicedMs_ = pageStartedMs_ = pageProgressMs_ = millis(); pageAttemptMs_ = pageStartedMs_-20;
    pageRetryErrno_ = 0;
    pageMemoryWaiting_=false;
  }
protected:
  size_t _currentClientWrite(const char *data, size_t size) override {
    if (!size || failed_) return 0;
    // Copy stack-backed headers/replies once. Backpressure is handled across
    // I/O turns, just like the borrowed page body, never by a 5 ms busy wait.
    if(size>sizeof deferred_ || deferredSize_>sizeof deferred_-size) {
      if(capturing_)deferredFailed_=true;
      else failWrite("header",EOVERFLOW,deferredSize_,deferredSize_+size);
      return 0;
    }
    if(!deferredSize_)prefixStarted_=prefixProgress_=millis();
    std::memcpy(deferred_+deferredSize_,data,size);deferredSize_+=size;return size;
  }
  size_t _currentClientWrite_P(PGM_P data, size_t size) override {
    return _currentClientWrite(data, size); // ESP32 maps PROGMEM into its address space.
  }
private:
  // A page may have two pending browser reads plus an empty preconnect.
  // Keep one more bounded slot for its foreground STOP/control request.
  struct PriorityClient {
    NetworkClient client;
    // Reuse the request prefix for the reply after dispatch. This replaces the
    // old shared peek buffer: lwIP cannot peek across separate queued pbufs.
    char reply[768];
    size_t size=0,sent=0,used=0;
    uint32_t accepted=0,progress=0;
    bool replying=false,parked=false,cancelMotion=false;
  } priority_[4];
  PriorityHandler priorityHandler_=nullptr;
  void *priorityContext_=nullptr;
  bool cancelMainMotion_=false,cancelNextMotion_=false;
  static bool motionRequest(std::string_view request) {
    const auto first=request.find(' '),end=request.find(' ',first+1);
    const auto url=request.substr(first+1,end-first-1),path=url.substr(0,url.find('?'));
    return path=="/api/move" || path=="/api/target" || path=="/api/mode";
  }
  void cancelPendingMotion() {
    if(page_ || draining_ || idlePersistent_) {
      // The current request already ran. Snapshot whether its persistent
      // successor arrived before the STOP acknowledgement. Keep that barrier
      // across response completion; later input on an empty socket is fresh.
      char first;const int n=::recv(client().fd(),&first,1,MSG_DONTWAIT|MSG_PEEK);
      cancelNextMotion_=n>0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK);
    } else cancelMainMotion_=true;
    for(auto &slot:priority_)if(slot.client && !slot.replying)slot.cancelMotion=true;
  }
  void closePriority(PriorityClient &slot,bool abort=false) {
    if(abort)abortTcpSocket(slot.client.fd());
    slot.client.stop();slot.client=NetworkClient();
  }
  void recordStopOwner(uint32_t received) {
    ++diagnostics_.stopOwnerCount;
    diagnostics_.stopOwnerMaxUs=std::max(diagnostics_.stopOwnerMaxUs,uint32_t(micros()-received));
  }
  void promoteReadyRequest() {
    if(requestUsed_ || idlePersistent_ || transferConnection_)return;
    for(auto &slot:priority_)if(slot.client && slot.parked && !slot.replying) {
      if(!requestBuffer_ || slot.used>requestCapacity_) {++diagnostics_.rejected;closePriority(slot,true);continue;}
      char first;
      if(::recv(_currentClient.fd(),&first,1,MSG_DONTWAIT|MSG_PEEK)>=0 ||
          (errno!=EAGAIN && errno!=EWOULDBLOCK))return;
      // No bytes or response state belong to the empty main connection yet.
      // Exchange ownership with a ready request, retaining its first-byte
      // deadline and cancellation state in the same fixed auxiliary slot.
      auto waiting=_currentClient;_currentClient=slot.client;slot.client=waiting;
      if(slot.used)std::memcpy(requestBuffer_,slot.reply,slot.used);
      requestUsed_=slot.used;slot.used=0;
      const auto accepted=slot.accepted;slot.accepted=slot.progress=_statusChange;
      _statusChange=accepted;
      const bool cancelled=cancelMainMotion_;cancelMainMotion_=slot.cancelMotion;
      slot.cancelMotion=cancelled;slot.parked=false;slot.size=slot.sent=0;
      break;
    }
  }
  void servicePriority() {
    const uint32_t now=millis();
    for(auto &slot:priority_) {
      if(!slot.client) {
        if(!page_ && !draining_ && !(_currentStatus==HC_WAIT_READ && !idlePersistent_ && !transferConnection_))continue;
        slot.client=_server.accept();
        if(!slot.client)continue;
        slot.client.setNoDelay(true);
        limitTcpSocketMss(slot.client.fd(),536);
        ++diagnostics_.accepted;
        slot.size=slot.sent=slot.used=0;slot.replying=slot.parked=slot.cancelMotion=false;
        slot.accepted=slot.progress=now;
      }
      if(!slot.replying && !slot.parked) {
        // Consume fragments into this slot. MSG_PEEK sees only the first lwIP
        // pbuf, even when a later POST-body segment has already been ACKed.
        const int n=::recv(slot.client.fd(),slot.reply+slot.used,sizeof slot.reply-slot.used,MSG_DONTWAIT);
        if(n==0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)) {
          closePriority(slot);continue;
        }
        if(n<=0) {
          if(uint32_t(now-slot.progress)>=4000 || uint32_t(now-slot.accepted)>=15000)closePriority(slot);
          continue;
        }
        slot.used+=size_t(n);slot.progress=now;
        size_t size=0;
        const auto state=inspectRequest({slot.reply,slot.used},sizeof slot.reply,&size);
        if(state==RequestState::Incomplete) {
          if(slot.used==sizeof slot.reply)slot.parked=true;
          else if(uint32_t(now-slot.accepted)>=15000)closePriority(slot,true);
          continue;
        }
        if(state!=RequestState::Ready){slot.parked=true;continue;}
        const std::string_view request(slot.reply,size);
        const auto stop=emergencyStopRequest(request);
        constexpr size_t headerReserve=160;
        char *body=slot.reply+headerReserve;
        size_t bodySize=0;int status=200;bool handled=false,stopsAll=false;
        const uint32_t received=micros();
        onOwner([&] {
          if(stopHandler_ && stop.axis!=-2) {
            const auto result=stopHandler_(stopContext_,stop);status=result.status;
            stopsAll=stop.axis==-1;
            bodySize=size_t(result.status==202
              ? std::snprintf(body,sizeof slot.reply-headerReserve,"{\"ok\":false,\"result\":\"deferred\",\"requestId\":%lu}",static_cast<unsigned long>(result.requestId))
              : std::snprintf(body,sizeof slot.reply-headerReserve,"{\"ok\":%s,\"result\":\"%s\"}",status==200?"true":"false",status==200?"ok":status==409?"cancelled":"unavailable"));
            handled=true;
          } else if(slot.cancelMotion && motionRequest(request)) {
            status=409;bodySize=size_t(std::snprintf(body,sizeof slot.reply-headerReserve,"{\"ok\":false,\"result\":\"cancelled\"}"));handled=true;
          } else if(priorityHandler_)handled=priorityHandler_(priorityContext_,request,body,sizeof slot.reply-headerReserve,bodySize,status,stopsAll);
          if(handled && stopsAll && (status==200 || status==202))recordStopOwner(received);
        });
        // Socket peeking belongs to the I/O thread after the owner applied STOP.
        if(handled && stopsAll && (status==200 || status==202))cancelPendingMotion();
        if(!handled){slot.parked=true;continue;}
        // Dispatch exactly once, even when the reader subsequently stalls.
        slot.replying=true;slot.progress=now;
        ++diagnostics_.requests;++diagnostics_.priorityRequests;
        const int header=std::snprintf(slot.reply,headerReserve,
          "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
          status,status==200?"OK":status==202?"Accepted":"Error",unsigned(bodySize));
        if(header<=0 || size_t(header)>=headerReserve || bodySize>sizeof slot.reply-headerReserve) {
          closePriority(slot,true);continue;
        }
        std::memmove(slot.reply+header,body,bodySize);slot.size=size_t(header)+bodySize;
        // Trailing pipelined commands are not executed on a closing socket.
        slot.used=0;
      }
      if(!slot.replying)continue;
      if(uint32_t(now-slot.progress)>=4000 || uint32_t(now-slot.accepted)>=15000) {
        ++diagnostics_.writeFailures;closePriority(slot,true);continue;
      }
      if(slot.sent<slot.size) {
        const int n=::send(slot.client.fd(),slot.reply+slot.sent,slot.size-slot.sent,MSG_DONTWAIT);
        if(n>0){slot.sent+=size_t(n);slot.progress=now;}
        else if(n<0 && !retryableWrite(errno)){++diagnostics_.writeFailures;closePriority(slot,true);continue;}
        else ++diagnostics_.writeRetries;
      }
      if(slot.sent==slot.size && !tcpPeerPendingBytes(slot.client.fd()))closePriority(slot);
    }
  }
  template<class Function> void onOwner(Function function) {
    if(!executor_) {function();return;}
    struct Invocation { BoundedWebServer *server; Function *function; } invocation{this,&function};
    executor_(executorContext_,[](void *context) {
      auto &call=*static_cast<Invocation *>(context);
      call.server->capturing_=true;
      (*call.function)();
      call.server->capturing_=false;
    },&invocation);
    if(deferredFailed_) {
      deferredFailed_=false;deferredSize_=0;failWrite("header",EOVERFLOW,0,sizeof deferred_);return;
    }
    // Only the I/O task flushes the retained bytes after the callback returns.
  }
  // The independent owner continues ticking while this task waits for TCP.
  void serviceControl() { if(!executor_ && serviceHandler_)serviceHandler_(serviceContext_); }
  Executor executor_=nullptr;
  void *executorContext_=nullptr;
  char deferred_[512]{};
  size_t deferredSize_=0,prefixSent_=0;
  uint32_t prefixStarted_=0,prefixProgress_=0;
  bool servicePrefix() {
    if(!deferredSize_)return true;
    const auto now=millis();
    if(uint32_t(now-prefixStarted_)>=15000 || uint32_t(now-prefixProgress_)>=4000) {
      failWrite("header",ETIMEDOUT,prefixSent_,deferredSize_);return false;
    }
    const int count=::send(client().fd(),deferred_+prefixSent_,deferredSize_-prefixSent_,MSG_DONTWAIT);
    if(count>0) {
      prefixSent_+=size_t(count);prefixProgress_=now;
      if(prefixSent_==deferredSize_) {
        diagnostics_.prefixMaxMs=std::max(diagnostics_.prefixMaxMs,uint32_t(now-prefixStarted_));
        prefixSent_=deferredSize_=0;return true;
      }
    } else if(count<0 && !retryableWrite(errno))failWrite("header",errno,prefixSent_,deferredSize_);
    else ++diagnostics_.writeRetries;
    return false;
  }
  bool capturing_=false,deferredFailed_=false;
  ServiceHandler serviceHandler_=nullptr;
  void *serviceContext_=nullptr;
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
    if(!servicePrefix()){if(failed_)releaseClient();return;}
    const uint32_t now = millis();
    if(shedQueuedBulk(now))return;
    const uint32_t elapsed=now-pageServicedMs_;pageServicedMs_=now;
    const uint32_t idleLimit = htmlTransfer_ ? 10000 : 4000;
    const uint32_t totalLimit = htmlTransfer_ ? 30000 : 15000;
    // Local throttling may postpone the idle deadline, but must not hold the
    // only HTTP request slot forever while browsers retry behind it.
    if (uint32_t(now-pageStartedMs_) >= totalLimit) {
      failWrite("total",pageRetryErrno_?pageRetryErrno_:ETIMEDOUT,pageSent_,pageSize_);
      releaseClient();return;
    }
    if(pageSize_-pageSent_>256&&!networkBulkTxBudget(pageSize_-pageSent_)&&tcpPeerFrameCapacity(client().fd())!=0) {
      // Do not classify our own memory throttle as an unresponsive reader.
      memoryWaitMs_+=elapsed;diagnostics_.memoryWaitMs+=elapsed;
      diagnostics_.memoryWaitMaxMs=std::max(diagnostics_.memoryWaitMaxMs,memoryWaitMs_);
      diagnostics_.memoryWaitFreeMin=std::min(diagnostics_.memoryWaitFreeMin,heapFreeBytes());
      diagnostics_.memoryWaitBlockMin=std::min(diagnostics_.memoryWaitBlockMin,heapMaxBlockBytes());
      pageProgressMs_+=elapsed;return;
    }
    memoryWaitMs_=0;
    // Allow the initial 3s TCP retransmission for both pages and JSON. Closing
    // JSON after 2s dropped its tail before a lost packet could be retried.
    if (uint32_t(now - pageProgressMs_) >= idleLimit) {
      failWrite("idle",pageRetryErrno_ ? pageRetryErrno_ : ETIMEDOUT, pageSent_, pageSize_);
      releaseClient(); return;
    }
    // A healthy heap can drain at loop speed. Retain conservative pacing when
    // radio/BT traffic leaves little RAM, and never spin waiting for TCP space.
    const uint32_t interval = networkBulkTxReady() ? 5 : 20;
    const uint32_t attemptElapsed=uint32_t(now-pageAttemptMs_);
    if (attemptElapsed < interval) return;
    pageAttemptMs_ = now;
    // Small Wi-Fi pools have at most eight dynamic TX buffers. Bound both the
    // packet and burst size even when the general heap looks healthy; filling
    // that pool with bulk data delays ACK/control traffic and triggers retries.
#if FORK_CLASSIC_ESP32 || FORK_C3 || FORK_S2
    constexpr size_t chunkLimit=512;
    // A long renderer pause can catch up in one bounded turn. At normal loop
    // cadence leave most of the radio pool free for ACKs, HUD and control.
    const size_t turnLimit=attemptElapsed>=50?4096:1024;
#else
    constexpr size_t chunkLimit=TCP_MSS,turnLimit=4096;
#endif
    size_t budget=turnLimit;
    for(unsigned attempt=0;attempt<8 && budget;++attempt) {
      serviceControl();
      size_t chunk = std::min({pageSize_ - pageSent_, budget, chunkLimit});
      if(pageRetryErrno_==ENOMEM || pageRetryErrno_==ENOBUFS)chunk=std::min(chunk,size_t(512));
      // Small control/recovery replies remain writable below the bulk reserve.
      if(pageSize_-pageSent_>256)chunk=networkBulkTxBudget(chunk);
#if FORK_CLASSIC_ESP32 || FORK_C3 || FORK_S2
      if(chunk&&pageSize_-pageSent_>256) {
        const auto pending=tcpPeerPendingBytes(client().fd());
        // TLS leaves little room for TCP's queued copies plus later radio
        // allocations. Stop admitting bulk before a slow reader consumes
        // the reserve and forces shedQueuedBulk() to abort its response.
        // Leave room for three later segments to elicit duplicate ACKs after
        // one loss (fast retransmit). A two-segment cap forces a multi-second
        // RTO even when the peer's receive window and our heap are available.
        const size_t queueLimit=heapFreeBytes()<49152?4*536:4096;
        if(pending!=size_t(-1))chunk=std::min(chunk,pending<queueLimit?queueLimit-pending:size_t(0));
      }
#endif
      if(!chunk)break;
      const int count = ::send(client().fd(), page_ + pageSent_, chunk, MSG_DONTWAIT);
      if (count > 0) {
        pageSent_ += size_t(count);
        budget -= size_t(count);
        pageProgressMs_ = now;
        pageRetryErrno_ = 0;
        if (pageSent_ == pageSize_) { finishResponse(); break; }
        if(!networkBulkTxReady())break; // one small write per turn under pressure
      } else {
        ++diagnostics_.writeRetries;
        if(count<0) {
          pageRetryErrno_ = errno;
          if (!retryableWrite(pageRetryErrno_)) {
            failWrite("socket", pageRetryErrno_, pageSent_, pageSize_); releaseClient();
          }
        }
        break;
      }
    }
  }
  static bool retryableWrite(int error) {
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR || error == ENOMEM || error == ENOBUFS;
  }
  void failWrite(const char *cause, int error, size_t sent, size_t size) {
    if (!failed_) {
      ++diagnostics_.writeFailures;
      diagnostics_.writeCause = cause; diagnostics_.writeErrno = error;
      diagnostics_.writeBytes = sent; diagnostics_.writeSize = size;
      if(htmlTransfer_) {
        ++diagnostics_.htmlFailures;diagnostics_.htmlCause=cause;diagnostics_.htmlErrno=error;
        diagnostics_.htmlBytes=sent;diagnostics_.htmlSize=size;
      }
    }
    failed_ = true; abortClient();
  }
  void releaseClient() {
    memoryWaitMs_=0;
    deferredSize_=prefixSent_=0;
    draining_=false;
    pageMemoryWaiting_=false;
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
    if(failed_){releaseClient();return;}
    page_=nullptr;draining_=true;
    drainAt_=drainProgressAt_=millis();drainBytes_=size_t(-1);
    if(!pageStartedMs_)pageStartedMs_=drainAt_;
    serviceDrain();
  }
  void serviceDrain() {
    if(!servicePrefix()){if(failed_)releaseClient();return;}
    const uint32_t now=millis();
    if(shedQueuedBulk(now))return;
    const size_t pending=tcpPeerPendingBytes(client().fd());
    if(pending) {
      if(pending<drainBytes_){drainBytes_=pending;drainProgressAt_=now;}
      if(uint32_t(now-pageStartedMs_)>=(htmlTransfer_?30000u:15000u) ||
         uint32_t(now-drainProgressAt_)>=(htmlTransfer_?10000u:4000u)) {
        failWrite("ack",ETIMEDOUT,pageSent_,pageSize_);releaseClient();
      }
      return;
    }
    draining_=false;
    if(!keepAliveResponse_){releaseClient();return;}
    page_ = nullptr;
    requestUsed_ = 0;
    persistentCandidate_ = keepAliveResponse_ = false;
    idlePersistent_ = true;
    _currentStatus = HC_WAIT_READ;
    _statusChange = millis();
    _currentUpload.reset();
    _currentRaw.reset();
  }
  bool shedQueuedBulk(uint32_t now) {
    // TCP owns another copy after send(). A slow HTTP reader can retain it
    // through a local RAM pause, longer than the recovery guard's two seconds.
    // Reclaim that queue before restarting; small command replies keep priority.
    // The larger bulk-send reserve pauses new copies, but does not itself
    // threaten recovery. Keep the accepted response until RAM actually crosses
    // the same critical thresholds as MemoryGuard; still reclaim after 500 ms,
    // before its unchanged two-second recovery deadline.
    const auto pending=pageSize_>256?tcpPeerPendingBytes(client().fd()):0;
    const bool critical=heapFreeBytes()<MemoryGuard::CriticalFree || heapMaxBlockBytes()<MemoryGuard::CriticalBlock;
    if(pending && pending!=size_t(-1) && critical) {
      if(!pageMemoryWaiting_){pageMemoryWaiting_=true;pageMemoryStarted_=now;}
      if(uint32_t(now-pageMemoryStarted_)>=500) {
        failWrite("memory",ENOMEM,pageSent_,pageSize_);releaseClient();return true;
      }
    } else pageMemoryWaiting_=false;
    return false;
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
      abortTcpSocket(socket);
    }
    client().stop();
  }
  bool failed_ = false;
  char *requestBuffer_ = nullptr;
  size_t requestCapacity_ = 0, requestUsed_ = 0;
  const char *const *lowMemoryPaths_ = nullptr;
  size_t lowMemoryPathCount_ = 0;
  const char *page_ = nullptr;
  size_t pageSize_ = 0, pageSent_ = 0;
  uint32_t pageStartedMs_ = 0, pageProgressMs_ = 0, pageAttemptMs_ = 0, pageServicedMs_=0;
  int pageRetryErrno_ = 0;
  bool persistentCandidate_ = false, keepAliveResponse_ = false, idlePersistent_ = false;
  bool htmlTransfer_ = false;
  bool transferConnection_ = false;
  bool draining_=false;
  bool pageMemoryWaiting_=false;
  uint32_t pageMemoryStarted_=0;
  uint32_t memoryWaitMs_=0;
  uint32_t drainAt_=0,drainProgressAt_=0;
  size_t drainBytes_=0;
  StopHandler stopHandler_ = nullptr;
  void *stopContext_ = nullptr;
  RequestHandler requestHandler_ = nullptr;
  void *requestContext_ = nullptr;
  HttpDiagnostics diagnostics_;
};
} // namespace forkrot
