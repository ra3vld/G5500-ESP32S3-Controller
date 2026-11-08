// Parser-free hardware HTTP transport. Operations live in DeviceApi. GPL-3.0.
#include "build_config.hpp"
#if WEB_INTERFACE_ENABLED
#include "web_server.hpp"
#include "web_assets.hpp"
#include "web_encoding.hpp"
#include "command_api.hpp"
#include "heap_metrics.hpp"
#include <new>
namespace forkrot {
bool WebInterface::handleMqttRequest(std::string_view request) {
  const auto space=request.find(' '),end=request.find(' ',space+1);
  if(space==request.npos || end==request.npos)return false;
  const auto method=request.substr(0,space),target=request.substr(space+1,end-space-1);
  const auto query=target.find('?');
  if(target.substr(0,query)!="/api/mqtt")return false;
  const bool get=method=="GET";
  if(!get && method!="POST") {
    static constexpr char error[]="{\"error\":\"method_not_allowed\"}";
    web_.sendFixed(405,"application/json",error,sizeof error-1);return true;
  }
  // The hardware form used to leave >3 KiB on the 8 KiB owner stack during
  // NVS/flash calls, overflowing loopTask. Keep this route out of that frame.
  // The decoder exists only for requests with arguments; ordinary GET uses
  // no heap, and OFF/recovery retain no persistent form buffer.
  std::unique_ptr<CommandForm> form;
  if(!get || (query!=target.npos && query+1<target.size())) {
    if(heapFreeBytes()>=sizeof(CommandForm)+MemoryGuard::CriticalFree &&
        heapMaxBlockBytes()>=sizeof(CommandForm)+MemoryGuard::CriticalBlock)
      form.reset(new(std::nothrow) CommandForm);
    if(!form) {
      static constexpr char error[]="{\"error\":\"mqtt_memory_limited\"}";
      web_.sendFixed(503,"application/json",error,sizeof error-1);return true;
    }
    const auto body=request.find("\r\n\r\n");
    const bool valid=get?form->parse(target.substr(query+1)):
        body!=request.npos && form->parse(request.substr(body+4),1536);
    if(!valid) {
      static constexpr char error[]="{\"error\":\"invalid_form\"}";
      web_.sendFixed(400,"application/json",error,sizeof error-1);return true;
    }
  }
  JsonWriter j(statusBuffer_,sizeof statusBuffer_);int status=200;const char *type="application/json";
  actions_.deviceRequest(get?"GET":"POST","/api/mqtt",form?form->view():FormView{},j,status,type);
  if(!j.good()) {
    static constexpr char error[]="{\"error\":\"response_too_large\"}";
    web_.sendFixed(503,"application/json",error,sizeof error-1);return true;
  }
  web_.sendFixed(status,type,j.data(),j.size());return true;
}
bool WebInterface::handleHardwareRequest(std::string_view request) {
  const auto space=request.find(' '),end=request.find(' ',space+1);
  if(space==request.npos || end==request.npos)return false;
  const auto method=request.substr(0,space),target=request.substr(space+1,end-space-1);
  const auto query=target.find('?');
  const auto path=target.substr(0,query);
  if(method=="GET" && (path=="/hardware" || path=="/recovery" || (runtime_.recovery && (path=="/" || path=="/setup")))) {
    if(!requestAcceptsGzip(request)) {
      static constexpr char error[]="{\"error\":\"gzip_required\"}";
      web_.sendFixed(406,"application/json",error,sizeof error-1);return true;
    }
    web_.sendFixed(200,"text/html; charset=utf-8",reinterpret_cast<const char *>(HARDWARE_PAGE_GZIP),sizeof HARDWARE_PAGE_GZIP,false,true);return true;
  }
  if(path=="/api/hardware" || path.substr(0,14)=="/api/hardware/") {
    // The response reuses the raw request buffer. Preserve the route before
    // JsonWriter clears byte zero or writes over the request line.
    char savedMethod[8],savedPath[64];
    if(method.size()>=sizeof savedMethod || path.size()>=sizeof savedPath)return false;
    std::memcpy(savedMethod,method.data(),method.size());savedMethod[method.size()]=0;
    std::memcpy(savedPath,path.data(),path.size());savedPath[path.size()]=0;
    CommandForm form;
    const auto body=request.find("\r\n\r\n");
    const bool valid=method=="GET" ? (query==target.npos || form.parse(target.substr(query+1))) :
        (body!=request.npos && form.parse(request.substr(body+4),1536));
    if(!valid) {static constexpr char error[]="{\"error\":\"invalid_form\"}";web_.sendFixed(400,"application/json",error,sizeof error-1);return true;}
    JsonWriter j(statusBuffer_,sizeof statusBuffer_);int status=200;const char *type="application/json";
    actions_.deviceRequest(savedMethod,savedPath,form.view(),j,status,type);
    if(!j.good()) {static constexpr char error[]="{\"error\":\"response_too_large\"}";web_.sendFixed(503,"application/json",error,sizeof error-1);return true;}
    web_.sendFixed(status,type,j.data(),j.size());return true;
  }
  if(runtime_.recovery) {static constexpr char error[]="{\"error\":\"recovery_only\"}";web_.sendFixed(404,"application/json",error,sizeof error-1);return true;}
  return false;
}
}
#endif
