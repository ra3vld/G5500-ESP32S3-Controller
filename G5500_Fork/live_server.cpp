#include "live_server.hpp"
#include "network_timing.hpp"
#if defined(ARDUINO_ARCH_ESP32) && WEB_INTERFACE_ENABLED
#include "heap_metrics.hpp"
#include "bounded_socket_write.hpp"
#include <tcp_peer.hpp>
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
  if(!clients_)return false;
  liveBootText(boot_,runtime_.hud.boot);
  selection_=runtime_.hud.selectionEpoch;
  ingressRange_[0]=runtime_.controller.azRange;ingressRange_[1]=runtime_.controller.elRange;ingressEl_=runtime_.controller.model==Model::G5500;
  for(unsigned i=0;i<2;++i) {
    ingressReady_[i]=runtime_.controller.axes[i].calibrated &&
      (!runtime_.controller.variableSpeed(i) || runtime_.controller.motionSettings[i].ready);
    ingressSpeedLimit_[i]=runtime_.controller.runtimeSpeedLimit(i);
  }
  mutex_=xSemaphoreCreateMutexStatic(&mutexStorage_);
  httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
  cfg.server_port=81; cfg.ctrl_port=32769; cfg.max_uri_handlers=2;
  // Keep one HTTPD slot free to accept/reject an excess peer immediately.
  // At its session limit IDF stops accepting: reconnecting browsers otherwise
  // accumulate queued sockets/buffers instead of reaching our admission check.
  cfg.max_open_sockets=clients_->size()+1;cfg.backlog_conn=1;
  cfg.stack_size=6144; cfg.recv_wait_timeout=1;cfg.send_wait_timeout=1;
  cfg.open_fn=opened;cfg.close_fn=closed;cfg.global_user_ctx=this;
  if(httpd_start(&server_,&cfg)!=ESP_OK){server_=nullptr;clients_.reset();return false;}
  httpd_uri_t uri{};uri.uri="/api/live";uri.method=HTTP_GET;uri.handler=handler;uri.user_ctx=this;uri.is_websocket=true;uri.handle_ws_control_frames=true;
  if(httpd_register_uri_handler(server_,&uri)!=ESP_OK){httpd_stop(server_);server_=nullptr;clients_.reset();return false;}
  uri.uri="/api/control";
  if(httpd_register_uri_handler(server_,&uri)!=ESP_OK){httpd_stop(server_);server_=nullptr;clients_.reset();return false;}
  return true;
}
esp_err_t LiveServer::opened(httpd_handle_t server,int fd) {
  int yes=1;setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof yes);
  // HTTPD runs independently of motor control. Allow a split TCP request to
  // arrive across Wi-Fi retries, with a bounded wait for an incomplete peer.
  timeval timeout{0,200000};
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
  // Synchronous writes are used only for HTTP upgrade and peer CLOSE replies.
  // Data/PONG frames use the persistent nonblocking outgoing buffer below.
  if(httpd_sess_set_send_override(server,fd,sendSocket)!=ESP_OK)return ESP_FAIL;
  // IDF 5.5.5 no longer calls the URI handler for a successful upgrade.
  // Reserve the client at accept; sendWork waits for the WS handshake before
  // publishing. This also bounds clients that have not finished upgrading.
  auto &self=*static_cast<LiveServer *>(httpd_get_global_user_ctx(server));
  Guard lock(self.mutex_);
  for(auto &c:*self.clients_)if(c.fd<0){
    // Decline a new browser before it eats the control/recovery headroom.
    // Existing clients, including their STOP queue, keep their buffers.
    const size_t needed=sizeof(ClientBuffers)+(self.stateSize_?0:sizeof(StateBuffer));
    if(heapFreeBytes()<needed+MemoryGuard::HealthyFree ||
       heapMaxBlockBytes()<needed+MemoryGuard::HealthyBlock)return ESP_FAIL;
    std::unique_ptr<ClientBuffers> buffers(new(std::nothrow) ClientBuffers{});
    if(!buffers)return ESP_FAIL;
    const auto gen=c.generation+1;c=Client{};c.buffers=std::move(buffers);
    c.fd=fd;c.generation=gen;return ESP_OK;
  }
  return ESP_FAIL;
}
int LiveServer::sendSocket(httpd_handle_t server,int fd,const char *data,size_t size,int flags) {
  const auto now=[] {return uint32_t(esp_timer_get_time()/1000);};
  const uint32_t started=now();size_t written=0;uint32_t retries=0;int retryError=0;
  const int result=boundedSocketWrite(data,size,100,
    [&](const char *p,size_t n){
      const int sent=::send(fd,p,n,flags|MSG_DONTWAIT);
      if(sent>0)written+=size_t(sent);
      else if(sent<0){retryError=errno;++retries;}
      return sent;
    },now,[]{vTaskDelay(1);});
  if(result<0) {
    const int error=errno;const uint32_t elapsed=now()-started;
    auto &self=*static_cast<LiveServer *>(httpd_get_global_user_ctx(server));
    {Guard lock(self.mutex_);
      self.sendErrno_=error;self.sendRetryErrno_=retryError;
      self.sendWritten_=written;self.sendRequested_=size;
      self.sendRetryErrors_=retries;self.sendElapsedMs_=elapsed;
      if(error==ETIMEDOUT)++self.sendTimeouts_;
    }
    errno=error;
  }
  return result;
}
void LiveServer::closed(httpd_handle_t server,int fd) {
  auto &self=*static_cast<LiveServer *>(httpd_get_global_user_ctx(server));
  { Guard lock(self.mutex_);
    bool any=false;
    for(auto &c:*self.clients_){
      if(c.fd==fd){releaseSnapshot(*c.buffers,uint32_t(esp_timer_get_time()/1000));const auto generation=c.generation+1;c=Client{};c.generation=generation;}
      any|=c.fd>=0;
    }
    if(!any){for(auto &state:self.states_)state.reset();self.stateSize_=0;}
  }
  ::close(fd);
}
LiveServer::StateBuffer *LiveServer::writableState(unsigned &index) {
  bool any=false;for(const auto &client:*clients_)any|=client.fd>=0;
  if(!any)return nullptr;
  // Called with mutex_: only zero-user frames may be overwritten. Prefer
  // reusing the latest one, so fast/idle links need just one shared buffer.
  if(states_[stateIndex_] && !states_[stateIndex_]->users){index=stateIndex_;return states_[index].get();}
  for(unsigned i=0;i<states_.size();++i)if(states_[i]&&!states_[i]->users){index=i;return states_[i].get();}
  for(unsigned i=0;i<states_.size();++i)if(!states_[i]){
    if(heapFreeBytes()<sizeof(StateBuffer)+MemoryGuard::HealthyFree ||
       heapMaxBlockBytes()<sizeof(StateBuffer)+MemoryGuard::HealthyBlock)return nullptr;
    states_[i].reset(new(std::nothrow) StateBuffer{});index=i;return states_[i].get();
  }
  // All clients are still transmitting. Retain the current publication until
  // one of them releases a frame; commands/STOP and pending sends keep running.
  return nullptr;
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
  const bool control=frame.type==HTTPD_WS_TYPE_PING||frame.type==HTTPD_WS_TYPE_PONG||frame.type==HTTPD_WS_TYPE_CLOSE;
  if((frame.type!=HTTPD_WS_TYPE_TEXT&&!control)||frame.len>(control?125:LIVE_INPUT_CAPACITY)||!frame.final)return ESP_FAIL;
  Message input;frame.payload=reinterpret_cast<uint8_t *>(input.data);
  // IDF uses len==0 to parse a new frame header. An empty control frame is
  // already complete after the first read; a second read consumes the next
  // frame (or waits for it and disconnects a healthy peer).
  if(frame.len&&httpd_ws_recv_frame(req,&frame,LIVE_INPUT_CAPACITY)!=ESP_OK)return ESP_FAIL;
  if(control) {
    if(frame.type==HTTPD_WS_TYPE_PONG)return ESP_OK;
    if(frame.type==HTTPD_WS_TYPE_CLOSE) {
      // IDF closes this session after the handler. Reply only at a frame
      // boundary: never insert CLOSE bytes into an incomplete text frame.
      {Guard lock(self.mutex_);for(auto &c:*self.clients_)if(c.fd==fd&&c.buffers->outgoing.written())return ESP_FAIL;}
      return httpd_ws_send_frame(req,&frame);
    }
    Guard lock(self.mutex_);
    for(auto &c:*self.clients_)if(c.fd==fd){
      std::memcpy(c.buffers->pong,input.data,frame.len);c.buffers->pongSize=frame.len;c.buffers->pongPending=true;return ESP_OK;
    }
    return ESP_FAIL;
  }
  input.size=frame.len;
  if(std::string_view(input.data,input.size)=="v=1&ping=1") {
    Guard lock(self.mutex_);
    for(auto &c:*self.clients_)if(c.fd==fd&&!c.closing) {
      if(!c.buffers->pongPending)c.buffers->pongSize=0;
      c.buffers->pongPending=true;return ESP_OK;
    }
    return ESP_FAIL;
  }
  // Select before the first command, without needing a boot ID from a large
  // telemetry frame. IDF can finish the upgrade without calling our GET handler.
  if(std::string_view(input.data,input.size)=="v=1&control=1") {
    Guard lock(self.mutex_);
    for(auto &c:*self.clients_)if(c.fd==fd) {
      c.controlOnly=true;c.revision=0;return ESP_OK;
    }
    return ESP_FAIL;
  }
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
      // Leased input has an admission reply and keeps renewing. Hud::result()
      // tracks one-shot jobs, not this coalesced manual stream.
      const bool manualStream=request.path=="/api/move"&&!request.get("gesture").empty();
      if(response.status==202&&!manualStream){bool saved=false;for(auto &p:client.buffers->pending)if(!p.cid){p={request.id,runtime_.hud.requestId()};saved=true;break;}if(!saved)client.closing=true;}
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
  bool any=false,missingState=false;{Guard lock(mutex_);
    for(unsigned i=0;i<states_.size();++i)if(i!=stateIndex_&&states_[i]&&!states_[i]->users&&uint32_t(now-states_[i]->unusedAt)>=1000)states_[i].reset();
    selection_=runtime_.hud.selectionEpoch;
    ingressRange_[0]=runtime_.controller.azRange;ingressRange_[1]=runtime_.controller.elRange;ingressEl_=runtime_.controller.model==Model::G5500;
    for(unsigned i=0;i<2;++i) {
      ingressReady_[i]=runtime_.controller.axes[i].calibrated &&
        (!runtime_.controller.variableSpeed(i) || runtime_.controller.motionSettings[i].ready);
      ingressSpeedLimit_[i]=runtime_.controller.runtimeSpeedLimit(i);
    }
    for(auto &c:*clients_)any|=c.fd>=0;
    missingState=!stateSize_;}
  if(!any)return;
  const uint32_t critical=liveControlFingerprint(runtime_.controller,runtime_.hud);
  if(missingState || critical!=controlHash_ || uint32_t(now-publishedAt_)>=WebPositionIntervalMs) {
    char data[LIVE_STATE_CAPACITY];JsonWriter j(data,sizeof data);writeLiveState(j,runtime_.controller,runtime_.hud,now);
    if(j.good()) {
      const uint32_t hash=hudCrc(reinterpret_cast<const uint8_t *>(j.data()),j.size());
      Guard lock(mutex_);
      if(!stateSize_||hash!=hash_||uint32_t(now-publishedAt_)>=250){
        unsigned index=stateIndex_;auto *state=writableState(index);
        if(state){
          const auto revision=revision_+1;char boot[17];liveBootText(boot,runtime_.hud.boot);
          JsonWriter out(state->bytes.data()+4,LIVE_STATE_CAPACITY);out<<"{\"v\":1,\"type\":\"state\",\"boot\":"<<jsonQuoted(boot)<<",\"revision\":"<<revision<<",\"state\":"<<std::string_view(j.data(),j.size())<<'}';
          if(out.good()){
            state->size=out.size();state->revision=revision;state->unusedAt=now;
            writeLiveFrameHeader(state->bytes.data(),state->size,HTTPD_WS_TYPE_TEXT);
            hash_=hash;publishedAt_=now;revision_=revision;stateIndex_=index;stateSize_=state->size;controlHash_=critical;
          }else{
            state->size=0;if(index==stateIndex_)stateSize_=0;
          }
        }
      }else controlHash_=critical;
    }
  }
  bool queue=false;
  {Guard lock(mutex_);
    bool needed=false;for(const auto &c:*clients_)needed|=c.fd>=0&&(c.closing||c.buffers->outgoing.active()||c.buffers->pongPending||c.buffers->count||c.buffers->cancelledFrom||
        (c.revision!=revision_&&(!c.ackWaiting||uint32_t(now-c.ackCheckedAt)>=20)));
    if(needed&&!workPending_&&uint32_t(now-wakeAt_)>=5){workPending_=true;wakeAt_=now;queue=true;}
  }
  if(queue&&httpd_queue_work(server_,sendWork,this)!=ESP_OK){Guard lock(mutex_);workPending_=false;++queueErrors_;}
}
void LiveServer::writeDiagnostics(JsonWriter &j) {
  if(!server_)return;
  Guard lock(mutex_);unsigned clients=0;
  for(const auto &c:*clients_)clients+=c.fd>=0;
  unsigned states=0;for(const auto &state:states_)states+=bool(state);
  j<<",\"liveControlChannel\":true,\"liveHeartbeat\":true,\"liveClients\":"<<clients<<",\"liveRevision\":"<<revision_<<",\"liveStateBytes\":"<<stateSize_
   <<",\"liveStateBuffers\":"<<states<<",\"liveBufferBytes\":"<<(sizeof(Clients)+clients*sizeof(ClientBuffers)+states*sizeof(StateBuffer))
   <<",\"liveWorkRuns\":"<<workRuns_<<",\"liveFrames\":"<<framesSent_
   <<",\"liveSendDeferred\":"<<sendDeferred_<<",\"liveQueueErrors\":"<<queueErrors_<<",\"liveSendErrors\":"<<sendErrors_
   <<",\"liveMemoryDeferred\":"<<memoryDeferred_<<",\"liveSocketDeferred\":"<<socketDeferred_
   <<",\"liveAckDeferred\":"<<ackDeferred_
   <<",\"liveAckTimeouts\":"<<ackTimeouts_
   <<",\"liveSendMemoryRetries\":"<<sendMemoryRetries_
   <<",\"liveSendTimeouts\":"<<sendTimeouts_<<",\"liveSendErrno\":"<<sendErrno_
   <<",\"liveSendRetryErrno\":"<<sendRetryErrno_<<",\"liveSendRetryErrors\":"<<sendRetryErrors_
   <<",\"liveSendWritten\":"<<sendWritten_<<",\"liveSendRequested\":"<<sendRequested_
   <<",\"liveSendElapsedMs\":"<<sendElapsedMs_;
}
void LiveServer::sendWork(void *context) {
  auto &self=*static_cast<LiveServer *>(context);
  {Guard lock(self.mutex_);++self.workRuns_;}
  for(auto &client:*self.clients_) {
    int fd;bool close=false,checkSpace=false;
    // Only HTTPD closes/replaces client buffers. Runtime only queues commands
    // and results under the mutex; the outgoing frame remains HTTPD-owned.
    {Guard lock(self.mutex_);fd=client.fd;if(fd<0)continue;
      if(httpd_ws_get_fd_info(self.server_,fd)!=HTTPD_WS_CLIENT_WEBSOCKET)continue;
      const auto &b=*client.buffers;
      checkSpace=!client.closing&&!client.controlOnly&&!b.outgoing.active()&&!b.pongPending&&!b.cancelledFrom&&!b.count;
    }
    // Never take lwIP's core lock while holding the controller/HTTPD mutex.
    const size_t capacity=checkSpace?tcpPeerFrameCapacity(fd):size_t(-1);
    {Guard lock(self.mutex_);
      // The loop can publish a larger revision while the TCP snapshot is read.
      // Compare its current size here, rather than admitting an older length.
      const bool waitingSpace=checkSpace&&client.revision!=self.revision_&&self.stateSize_&&
          capacity<self.stateSize_+(self.stateSize_<126?2:4);
      close=client.closing&&!client.buffers->stop.size;
      auto &b=*client.buffers;auto &out=b.outgoing;
      if(!close&&!client.closing&&!out.active()) {
        size_t n=0;uint8_t opcode=HTTPD_WS_TYPE_TEXT;char *data=out.payload();
        if(b.pongPending){n=b.pongSize;std::memcpy(data,b.pong,n);b.pongPending=false;opcode=HTTPD_WS_TYPE_PONG;}
        else if(b.cancelledFrom){
          const int size=std::snprintf(data,ControlCapacity,"{\"v\":1,\"type\":\"result\",\"boot\":\"%s\",\"id\":%lu,\"cancelledFrom\":%lu,\"cancelledThrough\":%lu,\"result\":{\"ok\":false,\"result\":\"cancelled\",\"superseded\":true}}",self.boot_,(unsigned long)b.cancelledThrough,(unsigned long)b.cancelledFrom,(unsigned long)b.cancelledThrough);
          if(size>0&&size_t(size)<ControlCapacity)n=size_t(size);
          b.cancelledFrom=b.cancelledThrough=0;
        }
        else if(b.count){auto &r=b.replies[b.head];n=r.size;std::memcpy(data,r.data,n);b.head=(b.head+1)%4;--b.count;}
        else if(client.revision!=self.revision_&&self.stateSize_&&!waitingSpace){
          if(client.controlOnly) {
            const int size=std::snprintf(data,ControlCapacity,"{\"v\":1,\"type\":\"control\",\"boot\":\"%s\",\"selection\":%lu}",self.boot_,(unsigned long)self.selection_);
            if(size>0&&size_t(size)<ControlCapacity)n=size_t(size);
          } else {
            auto &state=*self.states_[self.stateIndex_];
            if(out.borrow(state.bytes.data(),state.size,uint32_t(esp_timer_get_time()/1000))){
              ++state.users;b.snapshot=&state;b.memoryWaits=0;
            }
          }
          client.revision=self.revision_;
          client.ackWaiting=false;
        }
        else if(waitingSpace){
          const auto now=uint32_t(esp_timer_get_time()/1000);
          if(!client.ackWaiting)client.ackStartedAt=now;
          client.ackWaiting=true;client.ackCheckedAt=now;++self.ackDeferred_;
          if(uint32_t(now-client.ackStartedAt)>=LiveFrameTimeoutMs){
            ++self.ackTimeouts_;++self.sendErrors_;++self.sendTimeouts_;
            self.sendErrno_=self.sendRetryErrno_=ETIMEDOUT;self.sendElapsedMs_=uint32_t(now-client.ackStartedAt);
            self.sendWritten_=self.sendRequested_=self.sendRetryErrors_=self.sendMemoryRetries_=0;
            client.closing=true;close=!b.stop.size;
          }
        }
        if(n||opcode==HTTPD_WS_TYPE_PONG){out.begin(n,opcode,uint32_t(esp_timer_get_time()/1000));b.memoryWaits=0;}
      }
    }
    auto &out=client.buffers->outgoing;
    if(!close&&out.active()) {
      const uint32_t now=uint32_t(esp_timer_get_time()/1000);const auto retries=out.retries;
      bool memoryWait=false;
      const auto step=out.pump(now,[=,&memoryWait](const char *data,size_t size)->int{
        if(size>256&&!networkBulkTxReady()){memoryWait=true;errno=EAGAIN;return -1;}
        return ::send(fd,data,size,MSG_DONTWAIT);
      });
      const int error=errno;
      {Guard lock(self.mutex_);
        self.sendDeferred_+=out.retries-retries;
        if(memoryWait){++self.memoryDeferred_;++client.buffers->memoryWaits;}
        else self.socketDeferred_+=out.retries-retries;
        if(step==LiveFrame<ControlCapacity>::Step::Complete){++self.framesSent_;out.clear();releaseSnapshot(*client.buffers,now);}
        else if(step==LiveFrame<ControlCapacity>::Step::Failed){
          ++self.sendErrors_;self.sendErrno_=error;self.sendRetryErrno_=out.retryError;
          self.sendRetryErrors_=out.retries;self.sendElapsedMs_=now-out.started;
          self.sendWritten_=out.written();self.sendRequested_=out.requested();
          self.sendMemoryRetries_=client.buffers->memoryWaits;
          if(error==ETIMEDOUT)++self.sendTimeouts_;
          close=true;
        }
      }
    }
    if(close)httpd_sess_trigger_close(self.server_,fd);
  }
  Guard lock(self.mutex_);self.workPending_=false;
}
}
#endif
