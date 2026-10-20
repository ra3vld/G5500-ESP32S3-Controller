#include "live_server.hpp"
#include "network_timing.hpp"
#if defined(ARDUINO_ARCH_ESP32) && WEB_INTERFACE_ENABLED
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <cstring>
#include <new>
namespace forkrot {
namespace { struct Guard { SemaphoreHandle_t m; explicit Guard(SemaphoreHandle_t v):m(v){xSemaphoreTake(m,portMAX_DELAY);} ~Guard(){xSemaphoreGive(m);} }; }
bool LiveServer::begin() {
  clients_.reset(new(std::nothrow) Clients{});
  state_.reset(new(std::nothrow) std::array<char,LIVE_STATE_CAPACITY>{});
  if(!clients_ || !state_){clients_.reset();state_.reset();return false;}
  liveBootText(boot_,runtime_.hud.boot);
  ingressRange_[0]=runtime_.controller.azRange;ingressRange_[1]=runtime_.controller.elRange;ingressEl_=runtime_.controller.model==Model::G5500;
  for(unsigned i=0;i<2;++i) ingressReady_[i]=runtime_.controller.axes[i].calibrated &&
    (!runtime_.controller.variableSpeed(i) || runtime_.controller.motionSettings[i].ready);
  mutex_=xSemaphoreCreateMutexStatic(&mutexStorage_);
  httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
  cfg.server_port=81; cfg.ctrl_port=32769; cfg.max_open_sockets=4; cfg.max_uri_handlers=1;
  cfg.stack_size=6144; cfg.recv_wait_timeout=1;cfg.send_wait_timeout=1;
  cfg.open_fn=opened;cfg.close_fn=closed;cfg.global_user_ctx=this;
  if(httpd_start(&server_,&cfg)!=ESP_OK){server_=nullptr;clients_.reset();state_.reset();return false;}
  httpd_uri_t uri{};uri.uri="/api/live";uri.method=HTTP_GET;uri.handler=handler;uri.user_ctx=this;uri.is_websocket=true;
  if(httpd_register_uri_handler(server_,&uri)!=ESP_OK){httpd_stop(server_);server_=nullptr;clients_.reset();state_.reset();return false;}
  return true;
}
esp_err_t LiveServer::opened(httpd_handle_t server,int fd) {
  int yes=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof yes);
  timeval timeout{0,5000};
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout);
  // IDF 5.5.5 no longer calls the URI handler for a successful upgrade.
  // Reserve the client at accept; sendWork waits for the WS handshake before
  // publishing. This also bounds clients that have not finished upgrading.
  auto &self=*static_cast<LiveServer *>(httpd_get_global_user_ctx(server));
  Guard lock(self.mutex_);
  for(auto &c:*self.clients_)if(c.fd<0){const auto gen=c.generation+1;c=Client{};c.fd=fd;c.generation=gen;return ESP_OK;}
  return ESP_FAIL;
}
void LiveServer::closed(httpd_handle_t server,int fd) {
  auto &self=*static_cast<LiveServer *>(httpd_get_global_user_ctx(server));
  { Guard lock(self.mutex_); for(auto &c:*self.clients_)if(c.fd==fd){const auto generation=c.generation+1;c=Client{};c.generation=generation;} }
  ::close(fd);
}
void LiveServer::reply(Client &c,uint32_t id,const char *body) {
  if(c.count==4){c.closing=true;return;}
  auto &r=c.replies[(c.head+c.count)%4];

  const int n=std::snprintf(r.data,sizeof r.data,"{\"v\":1,\"type\":\"result\",\"boot\":\"%s\",\"id\":%lu,\"result\":%s}",boot_,static_cast<unsigned long>(id),body);
  if(n<0||size_t(n)>=sizeof r.data){c.closing=true;return;}
  r.size=size_t(n);++c.count;
}
void LiveServer::cancelTarget(Client &c,uint32_t id) {
  if(!c.cancelledFrom) c.cancelledFrom=c.cancelledThrough=id;
  else if(id==c.cancelledThrough+1) c.cancelledThrough=id;
  else reply(c,id,"{\"ok\":false,\"result\":\"cancelled\",\"superseded\":true}");
}
esp_err_t LiveServer::handler(httpd_req_t *req) {
  auto &self=*static_cast<LiveServer *>(req->user_ctx); const int fd=httpd_req_to_sockfd(req);
  if(req->method==HTTP_GET) {
    // Older IDF versions also invoke this handler after upgrading. Ordinary
    // HTTP requests to this endpoint must not occupy a live slot indefinitely.
    return httpd_ws_get_fd_info(self.server_,fd)==HTTPD_WS_CLIENT_WEBSOCKET?ESP_OK:ESP_FAIL;
  }
  httpd_ws_frame_t frame{};
  if(httpd_ws_recv_frame(req,&frame,0)!=ESP_OK)return ESP_FAIL;
  if(frame.type==HTTPD_WS_TYPE_CLOSE)return ESP_FAIL;
  if(frame.type!=HTTPD_WS_TYPE_TEXT||frame.len>LIVE_INPUT_CAPACITY||!frame.final)return ESP_FAIL;
  Message input;frame.payload=reinterpret_cast<uint8_t *>(input.data);
  if(httpd_ws_recv_frame(req,&frame,LIVE_INPUT_CAPACITY)!=ESP_OK)return ESP_FAIL;
  input.size=frame.len;
  LiveRequest request;if(!request.parse({input.data,input.size}))return ESP_FAIL;
  Guard lock(self.mutex_);
  for(auto &c:*self.clients_)if(c.fd==fd&&(!c.closing||request.stop())) {
    if(request.id<=c.lastId){self.reply(c,request.id,"{\"ok\":false,\"result\":\"cancelled\"}");return ESP_OK;}
    c.lastId=request.id;
    auto &slot=request.stop()?c.stop:c.command;
    if(slot.size) {
      LiveRequest old;
      bool replace=!request.stop() && old.parse({slot.data,slot.size}) &&
        old.path=="/api/target" && request.path=="/api/target" && old.boot==request.boot &&
        old.selection==request.selection && old.get("rangeRevision")==request.get("rangeRevision");
      if(replace) {
        for(unsigned axis=0;axis<2;++axis) for(const LiveRequest *candidate:{&old,&request}) {
          const auto value=candidate->get(axis?"el":"az"); if(value.empty()) continue;
          float angle; if(!number(value,angle) || !self.ingressRange_[axis].contains(angle) ||
              (axis && !self.ingressEl_ && angle!=0) ||
              ((axis==0 || self.ingressEl_) && !self.ingressReady_[axis])) replace=false;
        }
      }
      if(replace) {
        bool barrier=false; LiveRequest stop;
        if(c.stop.size && stop.parse({c.stop.data,c.stop.size})) barrier=old.id<stop.id && stop.id<request.id;
        if(!barrier) for(const char *key:{"az","el"}) if(request.get(key).empty() && !old.get(key).empty()) {
          float angle; if(!number(old.get(key),angle)) { replace=false; break; }
          const int added=std::snprintf(input.data+input.size,sizeof input.data-input.size,"&%s=%.9g",key,double(angle));
          if(added<0 || size_t(added)>=sizeof input.data-input.size) { replace=false;break; } input.size+=size_t(added);
        }
        if(replace) { self.cancelTarget(c,old.id);slot=input; }
      }
      if(!replace) self.reply(c,request.id,"{\"ok\":false,\"result\":\"busy\"}");
    } else slot=input;
    return ESP_OK;
  }
  return ESP_FAIL;
}
void LiveServer::commands(uint32_t now) {
  if(!server_)return;
  // STOP from every client precedes all normal commands. A STOP cancels queued
  // older work on that connection; unrelated clients retain controller semantics.
  for(unsigned pass=0;pass<2;++pass)for(auto &client:*clients_) {
    Message input;uint32_t generation;int fd;
    { Guard lock(mutex_); if(client.fd<0||(client.closing&&(pass||!client.stop.size)))continue;
      auto &slot=pass?client.command:client.stop;if(!slot.size)continue;
      input=slot;slot.size=0;generation=client.generation;fd=client.fd;
      if(!pass&&client.command.size){LiveRequest old,stop;
        if(old.parse({client.command.data,client.command.size})&&stop.parse({input.data,input.size})&&old.id<stop.id){
          reply(client,old.id,"{\"ok\":false,\"result\":\"cancelled\"}");client.command.size=0;
        }
      }
    }
    LiveRequest request;if(!request.parse({input.data,input.size}))continue;
    // Main-loop ownership: HTTPD must never call executeLive or touch runtime.
    const auto response=executeLive(request,runtime_.controller,runtime_.protocol,runtime_.loggerSettings,runtime_.hud,now);
    Guard lock(mutex_);if(client.fd!=fd||client.generation!=generation)continue;
    reply(client,request.id,response.body.c_str());
    if(response.status==202){bool saved=false;for(auto &p:client.pending)if(!p.cid){p={request.id,runtime_.hud.requestId()};saved=true;break;}if(!saved)client.closing=true;}
  }
  for(auto &client:*clients_) {
    Guard lock(mutex_);if(client.fd<0||client.closing)continue;
    for(auto &p:client.pending)if(p.cid){const auto result=runtime_.hud.result(p.request).result;if(result!=Result::Deferred){
      char body[80];std::snprintf(body,sizeof body,"{\"ok\":%s,\"result\":\"%s\"}",result==Result::Ok||result==Result::Pending?"true":"false",Controller::resultName(result));
      reply(client,p.cid,body);p={};
    }}
  }
}
void LiveServer::publish(uint32_t now) {
  if(!server_)return;
  bool any=false;{Guard lock(mutex_);
    ingressRange_[0]=runtime_.controller.azRange;ingressRange_[1]=runtime_.controller.elRange;ingressEl_=runtime_.controller.model==Model::G5500;
    for(unsigned i=0;i<2;++i) ingressReady_[i]=runtime_.controller.axes[i].calibrated &&
      (!runtime_.controller.variableSpeed(i) || runtime_.controller.motionSettings[i].ready);
    for(auto &c:*clients_)any|=c.fd>=0;}
  if(!any)return;
  const uint32_t critical=liveControlFingerprint(runtime_.controller,runtime_.hud);
  if(critical!=controlHash_ || uint32_t(now-publishedAt_)>=WebPositionIntervalMs) {
    controlHash_=critical;
    char data[LIVE_STATE_CAPACITY];JsonWriter j(data,sizeof data);writeLiveState(j,runtime_.controller,runtime_.hud,now);
    if(j.good()) {
      const uint32_t hash=hudCrc(reinterpret_cast<const uint8_t *>(j.data()),j.size());
      Guard lock(mutex_);
      if(hash!=hash_||uint32_t(now-publishedAt_)>=250){
        hash_=hash;publishedAt_=now;++revision_;char boot[17];liveBootText(boot,runtime_.hud.boot);
        JsonWriter out(state_->data(),state_->size());out<<"{\"v\":1,\"type\":\"state\",\"boot\":"<<jsonQuoted(boot)<<",\"revision\":"<<revision_<<",\"state\":"<<std::string_view(j.data(),j.size())<<'}';
        stateSize_=out.good()?out.size():0;
      }
    }
  }
  bool queue=false;
  {Guard lock(mutex_);
    bool needed=false;for(const auto &c:*clients_)needed|=c.fd>=0&&(c.closing||c.count||c.cancelledFrom||c.revision!=revision_);
    if(needed&&!workPending_&&uint32_t(now-wakeAt_)>=5){workPending_=true;wakeAt_=now;queue=true;}
  }
  if(queue&&httpd_queue_work(server_,sendWork,this)!=ESP_OK){Guard lock(mutex_);workPending_=false;++queueErrors_;}
}
void LiveServer::writeDiagnostics(JsonWriter &j) {
  if(!server_)return;
  Guard lock(mutex_);unsigned clients=0;
  for(const auto &c:*clients_)clients+=c.fd>=0;
  j<<",\"liveClients\":"<<clients<<",\"liveRevision\":"<<revision_<<",\"liveStateBytes\":"<<stateSize_
   <<",\"liveWorkRuns\":"<<workRuns_<<",\"liveFrames\":"<<framesSent_
   <<",\"liveQueueErrors\":"<<queueErrors_<<",\"liveSendErrors\":"<<sendErrors_;
}
void LiveServer::sendWork(void *context) {
  auto &self=*static_cast<LiveServer *>(context);
  {Guard lock(self.mutex_);++self.workRuns_;}
  char data[LIVE_STATE_CAPACITY];
  for(auto &client:*self.clients_) {
    size_t n=0;int fd;bool close=false;
    {Guard lock(self.mutex_);fd=client.fd;if(fd<0)continue;
      if(httpd_ws_get_fd_info(self.server_,fd)!=HTTPD_WS_CLIENT_WEBSOCKET)continue;
      close=client.closing&&!client.stop.size;
      if(!close&&client.cancelledFrom){
        const int size=std::snprintf(data,sizeof data,"{\"v\":1,\"type\":\"result\",\"boot\":\"%s\",\"id\":%lu,\"cancelledFrom\":%lu,\"cancelledThrough\":%lu,\"result\":{\"ok\":false,\"result\":\"cancelled\",\"superseded\":true}}",self.boot_,(unsigned long)client.cancelledThrough,(unsigned long)client.cancelledFrom,(unsigned long)client.cancelledThrough);
        if(size>0 && size_t(size)<sizeof data)n=size_t(size);
        client.cancelledFrom=client.cancelledThrough=0;
      }
      else if(!close&&client.count){auto &r=client.replies[client.head];n=r.size;std::memcpy(data,r.data,n);client.head=(client.head+1)%4;--client.count;}
      else if(!close&&client.revision!=self.revision_&&self.stateSize_){n=self.stateSize_;std::memcpy(data,self.state_->data(),n);client.revision=self.revision_;}
    }
    if(n){httpd_ws_frame_t frame{};frame.type=HTTPD_WS_TYPE_TEXT;frame.final=true;frame.payload=reinterpret_cast<uint8_t *>(data);frame.len=n;
      const bool sent=httpd_ws_send_frame_async(self.server_,fd,&frame)==ESP_OK;
      {Guard lock(self.mutex_);if(sent)++self.framesSent_;else ++self.sendErrors_;}
      if(!sent)close=true;}
    if(close)httpd_sess_trigger_close(self.server_,fd);
  }
  Guard lock(self.mutex_);self.workPending_=false;
}
}
#endif
