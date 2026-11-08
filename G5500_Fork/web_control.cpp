// Bounded HTTP controls using the same DeviceApi/Controller owner. GPL-3.0.
#include "build_config.hpp"
#if WEB_INTERFACE_ENABLED
#include "web_server.hpp"
#include "form_decode.hpp"
namespace forkrot {
bool WebInterface::handlePriorityRequest(std::string_view request,char *reply,size_t capacity,size_t &size,int &status,bool &stopsAll) {
  stopsAll=false;
  if(runtime_.recovery)return false;
  const auto methodEnd=request.find(' '),urlEnd=request.find(' ',methodEnd+1);
  const auto method=request.substr(0,methodEnd),url=request.substr(methodEnd+1,urlEnd-methodEnd-1);
  const auto query=url.find('?');const auto path=url.substr(0,query);
  // Only bounded, short control replies use this lane. Large status/config
  // snapshots keep the main buffer; settings/NVS retain their normal path.
  if(path!="/api/move" && path!="/api/target" && path!="/api/mode")return false;
  char encoded[385],decoded[385];FormArgument entries[12];size_t count=0,used=0;
  const auto arguments=query==url.npos?std::string_view{}:url.substr(query+1);
  const auto body=request.substr(request.find("\r\n\r\n")+4);
  if(arguments.size()+body.size()+1>=sizeof encoded)return false;
  if(!arguments.empty()){std::memcpy(encoded,arguments.data(),arguments.size());used=arguments.size();}
  if(!body.empty()) {
    if(used)encoded[used++]='&';
    std::memcpy(encoded+used,body.data(),body.size());used+=body.size();
  }
  JsonWriter j(reply,capacity);
  if(!decodeForm({encoded,used},decoded,sizeof decoded,entries,12,count,sizeof decoded-1)) {
    status=400;j << "{\"error\":\"bad_arguments\"}";
  } else {
    const char *type="application/json";
    actions_.deviceRequest(method,path,{entries,count},j,status,type);
    if(path=="/api/move")for(size_t i=0;i<count;++i)
      if(entries[i].first=="d" && entries[i].second=="S")stopsAll=true;
  }
  if(!j.good()) {j=JsonWriter(reply,capacity);status=503;j << "{\"error\":\"response_too_large\"}";}
  size=j.size();return true;
}
}
#endif
