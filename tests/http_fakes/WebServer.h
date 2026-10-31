#pragma once
#include "Arduino.h"
#include <cassert>
#include <memory>
#include <cstring>
#include <functional>
using PGM_P=const char *;
inline constexpr size_t CONTENT_LENGTH_NOT_SET=size_t(-2);
enum HTTPClientStatus { HC_NONE, HC_WAIT_READ, HC_WAIT_CLOSE };
struct TestSocket {
  std::string input, output, parsedRequest;
  size_t consumed=0, maxRead=4096;
  size_t sendQuota=size_t(-1);
  size_t outstanding=0, queueCapacity=size_t(-1);
  size_t largestSend=0;
  int writeError=0;
  bool noDelay=false;
  std::function<void()> handler;
  bool pending=false, open=false, reset=false, readError=false, peerClosed=false,
       respond=false, blockWrites=false;
  unsigned heapBufferReads=0, parsed=0, handled=0, baseHandled=0;
};
inline TestSocket testSocket;
inline void delay(unsigned ms) { fakeHardware.now+=ms; }
class NetworkClient {
  bool active_=false;
protected:
  std::shared_ptr<int> _rxBuffer;
public:
  NetworkClient()=default;
  explicit NetworkClient(bool active):active_(active),_rxBuffer(std::make_shared<int>(0)){}
  virtual ~NetworkClient()=default;
  virtual int available() { ++testSocket.heapBufferReads; return 0; }
  virtual int read() { ++testSocket.heapBufferReads; return -1; }
  virtual int read(uint8_t *,size_t) { ++testSocket.heapBufferReads; return 0; }
  virtual size_t readBytes(char *,size_t) { ++testSocket.heapBufferReads; return 0; }
  virtual int peek() { ++testSocket.heapBufferReads; return -1; }
  virtual size_t write(uint8_t) { return 0; }
  virtual size_t write(const uint8_t *,size_t) { return 0; }
  int fd() const { return active_ && testSocket.open ? 1 : -1; }
  explicit operator bool() const { return fd()>=0; }
  void stop() { testSocket.open=false; active_=false; }
  bool isSSE() const { return false; }
  void setTimeout(unsigned) {}
  int setNoDelay(bool enabled) { testSocket.noDelay=enabled;return 0; }
  void clear() { if (_rxBuffer) ++testSocket.heapBufferReads; }
};
class NetworkServer {
public:
  bool hasClient() { return testSocket.pending; }
  NetworkClient accept() {
    if (!testSocket.pending) return NetworkClient();
    testSocket.pending=false;testSocket.open=true;return NetworkClient(true);
  }
};
class WebServer {
public:
  explicit WebServer(int){}
  virtual ~WebServer()=default;
  virtual void handleClient() { ++testSocket.baseHandled; }
  NetworkClient &client() { return _currentClient; }
protected:
  virtual size_t _currentClientWrite(const char *,size_t n) { return n; }
  virtual size_t _currentClientWrite_P(PGM_P,size_t n) { return n; }
  void _prepareHeader(String &out,int,const char *,size_t size) {
    out=String("HTTP/1.1 200 OK\r\nContent-Length: ")+String(size)+"\r\nConnection: close\r\n\r\n";
  }
  bool _parseRequest(NetworkClient &input) {
    ++testSocket.parsed;
    // Exercise all read entry points used by the real WebServer parser.
    assert(input.peek()=='G' || input.peek()=='P');
    const auto first=input.read();assert(first=='G' || first=='P');
    uint8_t second;assert(input.read(&second,1)==1);
    std::string rest(size_t(input.available()),'\0');
    assert(input.readBytes(rest.data(),rest.size())==rest.size());
    testSocket.parsedRequest=std::string(1,char(first))+char(second)+rest;
    assert(input.available()==0 && input.read()==-1);
    input.clear();
    return true;
  }
  bool _handleRequest() {
    ++testSocket.handled;
    if (testSocket.handler) testSocket.handler();
    if (testSocket.respond) _currentClientWrite("reply",5);
    return true;
  }
  void _clearResponseHeaders() {}
  struct RequestArgument { String key, value; };
  RequestArgument *_currentArgs = nullptr;
  int _currentArgCount = 0;
  struct Chain { template<class F> void runChain(WebServer &,F f) { f(); } };
  Chain *_chain=nullptr;
  NetworkServer _server;
  NetworkClient _currentClient;
  HTTPClientStatus _currentStatus=HC_NONE;
  unsigned long _statusChange=0;
  size_t _contentLength=0;
  int _responseCode=0;
  std::unique_ptr<int> _currentUpload,_currentRaw;
};
