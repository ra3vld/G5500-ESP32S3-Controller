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
inline unsigned parserPreemptionMs=0;
inline TestSocket testSocket;
inline TestSocket extraSockets[4];
inline TestSocket &socketFor(int fd) { return fd>=2?extraSockets[fd-2]:testSocket; }
inline void delay(unsigned ms) { fakeHardware.now+=ms; }
class NetworkClient {
  int socket_=0;
  unsigned timeout_=1000;
protected:
  std::shared_ptr<int> _rxBuffer;
public:
  NetworkClient()=default;
  explicit NetworkClient(int socket):socket_(socket),_rxBuffer(std::make_shared<int>(0)){}
  virtual ~NetworkClient()=default;
  virtual int available() { ++testSocket.heapBufferReads; return 0; }
  virtual int read() { ++testSocket.heapBufferReads; return -1; }
  virtual int read(uint8_t *,size_t) { ++testSocket.heapBufferReads; return 0; }
  virtual size_t readBytes(char *,size_t) { ++testSocket.heapBufferReads; return 0; }
  virtual int peek() { ++testSocket.heapBufferReads; return -1; }
  virtual size_t write(uint8_t) { return 0; }
  virtual size_t write(const uint8_t *,size_t) { return 0; }
  int fd() const { return socket_ && socketFor(socket_).open ? socket_ : -1; }
  explicit operator bool() const { return fd()>=0; }
  void stop() { if(socket_)socketFor(socket_).open=false; socket_=0; }
  bool isSSE() const { return false; }
  void setTimeout(unsigned value) {timeout_=value;}
  unsigned getTimeout() const {return timeout_;}
  int setNoDelay(bool enabled) { socketFor(socket_).noDelay=enabled;return 0; }
  void clear() { if (_rxBuffer) ++testSocket.heapBufferReads; }
};
class NetworkServer {
public:
  bool hasClient() {
    if(testSocket.pending)return true;
    for(const auto &socket:extraSockets)if(socket.pending)return true;
    return false;
  }
  NetworkClient accept() {
    for(int fd=1;fd<=5;++fd) {
      auto &socket=socketFor(fd);
      if(!socket.pending || socket.open)continue;
      socket.pending=false;socket.open=true;return NetworkClient(fd);
    }
    return NetworkClient();
  }
};
class WebServer {
public:
  explicit WebServer(int){}
  virtual ~WebServer()=default;
  virtual void handleClient() { ++testSocket.baseHandled; }
  void send(int,const char *,const char *) { assert(false && "unexpected stock response"); }
  NetworkClient &client() { return _currentClient; }
  String uri() const {
    const auto &r=testSocket.parsedRequest;const auto at=r.find(' ')+1;
    return String(r.substr(at,r.find_first_of(" ?",at)-at));
  }
  String arg(const char *name) const {
    const auto &r=testSocket.parsedRequest;const auto body=r.find("\r\n\r\n");
    const auto key=std::string(name)+"=";const auto at=r.find(key,body==r.npos?0:body+4);
    if(at==r.npos)return String();
    const auto value=at+key.size();return String(r.substr(value,r.find('&',value)-value));
  }

protected:
  virtual size_t _currentClientWrite(const char *,size_t n) { return n; }
  virtual size_t _currentClientWrite_P(PGM_P,size_t n) { return n; }
  void _prepareHeader(String &out,int,const char *,size_t size) {
    out=String("HTTP/1.1 200 OK\r\nContent-Length: ")+String(size)+"\r\nConnection: close\r\n\r\n";
  }
  bool _parseRequest(NetworkClient &input) {
    ++testSocket.parsed;
    // Model the SDK line reader deadline while its task is preempted. The
    // complete request is already in RequestClient, not arriving on TCP.
    if(parserPreemptionMs) {
      delay(parserPreemptionMs);
      if(parserPreemptionMs>=input.getTimeout()) {
        static constexpr char error[]="HTTP/1.1 408 Request Timeout\r\nContent-Length: 0\r\n\r\n";
        _currentClientWrite(error,sizeof error-1);return false;
      }
    }
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
