// HTTP transport for the shared device API. GPL-3.0.
#include "build_config.hpp"
#if WEB_INTERFACE_ENABLED
#include "web_server.hpp"
#include "web_page.hpp"
#include "diagnostic_pages.hpp"
#include "web_assets.hpp"
#include "web_encoding.hpp"
namespace forkrot {
bool WebInterface::handleStaticRequest(std::string_view request) {
  if(request.substr(0,8)!="GET /ui/")return false;
  const auto end=request.find(' ',4);
  if(end==request.npos)return false;
  const auto path=request.substr(4,end-4);
  for(const auto &asset:WEB_STATIC_ASSETS)if(path==asset.path) {
    const bool gzip=requestAcceptsGzip(request);
    if(!gzip && !asset.raw) {
      static constexpr char error[]="{\"error\":\"gzip_required\"}";
      web_.sendFixed(406,"application/json",error,sizeof error-1);return true;
    }
    web_.sendFixed(200,asset.type,gzip?reinterpret_cast<const char *>(asset.gzip):asset.raw,
                   gzip?asset.gzipSize:asset.rawSize,false,gzip,true);return true;
  }
  static constexpr char error[]="{\"error\":\"not_found\"}";
  web_.sendFixed(404,"application/json",error,sizeof error-1);return true;
}
void WebInterface::sendHtml(const char *raw, size_t rawSize, const unsigned char *gzip, size_t gzipSize) {
  const String encoding = web_.header("Accept-Encoding");
  web_.sendHeader("Vary", "Accept-Encoding");
#if !WEB_PLAIN_HTML_SUPPORT
  // Call sites omit plaintext references, so gzip-only builds discard it
  // even without link-time optimization.
  (void)raw; (void)rawSize;
  if (!encoding.length() || acceptsGzip(encoding.c_str())) {
    web_.sendHeader("Content-Encoding", "gzip");
    web_.sendPage(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(gzip), gzipSize);
  } else web_.send(406, "application/json", "{\"error\":\"gzip_required\"}");
#else
  if (acceptsGzip(encoding.c_str())) {
    web_.sendHeader("Content-Encoding", "gzip");
    web_.sendPage(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(gzip), gzipSize);
  } else {
    web_.sendPage(200, "text/html; charset=utf-8", raw, rawSize);
  }
#endif
}
void WebInterface::serveApi(std::string_view method,std::string_view path,FormView arguments) {
  JsonWriter j(statusBuffer_,sizeof statusBuffer_);
  int status=200;const char *type="application/json";
  actions_.deviceRequest(method,path,arguments,j,status,type);
  if(!j.good()) {
    static constexpr char error[]="{\"error\":\"response_too_large\"}";
    web_.sendFixed(503,"application/json",error,sizeof error-1);return;
  }
  // During commissioning, the next browser request may be a non-repeatable
  // setup POST. An idle status socket can yield to another client just as the
  // browser reuses it. Announce close up front instead of exposing that race.
  web_.sendFixed(status,type,j.data(),j.size(),!runtime_.setup.required);
}
void WebInterface::setupWeb() {
  web_.setPriorityHandler([](void *context,std::string_view request,char *reply,size_t capacity,size_t &size,int &status,bool &stopsAll) {
    return static_cast<WebInterface *>(context)->handlePriorityRequest(request,reply,capacity,size,status,stopsAll);
  },this);
  web_.setRequestHandler([](void *context,std::string_view request) {
    auto &self=*static_cast<WebInterface *>(context);
    // The frequent polling request needs no decoded arguments or String
    // headers. Use literals: the response overwrites the raw request buffer.
    if(!self.runtime_.recovery && request.substr(0,16)=="GET /api/status ") {
      self.serveApi("GET","/api/status",{});return true;
    }
    return self.handleStaticRequest(request) || self.handleMqttRequest(request) || self.handleHardwareRequest(request) ||
        (!self.runtime_.recovery && self.handleOtaRequest(request));
  },this);
  if(runtime_.recovery) {web_.begin();return;}
  static const char *headers[]={"Accept-Encoding"};web_.collectHeaders(headers,1);
  web_.on("/",HTTP_GET,[this] {web_.sendHeader("Cache-Control","no-store");sendHtml(WEB_PLAIN_HTML_SUPPORT?WEB_PAGE_HTTP:nullptr,WEB_PLAIN_HTML_SUPPORT?sizeof WEB_PAGE_HTTP-1:0,WEB_PAGE_GZIP,sizeof WEB_PAGE_GZIP);});
  web_.on("/setup",HTTP_GET,[this] {web_.sendHeader("Cache-Control","no-store");sendHtml(WEB_PLAIN_HTML_SUPPORT?WEB_PAGE_HTTP:nullptr,WEB_PLAIN_HTML_SUPPORT?sizeof WEB_PAGE_HTTP-1:0,WEB_PAGE_GZIP,sizeof WEB_PAGE_GZIP);});
  web_.on("/diaglog",HTTP_GET,[this] {sendHtml(WEB_PLAIN_HTML_SUPPORT?DIAG_PAGE_HTTP:nullptr,WEB_PLAIN_HTML_SUPPORT?sizeof DIAG_PAGE_HTTP-1:0,DIAG_PAGE_GZIP,sizeof DIAG_PAGE_GZIP);});
  web_.on("/resetdiag",HTTP_GET,[this] {sendHtml(WEB_PLAIN_HTML_SUPPORT?RESET_DIAG_PAGE_HTTP:nullptr,WEB_PLAIN_HTML_SUPPORT?sizeof RESET_DIAG_PAGE_HTTP-1:0,RESET_DIAG_PAGE_GZIP,sizeof RESET_DIAG_PAGE_GZIP);});
  web_.on("/adc",HTTP_GET,[this] {sendHtml(WEB_PLAIN_HTML_SUPPORT?ADC_TEST_PAGE_HTTP:nullptr,WEB_PLAIN_HTML_SUPPORT?sizeof ADC_TEST_PAGE_HTTP-1:0,ADC_TEST_PAGE_GZIP,sizeof ADC_TEST_PAGE_GZIP);});
  static const char *const apiPaths[] = {
    "/api/setup",
    "/api/motion/coast",
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
    "/api/mqtt",
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
    "/api/speed",
    "/api/target",
    "/api/wifi",
    "/api/config",
    "/api/diagnostics",
    "/api/network",
    "/api/network/confirm",
    "/api/network/rollback"};
#if ESP32_SPP_PROFILE
  web_.onNotFound([this] {
    const String uri=web_.uri();
    if(web_.method()==HTTP_GET && (uri=="/settings" || uri=="/calibration" || uri=="/network")) {
      web_.sendHeader("Cache-Control","no-store");
      if(uri=="/settings")sendHtml(WEB_PLAIN_HTML_SUPPORT?ESSENTIAL_SETTINGS_PAGE_HTTP:nullptr,WEB_PLAIN_HTML_SUPPORT?sizeof ESSENTIAL_SETTINGS_PAGE_HTTP-1:0,ESSENTIAL_SETTINGS_PAGE_GZIP,sizeof ESSENTIAL_SETTINGS_PAGE_GZIP);
      else if(uri=="/network")sendHtml(WEB_PLAIN_HTML_SUPPORT?ESSENTIAL_NETWORK_PAGE_HTTP:nullptr,WEB_PLAIN_HTML_SUPPORT?sizeof ESSENTIAL_NETWORK_PAGE_HTTP-1:0,ESSENTIAL_NETWORK_PAGE_GZIP,sizeof ESSENTIAL_NETWORK_PAGE_GZIP);
      else sendHtml(WEB_PLAIN_HTML_SUPPORT?ESSENTIAL_CALIBRATION_PAGE_HTTP:nullptr,WEB_PLAIN_HTML_SUPPORT?sizeof ESSENTIAL_CALIBRATION_PAGE_HTTP-1:0,ESSENTIAL_CALIBRATION_PAGE_GZIP,sizeof ESSENTIAL_CALIBRATION_PAGE_GZIP);
      return;
    }
    const char *path=nullptr;
    for(const char *candidate:apiPaths)if(uri==candidate){path=candidate;break;}
    if(!path){web_.send(404,"application/json","{\"error\":\"not_found\"}");return;}
    std::array<FormArgument,HTTP_MAX_ARGUMENTS> arguments;
    const size_t count=web_.args();
    if(count>arguments.size()){web_.send(400,"application/json","{\"error\":\"too_many_arguments\"}");return;}
    for(size_t i=0;i<count;++i)arguments[i]={web_.argumentName(i),web_.argumentValue(i)};
    web_.sendHeader("Cache-Control","no-store");
    serveApi(web_.method()==HTTP_GET?"GET":web_.method()==HTTP_POST?"POST":"OTHER",path,{arguments.data(),count});
  });
#else
  for(const char *path:apiPaths) {
    web_.on(path,HTTP_ANY,[this,path] {
      std::array<FormArgument,HTTP_MAX_ARGUMENTS> arguments;
      const size_t count=web_.args();
      if(count>arguments.size()) {web_.send(400,"application/json","{\"error\":\"too_many_arguments\"}");return;}
      for(size_t i=0;i<count;++i)arguments[i]={web_.argumentName(i),web_.argumentValue(i)};
      web_.sendHeader("Cache-Control","no-store");
      serveApi(web_.method()==HTTP_GET?"GET":web_.method()==HTTP_POST?"POST":"OTHER",path,{arguments.data(),count});
    });
  }
#endif
  web_.begin();
  if(!live_.begin())diagnostics_.dbgPrintln("[LIVE] WebSocket unavailable; HTTP fallback");
  camera_.begin();
#if CAMERA_ENABLED
  diagnostics_.dbgPrintln(camera_.ready() ? "[CAMERA] ready: JPEG 640x480 on port 82"
                                        : String("[CAMERA] ")+camera_.error());
#endif
}
}
#endif
