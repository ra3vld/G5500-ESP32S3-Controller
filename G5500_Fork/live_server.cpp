#include "live_server.hpp"
#include "network_timing.hpp"
#if defined(ARDUINO_ARCH_ESP32) && WEB_INTERFACE_ENABLED
#include "heap_metrics.hpp"
#include "bounded_socket_write.hpp"
#include <esp_timer.h>
#include <freertos/task.h>
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
  for(unsigned i=0;i<2;++i) {
    ingressReady_[i]=runtime_.controller.axes[i].calibrated &&
      (!runtime_.controller.variableSpeed(i) || runtime_.controller.motionSettings[i].ready);
    ingressSpeedLimit_[i]=runtime_.controller.runtimeSpeedLimit(i);
  }
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
  // HTTPD runs independently of motor control. Allow a split TCP request to
  // arrive across Wi-Fi retries, with a bounded wait for an incomplete peer.
  timeval timeout{0,200000};
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
  // IDF's WS sender treats a positive short write as a complete write. Ensure
  // both handshake and WS bytes are sent in full, without a 5 ms disconnect.
  if(httpd_sess_set_send_override(server,fd,sendSocket)!=ESP_OK)return ESP_FAIL;
  // IDF 5.5.5 no longer calls the URI handler for a successful upgrade.
  // Reserve the client at accept; sendWork waits for the WS handshake before
  // publishing. This also bounds clients that have not finished upgrading.
  auto &self=*static_cast<LiveServer *>(httpd_get_global_user_ctx(server));
  Guard lock(self.mutex_);
  for(auto &c:*self.clients_)if(c.fd<0){
    // Decline a new browser before it eats the control/recovery headroom.
    // Existing clients, including their STOP queue, keep their buffers.
    if(heapFreeBytes()<sizeof(ClientBuffers)+MemoryGuard::CriticalFree ||
       heapMaxBlockBytes()<sizeof(ClientBuffers)+MemoryGuard::CriticalBlock)return ESP_FAIL;
    std::unique_ptr<ClientBuffers> buffers(new(std::nothrow) ClientBuffers{});
    if(!buffers)return ESP_FAIL;
    const auto gen=c.generation+1;c=Client{};c.buffers=std::move(buffers);
    c.fd=fd;c.generation=gen;return ESP_OK;
  }
  return ESP_FAIL;
}
int LiveServer::sendSocket(httpd_handle_t,int fd,const char *data,size_t size,int flags) {
  return boundedSocketWrite(data,size,100,
    [=](const char *p,size_t n){return ::send(fd,p,n,flags|MSG_DONTWAIT);},
    []{return uint32_t(esp_timer_get_time()/1000);},
    []{vTaskDelay(1);});
}
void LiveServer::closed(httpd_handle_t server,int fd) {
  auto &self=*static_cast<LiveServer *>(httpd_get_global_user_ctx(server));
  { Guard lock(self.mutex_); for(auto &c:*self.clients_)if(c.fd==fd){const auto generation=c.generation+1;c=Client{};c.generation=generation;} }
  ::close(fd);
}
void LiveServer::reply(Client &c,uint32_t id,const char *body) {
  if(c.buffers->count==4){c.closing=true;return;}
  auto &r=c.buffers->replies[(c.buffers->head+c.buffers->count)%4];

  const int n=std::snprintf(r.data,sizeof r.data,"{\"v\":1,\"type\":\"result\",\"boot\":\"%s\",\"id\":%lu,\"result\":%s}",boot_,static_cast<unsigned long>(id),body);
  if(n<0||size_t(n)>=sizeof r.data){c.closing=true;return;}
  r.size=size_t(n);++c.buffers->count;
}
void LiveServer::cancelTarget(Client &c,uint32_t id) {
  if(!c.buffers->cancelledFrom) c.buffers->cancelledFrom=c.buffers->cancelledThrough=id;
  else if(id==c.buffers->cancelledThrough+1) c.buffers->cancelledThrough=id;
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
    if(request.path=="/api/speed") {
      const auto axis=request.get("axis");SpeedRequest next;
      next.boot=request.boot;next.cid=request.id;next.selection=request.selection;
      next.hasRangeRevision=!request.get("rangeRevision").empty();
      if((axis!="az"&&axis!="el")||!number(request.get("value"),next.value)||
          (next.hasRangeRevision&&!requestGeneration(request.get("rangeRevision"),next.rangeRevision))) {
        self.reply(c,request.id,"{\"ok\":false,\"result\":\"invalid\"}");return ESP_OK;
      }
      const unsigned index=axis=="az"?0:1;const float limit=self.ingressSpeedLimit_[index];
      if(next.value<limit/20-limit*1e-6f||next.value>limit) {
        self.reply(c,request.id,"{\"ok\":false,\"result\":\"invalid\"}");return ESP_OK;
      }
      auto &speed=c.buffers->speeds[index];
      if(speed.cid&&(speed.boot!=next.boot||speed.selection!=next.selection||
          speed.hasRangeRevision!=next.hasRangeRevision||speed.rangeRevision!=next.rangeRevision)) {
        self.reply(c,request.id,"{\"ok\":false,\"result\":\"busy\"}");return ESP_OK;
      }
      if(speed.cid)self.cancelTarget(c,speed.cid);
      speed=next;return ESP_OK;
    }
    auto &slot=request.stop()?c.buffers->stop:c.buffers->command;
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
        if(c.buffers->stop.size && stop.parse({c.buffers->stop.data,c.buffers->stop.size})) barrier=old.id<stop.id && stop.id<request.id;
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
void LiveServer::speedCommands(uint32_t now) {
  // Numeric latest-value slots keep AZ/EL independent without another pair
  // of full websocket buffers. HTTPD queues; only the control loop executes.
  for(auto &client:*clients_)for(unsigned axis=0;axis<2;++axis) {
    SpeedRequest input;uint32_t generation;int fd;
    { Guard lock(mutex_);if(client.fd<0||client.closing)continue;
      auto &slot=client.buffers->speeds[axis];if(!slot.cid)continue;
      input=slot;slot={};generation=client.generation;fd=client.fd;
    }
    Response response{409,"{\"ok\":false,\"result\":\"cancelled\"}"};
    if(input.boot==runtime_.hud.boot&&input.selection==runtime_.hud.selectionEpoch) {
      char value[24],revision[11];std::snprintf(value,sizeof value,"%.9g",double(input.value));
      std::snprintf(revision,sizeof revision,"%lu",static_cast<unsigned long>(input.rangeRevision));
      const std::pair<std::string_view,std::string_view> args[]={{"axis",axis?"el":"az"},{"value",value},{"rangeRevision",revision}};
      response=apiView(runtime_.controller,runtime_.protocol,runtime_.loggerSettings,"/api/speed",
          {args,input.hasRangeRevision?3u:2u},now);
      runtime_.controller.tick(now);
    }
    Guard lock(mutex_);if(client.fd!=fd||client.generation!=generation)continue;
    reply(client,input.cid,response.body.c_str());
  }
}
void LiveServer::commands(uint32_t now) {
  if(!server_)return;
  // STOP from every client precedes all normal commands. A STOP cancels queued
  // older work on that connection; unrelated clients retain controller semantics.
  for(unsigned pass=0;pass<2;++pass) {
    if(pass)speedCommands(now);
    for(auto &client:*clients_) {
      Message input;uint32_t generation;int fd;
      { Guard lock(mutex_); if(client.fd<0||(client.closing&&(pass||!client.buffers->stop.size)))continue;
        auto &slot=pass?client.buffers->command:client.buffers->stop;if(!slot.size)continue;
        input=slot;slot.size=0;generation=client.generation;fd=client.fd;
        if(!pass){LiveRequest stop;
          if(stop.parse({input.data,input.size})) {
            if(client.buffers->command.size){LiveRequest old;
              if(old.parse({client.buffers->command.data,client.buffers->command.size})&&old.id<stop.id){
                reply(client,old.id,"{\"ok\":false,\"result\":\"cancelled\"}");client.buffers->command.size=0;
              }
            }
            for(auto &speed:client.buffers->speeds)if(speed.cid&&speed.cid<stop.id) {
              reply(client,speed.cid,"{\"ok\":false,\"result\":\"cancelled\"}");speed={};
            }
          }
        }
      }
      LiveRequest request;if(!request.parse({input.data,input.size}))continue;
      // Main-loop ownership: HTTPD must never call executeLive or touch runtime.
      const auto response=executeLive(request,runtime_.controller,runtime_.protocol,runtime_.loggerSettings,runtime_.hud,now);
      Guard lock(mutex_);if(client.fd!=fd||client.generation!=generation)continue;
      reply(client,request.id,response.body.c_str());
      if(response.status==202){bool saved=false;for(auto &p:client.buffers->pending)if(!p.cid){p={request.id,runtime_.hud.requestId()};saved=true;break;}if(!saved)client.closing=true;}
    }
  }
  for(auto &client:*clients_) {
    Guard lock(mutex_);if(client.fd<0||client.closing)continue;
    for(auto &p:client.buffers->pending)if(p.cid){const auto result=runtime_.hud.result(p.request).result;if(result!=Result::Deferred){
      char body[80];std::snprintf(body,sizeof body,"{\"ok\":%s,\"result\":\"%s\"}",result==Result::Ok||result==Result::Pending?"true":"false",Controller::resultName(result));
      reply(client,p.cid,body);p={};
    }}
  }
}
void LiveServer::publish(uint32_t now) {
  if(!server_)return;
  bool any=false;{Guard lock(mutex_);
    ingressRange_[0]=runtime_.controller.azRange;ingressRange_[1]=runtime_.controller.elRange;ingressEl_=runtime_.controller.model==Model::G5500;
    for(unsigned i=0;i<2;++i) {
      ingressReady_[i]=runtime_.controller.axes[i].calibrated &&
        (!runtime_.controller.variableSpeed(i) || runtime_.controller.motionSettings[i].ready);
      ingressSpeedLimit_[i]=runtime_.controller.runtimeSpeedLimit(i);
    }
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
    bool needed=false;for(const auto &c:*clients_)needed|=c.fd>=0&&(c.closing||c.buffers->count||c.buffers->cancelledFrom||c.revision!=revision_);
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
      close=client.closing&&!client.buffers->stop.size;
      if(!close&&client.buffers->cancelledFrom){
        const int size=std::snprintf(data,sizeof data,"{\"v\":1,\"type\":\"result\",\"boot\":\"%s\",\"id\":%lu,\"cancelledFrom\":%lu,\"cancelledThrough\":%lu,\"result\":{\"ok\":false,\"result\":\"cancelled\",\"superseded\":true}}",self.boot_,(unsigned long)client.buffers->cancelledThrough,(unsigned long)client.buffers->cancelledFrom,(unsigned long)client.buffers->cancelledThrough);
        if(size>0 && size_t(size)<sizeof data)n=size_t(size);
        client.buffers->cancelledFrom=client.buffers->cancelledThrough=0;
      }
      else if(!close&&client.buffers->count){auto &r=client.buffers->replies[client.buffers->head];n=r.size;std::memcpy(data,r.data,n);client.buffers->head=(client.buffers->head+1)%4;--client.buffers->count;}
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
