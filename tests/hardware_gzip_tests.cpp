#include "bounded_web_server.hpp"
#include "web_assets.hpp"
#include "web_encoding.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main() {
  assert(requestAcceptsGzip("GET /hardware HTTP/1.1\r\n\r\n"));
  assert(requestAcceptsGzip("GET /hardware HTTP/1.1\r\nAccept-Encoding: gzip, br\r\n\r\n"));
  assert(!requestAcceptsGzip("GET /hardware HTTP/1.1\r\nAccept-Encoding: gzip;q=0\r\n\r\n"));
  assert(!requestAcceptsGzip("GET /hardware HTTP/1.1\r\nAccept-Encoding: identity\r\n\r\n"));
  char buffer[4096];BoundedWebServer web(80);web.setRequestBuffer(buffer,sizeof buffer);
  web.setRequestHandler([](void *ctx,std::string_view request) {
    auto &server=*static_cast<BoundedWebServer *>(ctx);
    assert(requestAcceptsGzip(request));
    server.sendFixed(200,"text/html; charset=utf-8",reinterpret_cast<const char *>(HARDWARE_PAGE_GZIP),sizeof HARDWARE_PAGE_GZIP,false,true);
    return true;
  },&web);
  testSocket={};testSocket.input="GET /hardware HTTP/1.1\r\nAccept-Encoding: gzip\r\n\r\n";testSocket.pending=true;
  fakeHeapFree=32768;fakeHeapBlock=16384;web.handleClient();
  const auto body=testSocket.output.find("\r\n\r\n")+4;
  for(unsigned n=0;n<1000 && testSocket.output.size()<body+sizeof HARDWARE_PAGE_GZIP;++n) {
    fakeHardware.now+=5;testSocket.sendQuota=512;web.handleClient();
  }
  assert(!web.writeFailed() && testSocket.parsed==0);
  assert(testSocket.output.find("Content-Encoding: gzip\r\n")!=std::string::npos);
  assert(testSocket.output.find("Vary: Accept-Encoding\r\n")!=std::string::npos);
  assert(testSocket.output.substr(body)==std::string(reinterpret_cast<const char *>(HARDWARE_PAGE_GZIP),sizeof HARDWARE_PAGE_GZIP));
  std::cout<<"PASS: hardware gzip negotiation, headers and parser-free bounded transfer\n";
}
