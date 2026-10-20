// HTTP transport for the shared device API. GPL-3.0.
#include "build_config.hpp"
#if WEB_INTERFACE_ENABLED
#include "web_server.hpp"
#include "web_page.hpp"
#include "diagnostic_pages.hpp"
#include "web_assets.hpp"
#include "web_encoding.hpp"
namespace forkrot {
void WebInterface::sendHtml(const char *raw, size_t rawSize, const unsigned char *gzip, size_t gzipSize) {
  const String encoding = web_.header("Accept-Encoding");
  web_.sendHeader("Vary", "Accept-Encoding");
  if (acceptsGzip(encoding.c_str())) {
    web_.sendHeader("Content-Encoding", "gzip");
    web_.sendPage(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(gzip), gzipSize);
  } else {
    web_.sendPage(200, "text/html; charset=utf-8", raw, rawSize);
  }
}
void WebInterface::serveApi(std::string_view method,std::string_view path,FormView arguments,bool fixed) {
  JsonWriter j(statusBuffer_,sizeof statusBuffer_);
  int status=200;const char *type="application/json";
  actions_.deviceRequest(method,path,arguments,j,status,type);
  if(!j.good()) {
    static constexpr char error[]="{\"error\":\"response_too_large\"}";
    web_.sendFixed(503,"application/json",error,sizeof error-1);return;
  }
  if(fixed)web_.sendFixed(status,type,j.data(),j.size(),true);
  else web_.sendBuffer(status,type,j.data(),j.size());
}
void WebInterface::setupWeb() {
  web_.setRequestHandler([](void *context,std::string_view request) {
    auto &self=*static_cast<WebInterface *>(context);
    // The frequent polling request needs no decoded arguments or String
    // headers. Use literals: the response overwrites the raw request buffer.
    if(!self.runtime_.recovery && request.substr(0,16)=="GET /api/status ") {
      self.serveApi("GET","/api/status",{},true);return true;
    }
    return self.handleHardwareRequest(request) || (!self.runtime_.recovery && self.handleOtaRequest(request));
  },this);
  if(runtime_.recovery) {web_.begin();return;}
  static const char *headers[]={"Accept-Encoding"};web_.collectHeaders(headers,1);
  web_.on("/",HTTP_GET,[this] {web_.sendHeader("Cache-Control","no-store");sendHtml(WEB_PAGE,sizeof WEB_PAGE-1,WEB_PAGE_GZIP,sizeof WEB_PAGE_GZIP);});
  web_.on("/diaglog",HTTP_GET,[this] {sendHtml(DIAG_PAGE,sizeof DIAG_PAGE-1,DIAG_PAGE_GZIP,sizeof DIAG_PAGE_GZIP);});
  web_.on("/resetdiag",HTTP_GET,[this] {sendHtml(RESET_DIAG_PAGE,sizeof RESET_DIAG_PAGE-1,RESET_DIAG_PAGE_GZIP,sizeof RESET_DIAG_PAGE_GZIP);});
  web_.on("/adc",HTTP_GET,[this] {sendHtml(ADC_TEST_PAGE,sizeof ADC_TEST_PAGE-1,ADC_TEST_PAGE_GZIP,sizeof ADC_TEST_PAGE_GZIP);});
  for(const char *path : {
    "/api/adcdiag",
    "/api/ap",
    "/api/azcal",
    "/api/azmode",
    "/api/azrange",
    "/api/cal",
    "/api/cal2",
    "/api/calmode",
    "/api/calreset",
    "/api/device",
    "/api/diaglog",
    "/api/diaglog/clear",
    "/api/elcal",
    "/api/elrange",
    "/api/execute",
    "/api/hardware/probe",
    "/api/hud/command",
    "/api/hud/discover",
    "/api/hud/peers",
    "/api/hud/select",
    "/api/mode",
    "/api/motion",
    "/api/motion/autotune",
    "/api/motion/reset",
    "/api/move",
    "/api/preset",
    "/api/preset/run",
    "/api/resethistory",
    "/api/resethistory/clear",
    "/api/resetreason",
    "/api/rssi",
    "/api/settings",
    "/api/status",
    "/api/target",
    "/api/wifi",
    "/api/config",
    "/api/diagnostics",
    "/api/network"}) {
    web_.on(path,HTTP_ANY,[this,path] {
      std::array<FormArgument,HTTP_MAX_ARGUMENTS> arguments;
      const size_t count=web_.args();
      if(count>arguments.size()) {web_.send(400,"application/json","{\"error\":\"too_many_arguments\"}");return;}
      for(size_t i=0;i<count;++i)arguments[i]={web_.argumentName(i),web_.argumentValue(i)};
      web_.sendHeader("Cache-Control","no-store");
      serveApi(web_.method()==HTTP_GET?"GET":web_.method()==HTTP_POST?"POST":"OTHER",path,{arguments.data(),count});
    });
  }
  web_.begin();
  if(!live_.begin())diagnostics_.dbgPrintln("[LIVE] WebSocket unavailable; HTTP fallback");
}
}
#endif
