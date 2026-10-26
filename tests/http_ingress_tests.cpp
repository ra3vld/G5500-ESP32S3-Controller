#include "bounded_web_server.hpp"
#include "web_page.hpp"
#include "web_assets.hpp"
#include "web_encoding.hpp"
#include "hardware_form.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main() {
  char buffer[5120];
  BoundedWebServer web(80);web.setRequestBuffer(buffer,sizeof buffer);
  static const char *const lowMemoryPaths[]={"/", "/api/status"};
  web.setLowMemoryPaths(lowMemoryPaths,2);
  const auto request=std::string("GET /api/status HTTP/1.1\r\nHost: esp32\r\nConnection: close\r\n\r\n");
  auto connect=[&](const std::string &input) {
    testSocket={};testSocket.input=input;testSocket.pending=true;
    fakeHeapFree=32768;fakeHeapBlock=16384;
  };
  // A browser's unused preconnect must yield to a queued manual command.
  connect("");web.handleClient();assert(testSocket.open);
  testSocket.pending=true;web.handleClient();assert(!testSocket.open && fakeHardware.now==0);
  testSocket.pending=false;
  connect(request.substr(0,10));
  web.handleClient();assert(testSocket.parsed==0 && fakeHardware.now==0);
  testSocket.pending=true;web.handleClient();assert(testSocket.open); // Keep partial requests.
  testSocket.pending=false;
  testSocket.input=request;
  web.handleClient();assert(testSocket.handled==1 && testSocket.heapBufferReads==0);
  connect("POST /api/wifi HTTP/1.1\r\nContent-Length: 8\r\n\r\nssid=");
  web.handleClient();assert(testSocket.parsed==0);
  testSocket.input+="abc";web.handleClient();assert(testSocket.handled==1 && testSocket.heapBufferReads==0);
  connect(request);fakeHeapFree=3000;
  web.handleClient();assert(testSocket.parsed==0 && testSocket.reset && web.rejectedRequests()==1);
  connect("POST /api/settings HTTP/1.1\r\nContent-Length: 0\r\n\r\n");fakeHeapBlock=3000;
  web.handleClient();assert(testSocket.parsed==0 && testSocket.reset && web.rejectedRequests()==2);
  connect(request.substr(0,10));web.handleClient();
  fakeHardware.now+=999;web.handleClient();assert(!testSocket.reset);
  ++fakeHardware.now;web.handleClient();assert(testSocket.reset && testSocket.parsed==0);
  connect("GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
  web.handleClient();assert(testSocket.reset && testSocket.parsed==0);
  connect(std::string(4096,'x'));web.handleClient();assert(testSocket.reset && testSocket.parsed==0);
  connect(request);testSocket.readError=true;web.handleClient();assert(web.readFailures()==1 && testSocket.reset);
  connect(request.substr(0,10));web.handleClient();testSocket.peerClosed=true;
  web.handleClient();assert(testSocket.reset && testSocket.parsed==0);
  connect("POST /api/wifi HTTP/1.1\r\nX: value\rContent-Length: 99999\r\n\r\n");
  web.handleClient();assert(testSocket.reset && testSocket.parsed==0);
  connect(request+request);web.handleClient();
  assert(testSocket.parsedRequest==request && testSocket.heapBufferReads==0);
  connect(request);testSocket.respond=true;testSocket.blockWrites=true;
  const auto beforeWrite=fakeHardware.now;
  web.handleClient();assert(web.writeFailed() && testSocket.reset && fakeHardware.now-beforeWrite==5);
  // A full hardware map, local/sensor settings and UART parameters fit the bounded parser.
  std::string allFields;
  for(unsigned i=0;i<96;++i)allFields+=(i?"&":"")+std::string("p")+std::to_string(i)+"=1";
  const auto hardwarePost=[&](const std::string &body){return "POST /api/hardware HTTP/1.1\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;};
  assert(inspectRequest(hardwarePost(allFields),sizeof buffer)==RequestState::Ready);
  assert(inspectRequest(hardwarePost(allFields+"&overflow=1"),sizeof buffer)==RequestState::TooLarge);
  // Parameter amplification must be rejected before the stock parser.
  connect("POST /api/settings HTTP/1.1\r\nContent-Length: 1002\r\n\r\nx="+std::string(1000,'&'));
  web.handleClient(); assert(testSocket.parsed==0 && testSocket.reset);
  connect("GET /api/settings?x="+std::string(129,'x')+" HTTP/1.1\r\n\r\n");
  web.handleClient(); assert(testSocket.parsed==0 && testSocket.reset);
  std::string longForm;
  for(unsigned i=0;i<24;++i) longForm += (i?"&x":"x") + std::to_string(i) + "=" + std::string(100,'a');
  const auto largePost="POST /api/settings HTTP/1.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: "+
      std::to_string(longForm.size())+"\r\n\r\n"+longForm;
  connect(largePost);fakeHeapFree=9000;web.handleClient();assert(testSocket.parsed==0 && testSocket.reset);
  connect(largePost);web.handleClient();assert(testSocket.handled==1 && !testSocket.reset);
  int stopped = -2;
  web.setStopHandler(
      [](void *context, const StopRequest &request) {
        *static_cast<int *>(context) = request.axis;
        return BoundedWebServer::StopResult{};
      },
      &stopped);
  connect("GET /api/move?d=S HTTP/1.1\r\nHost: esp32\r\n\r\n");
  fakeHeapFree=3000; fakeHeapBlock=512;
  web.handleClient(); assert(stopped==-1 && testSocket.parsed==0);
  assert(testSocket.output.find("{\"ok\":true,\"result\":\"ok\"}")!=std::string::npos);
  connect("GET /api/move?d=A HTTP/1.1\r\n\r\n"); fakeHeapFree=3000; fakeHeapBlock=512;
  web.handleClient(); assert(stopped==0 && testSocket.parsed==0);
  connect("GET /api/move?d=E HTTP/1.1\r\n\r\n"); fakeHeapFree=3000; fakeHeapBlock=512;
  web.handleClient(); assert(stopped==1 && testSocket.parsed==0);
  stopped=-2;
  connect("GET /api/move?d=S&d=R HTTP/1.1\r\n\r\n"); fakeHeapFree=3000; fakeHeapBlock=512;
  web.handleClient(); assert(stopped==-2 && testSocket.parsed==0);
  connect("GET /api/move?d=S HTTP/1.1\r\nHost: incomplete");
  web.handleClient(); assert(stopped==-2);
  testSocket.peerClosed=true;web.handleClient();
  web.setStopHandler([](void *, const StopRequest &) { return BoundedWebServer::StopResult{202, 42}; },
                     nullptr);
  connect("GET /api/move?d=S HTTP/1.1\r\n\r\n");
  fakeHeapFree = 3000;
  fakeHeapBlock = 512;
  web.handleClient();
  assert(testSocket.output.find("202 Accepted") != std::string::npos);
  assert(testSocket.output.find("\"ok\":false") != std::string::npos);
  assert(testSocket.output.find("\"requestId\":42") != std::string::npos);
  web.setStopHandler(
      [](void *, const StopRequest &r) {
        assert(r.scoped && r.selection == 7);
        return BoundedWebServer::StopResult{409, 0};
      },
      nullptr);
  connect("GET /api/move?d=S&selection=7 HTTP/1.1\r\n\r\n");
  fakeHeapFree = 3000;
  fakeHeapBlock = 512;
  web.handleClient();
  assert(testSocket.parsed == 0 && testSocket.output.find("409 Conflict") != std::string::npos);
  assert(testSocket.output.find("cancelled") != std::string::npos);
  assert(emergencyStopAxis("GET /api/move?d=S&selection=7&d=R HTTP/1.1\r\n\r\n") == -2);
  // The exact fragmented heap reported by the device can serve fixed-buffer
  // status and flash pages, while allocating handlers retain their headroom.
  connect(request);fakeHeapFree=8172;fakeHeapBlock=1524;
  web.handleClient();assert(testSocket.handled==1 && !testSocket.reset);
  connect("GET /api/settings HTTP/1.1\r\nHost: esp32\r\n\r\n");fakeHeapFree=8172;fakeHeapBlock=1524;
  web.handleClient();assert(testSocket.parsed==0 && testSocket.reset);
  connect("GET /api/status HTTP/1.1\r\nX: "+std::string(1600,'x')+"\r\n\r\n");
  fakeHeapFree=8172;fakeHeapBlock=1524;
  web.handleClient();assert(testSocket.parsed==0 && testSocket.reset);
  connect("GET / HTTP/1.1\r\nHost: esp32\r\n\r\n");
  fakeHeapFree=8172;fakeHeapBlock=1524;
  testSocket.handler=[&] { web.sendPage(200,"text/html",WEB_PAGE,sizeof WEB_PAGE-1); };
  const auto beforePage=fakeHardware.now;
  web.handleClient();assert(testSocket.handled==1 && !web.writeFailed());
  const auto bodyStart=testSocket.output.find("\r\n\r\n")+4;
  assert(testSocket.output.find("Content-Length: "+std::to_string(sizeof WEB_PAGE-1))!=std::string::npos);
  // Bulk traffic waits for the control reserve; admitting a fixed-buffer
  // request at low heap must not bypass that reserve.
  fakeHardware.now+=20;web.handleClient();assert(testSocket.output.size()==bodyStart && !web.writeFailed());
  fakeHeapFree=40000;fakeHeapBlock=16384;
  unsigned controlLoops=0;
  while(testSocket.output.size()<bodyStart+sizeof WEB_PAGE-1 && controlLoops<2000) {
    fakeHardware.now+=10;testSocket.sendQuota=256;
    const auto beforeLoop=fakeHardware.now;
    web.handleClient();++controlLoops;
    if(fakeHardware.now!=beforeLoop || web.writeFailed())std::cerr<<"page failure now="<<fakeHardware.now<<" before="<<beforeLoop<<" elapsed="<<fakeHardware.now-beforePage<<" loops="<<controlLoops<<" bytes="<<testSocket.output.size()-bodyStart<<" total="<<sizeof WEB_PAGE-1<<" cause="<<web.diagnostics().writeCause<<"\n";
    assert(fakeHardware.now==beforeLoop && !web.writeFailed());
  }
  assert(fakeHardware.now-beforePage>250 && controlLoops>100);
  assert(testSocket.output.substr(bodyStart)==WEB_PAGE && !testSocket.reset);
  assert(testSocket.heapBufferReads==0 && testSocket.parsed==1);
  assert(testSocket.noDelay);
  // A small transport queue returns ENOMEM until ACKs release it. It must
  // delay transmission instead of resetting the connection at the first miss.
  connect("GET / HTTP/1.1\r\nHost: esp32\r\n\r\n");
  fakeHeapFree=40000;fakeHeapBlock=16384;testSocket.queueCapacity=1024;
  const char *compressed=reinterpret_cast<const char *>(WEB_PAGE_GZIP);
  testSocket.handler=[&] { web.sendPage(200,"text/html",compressed,sizeof WEB_PAGE_GZIP); };
  const auto failuresBefore=web.diagnostics().writeFailures;
  web.handleClient();
  const auto gzipStart=testSocket.output.find("\r\n\r\n")+4;
  unsigned waiting=0;
  for(unsigned i=0;testSocket.output.size()<gzipStart+sizeof WEB_PAGE_GZIP && i<2000;++i) {
    fakeHardware.now+=5;
    if(i%16==15) testSocket.outstanding=0; // radio/TCP ACK every 80ms
    const auto bytesBefore=testSocket.output.size();
    const auto timeBefore=fakeHardware.now;
    web.handleClient();
    waiting+=bytesBefore==testSocket.output.size();
    assert(fakeHardware.now==timeBefore && !web.writeFailed());
  }
  assert(waiting>20 && web.diagnostics().writeFailures==failuresBefore);
  assert(testSocket.output.substr(gzipStart)==std::string(compressed,sizeof WEB_PAGE_GZIP));
  // A stable 28 KiB heap (or a fragmented heap below the burst threshold)
  // must still deliver a complete page. The former all-or-nothing gate sent
  // its 200/Content-Length header, then no body until the idle deadline.
  for(bool fragmented:{false,true}) {
    connect("GET / HTTP/1.1\r\nHost: esp32\r\n\r\n");
    fakeHeapFree=fragmented?40000:28672;fakeHeapBlock=fragmented?10240:16384;
    testSocket.queueCapacity=1024;
    testSocket.handler=[&] { web.sendPage(200,"text/html",compressed,sizeof WEB_PAGE_GZIP); };
    const auto lowFailures=web.diagnostics().writeFailures;
    const auto started=fakeHardware.now;
    web.handleClient();
    const auto lowStart=testSocket.output.find("\r\n\r\n")+4;
    assert(testSocket.output.size()-lowStart<=512);
    for(unsigned i=0;testSocket.open && i<1500;++i) {
      fakeHardware.now+=20;
      if(i%4==3)testSocket.outstanding=0;
      const auto beforeLoop=fakeHardware.now;
      const auto bytes=testSocket.output.size();
      web.handleClient();
      assert(fakeHardware.now==beforeLoop && !web.writeFailed());
      assert(testSocket.output.size()-bytes<=512);
    }
    assert(!testSocket.open && !testSocket.reset && fakeHardware.now-started<30000);
    assert(web.diagnostics().writeFailures==lowFailures);
    assert(testSocket.output.substr(lowStart)==std::string(compressed,sizeof WEB_PAGE_GZIP));
  }
  // JSON fallback also makes progress below the burst reserve, including a
  // temporary deeper dip after its first bytes. Never restart the response.
  connect(request);fakeHeapFree=28672;fakeHeapBlock=10240;
  testSocket.handler=[&] { web.sendBuffer(200,"application/json",WEB_PAGE,5564); };
  web.handleClient();const auto lowStatusStart=testSocket.output.find("\r\n\r\n")+4;
  assert(testSocket.output.size()-lowStatusStart==512);
  fakeHeapFree=24575;fakeHardware.now+=20;web.handleClient();
  assert(testSocket.output.size()-lowStatusStart==512 && !web.writeFailed());
  fakeHeapFree=28672;fakeHeapBlock=8191;fakeHardware.now+=20;web.handleClient();
  assert(testSocket.output.size()-lowStatusStart==512 && !web.writeFailed());
  fakeHeapBlock=10240;
  for(unsigned i=0;testSocket.open && i<20;++i){fakeHardware.now+=20;web.handleClient();}
  assert(!testSocket.open && !testSocket.reset && !web.writeFailed());
  assert(testSocket.output.substr(lowStatusStart)==std::string(WEB_PAGE,5564));
  // A busy small-screen renderer may return to HTTP only once a second.
  // Drain a bounded burst so the complete gzip arrives before HTML's deadline.
  connect("GET / HTTP/1.1\r\nHost: esp32\r\n\r\n");
  fakeHeapFree=80000;fakeHeapBlock=65536;
  testSocket.handler=[&] { web.sendPage(200,"text/html",compressed,sizeof WEB_PAGE_GZIP); };
  web.handleClient();
  const auto slowStart=testSocket.output.find("\r\n\r\n")+4;
  unsigned slowLoops=0;
  while(testSocket.open && slowLoops<30) {
    fakeHardware.now+=1000;const auto before=fakeHardware.now;const auto bytes=testSocket.output.size();
    web.handleClient();++slowLoops;
    assert(fakeHardware.now==before && testSocket.output.size()-bytes<=4096);
  }
  assert(!web.writeFailed() && !testSocket.open && slowLoops<30);
  assert(testSocket.output.substr(slowStart)==std::string(compressed,sizeof WEB_PAGE_GZIP));
  // Reproduce the device's partial gzip response. TCP retransmission can take
  // 3s initially, followed by a longer backoff. The former 2s deadline killed
  // this response; HTML must survive a 9s stall without blocking the loop.
  connect("GET / HTTP/1.1\r\nHost: esp32\r\n\r\n");
  fakeHeapFree=40000;fakeHeapBlock=16384;
  testSocket.handler=[&] { web.sendPage(200,"text/html; charset=utf-8",compressed,sizeof WEB_PAGE_GZIP); };
  const auto failuresBeforeStall=web.diagnostics().writeFailures;
  web.handleClient();
  const auto recoveryStart=testSocket.output.find("\r\n\r\n")+4;
  while(testSocket.output.size()-recoveryStart<5571) {
    testSocket.sendQuota=5571-(testSocket.output.size()-recoveryStart);
    fakeHardware.now+=20;web.handleClient();
  }
  testSocket.blockWrites=true;
  for(unsigned i=0;i<450;++i) {
    fakeHardware.now+=20;const auto beforeLoop=fakeHardware.now;
    web.handleClient();
    assert(fakeHardware.now==beforeLoop && !web.writeFailed() && testSocket.open);
  }
  testSocket.blockWrites=false;testSocket.sendQuota=size_t(-1);
  // Allow the larger settings page to drain after recovery; the loop remains bounded.
  const auto recoveryDeadline=fakeHardware.now+uint32_t((sizeof WEB_PAGE_GZIP+511)/512)*20+500;
  while(testSocket.open && fakeHardware.now<recoveryDeadline) { fakeHardware.now+=20;web.handleClient(); }
  assert(!testSocket.open && !testSocket.reset && web.diagnostics().writeFailures==failuresBeforeStall);
  assert(testSocket.output.substr(recoveryStart)==std::string(compressed,sizeof WEB_PAGE_GZIP));
  connect(request);testSocket.handler=[&] { web.sendBuffer(200,"application/json",WEB_PAGE,1500); };
  web.handleClient();const auto statusStart=testSocket.output.find("\r\n\r\n")+4;
  for(unsigned i=0;testSocket.output.size()<statusStart+1500 && i<20;++i) {
    fakeHardware.now+=20;web.handleClient();
  }
  assert(testSocket.output.substr(statusStart)==std::string(WEB_PAGE,1500));
  assert(acceptsGzip("gzip, deflate, br") && acceptsGzip("br, GZip;q=0.5"));
  assert(!acceptsGzip("") && !acceptsGzip("br, x-gzip") && !acceptsGzip("gzip;q=0"));
  assert(!acceptsGzip("gzip;q=0.000, br") && !acceptsGzip("gzip;q=1.1"));
  // Progress and total deadlines must close slow/stalled transfers and recover.
  fakeHardware.now=UINT32_MAX-500;
  connect(request);testSocket.handler=[&] { web.sendPage(200,"text/html",WEB_PAGE,sizeof WEB_PAGE-1); };
  web.handleClient();testSocket.blockWrites=true;
  const auto beforeStall=testSocket.output.size()-(testSocket.output.find("\r\n\r\n")+4);
  fakeHardware.now+=9999;web.handleClient();assert(!testSocket.reset);
  ++fakeHardware.now;web.handleClient();assert(testSocket.reset && web.writeFailed());
  assert(std::string(web.diagnostics().writeCause)=="idle" && web.diagnostics().writeErrno==EAGAIN);
  assert(web.diagnostics().writeBytes==beforeStall && web.diagnostics().writeSize==sizeof WEB_PAGE-1);
  connect(request);testSocket.handler=[&] { web.sendPage(200,"text/html",WEB_PAGE,sizeof WEB_PAGE-1); };
  web.handleClient();
  for(unsigned i=0;i<30;++i) {
    fakeHardware.now+=1000;testSocket.sendQuota=1;web.handleClient();
  }
  assert(testSocket.reset && web.writeFailed());
  assert(std::string(web.diagnostics().writeCause)=="total");
  // JSON allows the initial TCP retry at 3s, retaining a shorter bound than
  // HTML and never waiting inside the control loop.
  connect(request);testSocket.sendQuota=512;
  testSocket.handler=[&] { web.sendBuffer(200,"application/json",WEB_PAGE,1500); };
  web.handleClient();testSocket.blockWrites=true;
  fakeHardware.now+=3500;web.handleClient();assert(!testSocket.reset && !web.writeFailed());
  testSocket.blockWrites=false;testSocket.sendQuota=size_t(-1);fakeHardware.now+=5;web.handleClient();
  assert(!web.writeFailed() && testSocket.output.substr(testSocket.output.find("\r\n\r\n")+4)==std::string(WEB_PAGE,1500));
  connect(request);testSocket.sendQuota=512;
  testSocket.handler=[&] { web.sendBuffer(200,"application/json",WEB_PAGE,1500); };
  web.handleClient();testSocket.blockWrites=true;
  fakeHardware.now+=3999;web.handleClient();assert(!testSocket.reset);
  ++fakeHardware.now;web.handleClient();assert(testSocket.reset && web.writeFailed());
  assert(std::string(web.diagnostics().writeCause)=="idle" && web.diagnostics().writeSize==1500);
  for(unsigned i=0;i<10000;++i) {
    connect(request);testSocket.maxRead=7;
    for(unsigned n=0;n<10 && !testSocket.handled;++n) web.handleClient();
    assert(testSocket.handled==1 && testSocket.heapBufferReads==0 && testSocket.baseHandled==0);
  }
  const std::string polling="GET /api/status HTTP/1.1\r\nHost: esp32\r\n\r\n";
  assert(persistentStatusRequest(polling,polling.size()));
  for (const std::string &text : {request,
       std::string("GET /api/status HTTP/1.0\r\n\r\n"),
       std::string("GET / HTTP/1.1\r\n\r\n"),
       std::string("POST /api/status HTTP/1.1\r\n\r\n"),
       std::string("GET /api/status HTTP/1.1\r\ncOnNeCtIoN: keep-alive, CLOSE\r\n\r\n")})
    assert(!persistentStatusRequest(text,text.size()));
  assert(!persistentStatusRequest(polling+polling,polling.size()));
  connect(polling);
  testSocket.handler=[&] { web.sendBuffer(200,"application/json","{\"ok\":true}",11); };
  const auto acceptedBefore=web.acceptedRequests(), requestsBefore=web.diagnostics().requests;
  web.handleClient();
  for(unsigned i=1;i<10000;++i) {
    assert(testSocket.open && !testSocket.reset && !testSocket.pending);
    assert(testSocket.output.find("Connection: keep-alive\r\n")!=std::string::npos);
    testSocket.input=polling;testSocket.consumed=0;testSocket.output.clear();
    fakeHardware.now+=500;web.handleClient();
  }
  assert(web.acceptedRequests()==acceptedBefore+1 && web.diagnostics().requests==requestsBefore+10000);
  assert(testSocket.heapBufferReads==0);
  // A second browser/control client can take over an idle polling connection.
  testSocket.pending=true;web.handleClient();assert(!testSocket.open);
  testSocket.input=polling;testSocket.consumed=0;testSocket.output.clear();web.handleClient();
  assert(testSocket.open && web.acceptedRequests()==acceptedBefore+2);
  // Normal peer EOF between requests is not a rejected HTTP request.
  const auto rejectedBefore=web.rejectedRequests();
  testSocket.peerClosed=true;web.handleClient();
  assert(!testSocket.open && web.rejectedRequests()==rejectedBefore);
  connect(polling);testSocket.handler=[&] { web.sendBuffer(200,"application/json","{}",2); };
  web.handleClient();assert(testSocket.open);
  fakeHardware.now+=1499;web.handleClient();assert(testSocket.open);
  ++fakeHardware.now;web.handleClient();assert(!testSocket.open && !testSocket.reset);
  // If more than one request arrived together, close after the first response.
  connect(polling+polling);testSocket.handler=[&] { web.sendBuffer(200,"application/json","{}",2); };
  web.handleClient();assert(!testSocket.open && testSocket.parsed==1);
  assert(testSocket.output.find("Connection: close\r\n")!=std::string::npos);
  // Binary OTA chunks stay bounded and bypass the form parser. No complete
  // chunk, duplicate length or larger body can slip through a partial envelope.
  std::string chunk(2048,'\0'); chunk[0]=char(0xe9); chunk[100]='&';
  const std::string prefix="POST /api/ota/write?offset=0 HTTP/1.1\r\nContent-Type: application/octet-stream\r\nContent-Length: 2048\r\n\r\n";
  connect(prefix+chunk); web.handleClient(); assert(testSocket.reset && !testSocket.parsed);
  unsigned chunks=0;
  web.setRequestHandler([](void *context,std::string_view raw) {
    assert(raw.substr(raw.find("\r\n\r\n")+4).size()==2048);
    ++*static_cast<unsigned *>(context); return true;
  },&chunks);
  connect(prefix+chunk.substr(0,512)); web.handleClient(); assert(chunks==0 && !testSocket.reset);
  testSocket.input=prefix+chunk; web.handleClient(); assert(chunks==1 && !testSocket.parsed);
  connect("POST /api/settings HTTP/1.1\r\nContent-Length: 2048\r\n\r\n"+chunk);
  web.handleClient(); assert(testSocket.reset && chunks==1);
  connect("POST /api/ota/write?offset=0 HTTP/1.1\r\nContent-Length: 2049\r\n\r\n"+chunk+"a");
  web.handleClient(); assert(testSocket.reset && chunks==1);
  connect("POST /api/ota/write?offset=0 HTTP/1.1\r\nContent-Length: 2048\r\nContent-Length: 1\r\n\r\n"+chunk);
  web.handleClient(); assert(testSocket.reset && chunks==1);
  // An entire OTA upload reuses a single socket, including binary bodies.
  web.setRequestHandler([](void *context,std::string_view) {
    static constexpr char ok[] = "{\"ok\":true}";
    auto &server=*static_cast<BoundedWebServer *>(context);
    server.holdTransferConnection();
    server.sendBuffer(200,"application/json",ok,sizeof ok-1);
    return true;
  },&web);
  connect(prefix+chunk);web.handleClient();assert(testSocket.open && !testSocket.parsed);
  const auto otaAccepted=web.acceptedRequests(),otaRequests=web.diagnostics().requests;
  assert(testSocket.output.find("Connection: keep-alive\r\n")!=std::string::npos);
  for(unsigned i=0;i<1000;++i) {
    testSocket.input+=prefix+chunk;web.handleClient();assert(testSocket.open && !testSocket.parsed);
  }
  assert(web.acceptedRequests()==otaAccepted && web.diagnostics().requests==otaRequests+1000);
  // Another browser poll must not close the socket between upload blocks.
  testSocket.pending=true;web.handleClient();assert(testSocket.open);
  testSocket.pending=false;fakeHardware.now+=9999;web.handleClient();assert(testSocket.open);
  ++fakeHardware.now;web.handleClient();assert(!testSocket.open && !testSocket.reset);
  // The longer response budget applies only after motion is locked for OTA.
  connect(prefix+chunk);testSocket.blockWrites=true;
  const auto transferStarted=fakeHardware.now;web.handleClient();
  assert(web.writeFailed() && fakeHardware.now-transferStarted==100);
  assert(!persistentOtaRequest("POST /api/ota/finish HTTP/1.0\r\n\r\n",37));
  const std::string closeOta="POST /api/ota/finish HTTP/1.1\r\nConnection: close\r\n\r\n";
  assert(!persistentOtaRequest(closeOta,closeOta.size()));
  testSocket.peerClosed=true; web.handleClient(); // close the prior persistent OTA socket
  // A fragmented heap that rejects ordinary settings must still accept the
  // emergency settings POST and transmit its reply without entering the parser.
  struct RecoveryContext { BoundedWebServer *web; HardwareForm form; bool saved=false; } recovery{&web,{},false};
  web.setRequestHandler([](void *context,std::string_view raw) {
    auto &r=*static_cast<RecoveryContext *>(context);
    const auto at=raw.find("\r\n\r\n");
    r.saved=at!=raw.npos && parseHardwareForm(raw.substr(at+4),r.form,true);
    static const char reply[]="{\"ok\":true}";
    r.web->sendFixed(200,"application/json",reply,sizeof reply-1);
    return true;
  },&recovery);
  const std::string settingsBody="token=123&generation=2&driveInterface=2&simulation=1&bluetoothEnabled=0&bluetoothMode=3&forceApSta=0&buttonGpio=-1&buttonActiveLow=1&buttonPullup=0&pwmInverted=1";
  connect("POST /api/hardware HTTP/1.1\r\nContent-Length: "+std::to_string(settingsBody.size())+"\r\n\r\n"+settingsBody);
  fakeHeapFree=3000;fakeHeapBlock=512;
  web.handleClient();
  while(testSocket.open){fakeHardware.now+=20;web.handleClient();}
  assert(recovery.saved && recovery.form.config.drive==2 && recovery.form.config.buttonGpio==-1);
  assert(testSocket.parsed==0 && testSocket.heapBufferReads==0);
  assert(testSocket.output.find("{\"ok\":true}")!=std::string::npos);
  assert(testSocket.output.find("Cache-Control: no-store")!=std::string::npos);
  // Status bypasses the allocating parser and reuses one TCP connection even
  // when the largest free block is smaller than a browser's Cookie header.
  web.setRequestHandler([](void *context,std::string_view raw) {
    if(raw.substr(0,16)!="GET /api/status ")return false;
    auto &server=*static_cast<BoundedWebServer *>(context);
    server.sendFixed(200,"application/json","{}",2,true);return true;
  },&web);
  const std::string fixedPoll="GET /api/status HTTP/1.1\r\nCookie: "+std::string(800,'x')+"\r\n\r\n";
  connect(fixedPoll);fakeHeapFree=3000;fakeHeapBlock=512;
  web.handleClient();const auto fixedAccepted=web.acceptedRequests();
  for(unsigned i=0;i<10000;++i) {
    assert(testSocket.open && testSocket.parsed==0 && !testSocket.reset);
    assert(testSocket.output.find("Connection: keep-alive")!=std::string::npos);
    testSocket.input=fixedPoll;testSocket.consumed=0;testSocket.output.clear();
    fakeHardware.now+=1000;web.handleClient();
  }
  assert(web.acceptedRequests()==fixedAccepted && testSocket.heapBufferReads==0);
  testSocket.peerClosed=true;web.handleClient();
  for(const std::string &closePoll:{request,std::string("GET /api/status HTTP/1.0\r\n\r\n"),fixedPoll+fixedPoll}) {
    connect(closePoll);web.handleClient();assert(!testSocket.open && !testSocket.parsed);
    assert(testSocket.output.find("Connection: close")!=std::string::npos);
  }
  // Raising throughput keeps each burst bounded and never waits for TCP space.
  web.setRequestHandler(nullptr,nullptr);
  auto transferTime=[&](size_t heap) {
    connect(request);fakeHeapFree=heap;
    testSocket.handler=[&] {web.sendBuffer(200,"text/html",WEB_PAGE,8192);};
    const auto start=fakeHardware.now;web.handleClient();
    for(unsigned i=0;testSocket.open && i<1000;++i) {
      const auto bytes=testSocket.output.size();fakeHardware.now+=5;
      if(heap<32768&&i==20)fakeHeapFree=65536;
      const auto now=fakeHardware.now;web.handleClient();
      assert(fakeHardware.now==now && testSocket.output.size()-bytes<=4096);
    }
    assert(!testSocket.open && !testSocket.reset);return uint32_t(fakeHardware.now-start);
  };
  const auto slow=transferTime(16384),fast=transferTime(65536);
  assert(slow>=fast+100);
  std::cout<<"PASS: recovery POST remains writable with free=3000/max=512 while normal settings require larger blocks\n";
  std::cout<<"PASS: 10000 parser-free status polls at free=3000/max=512; bulk waits below control reserve, bounded nonblocking 4KB bursts\n";
  std::cout<<"PASS: complete current page over slow socket after low-heap admission, per-loop control progress, transfer deadlines and recovery\n";
  std::cout<<"PASS: gzip bytes through 1KB queue with repeated ENOMEM/ACKs, paced JSON, encoding negotiation and failure diagnostics\n";
  std::cout<<"PASS: HTTP fragments, complete POST body, low heap, timeouts, socket failures and 10000 requests without NetworkClient RX allocations\n";
  std::cout<<"PASS: 10000 status requests on one connection, HTTP/1.0/close/pipeline fallback, idle/peer-close cleanup and second-client fairness\n";
  std::cout<<"PASS: gzip resumes after a 9s stall at byte 5571; HTML 10s idle/30s total, JSON resumes after 3.5s and expires at 4s, nonblocking control-loop progress\n";
}
