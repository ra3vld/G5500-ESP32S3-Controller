#include "live_server.hpp"
#include "network_timing.hpp"
#if defined(ARDUINO_ARCH_ESP32) && WEB_LIVE_SUPPORT
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
    const size_t needed=sizeof(ClientBuffers)+(self.stateSize_?0:sizeof(StateBuffer)+LIVE_STATE_CAPACITY+4);
    if(heapFreeBytes()<needed+MemoryGuard::HealthyFree ||
       heapMaxBlockBytes()<needed+MemoryGuard::HealthyBlock)return ESP_FAIL;
    const auto gen=c.generation+1;c=Client{};
    c.fd=fd;c.generation=gen;c.acceptedAt=uint32_t(esp_timer_get_time()/1000);return ESP_OK;
  }
  return ESP_FAIL;
}
bool LiveServer::activate(Client &c) {
  if(c.buffers)return true;
  const size_t needed=sizeof(ClientBuffers)+(stateSize_?0:sizeof(StateBuffer)+LIVE_STATE_CAPACITY+4);
  if(heapFreeBytes()<needed+MemoryGuard::HealthyFree || heapMaxBlockBytes()<needed+MemoryGuard::HealthyBlock) {
    ++admissionRejected_;c.closing=true;return false;
  }
  c.buffers.reset(new(std::nothrow) ClientBuffers{});
  if(!c.buffers){++admissionRejected_;c.closing=true;return false;}
  return true;
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
      if(c.fd==fd){if(c.buffers)releaseSnapshot(*c.buffers,uint32_t(esp_timer_get_time()/1000));const auto generation=c.generation+1;c=Client{};c.generation=generation;}
      any|=c.fd>=0&&bool(c.buffers);
    }
    if(!any){for(auto &state:self.states_)state.reset();self.stateSize_=self.metadataSize_=0;}
  }
  // This session has already ended. A graceful FIN can otherwise keep its
  // unacknowledged telemetry in lwIP while the browser opens a replacement.
  // Release those queues before reconnects consume the control headroom.
  // Do not take lwIP's core lock while holding mutex_.
  abortTcpSocket(fd);
  ::close(fd);
}
LiveServer::StateBuffer *LiveServer::writableState(unsigned &index,size_t size,bool metadata) {
  // Grow only at a frame boundary, in bounded 256-byte classes. Small rare
  // projections no longer reserve a 3 KiB frame per stalled consumer.
  const size_t capacity=(size+4+255)&~size_t(255);
  const unsigned limit=(metadata?stateSize_:metadataSize_)?unsigned(states_.size()):4;
  const auto usable=[&](unsigned i) {
    return (!states_[i]||!states_[i]->users) &&
      !(metadata?stateSize_&&i==stateIndex_:metadataSize_&&i==metadataIndex_);
  };
  if(!usable(index)) {
    index=unsigned(states_.size());
    for(unsigned i=0;i<limit;++i)if(usable(i)){index=i;break;}
    if(index==states_.size())return nullptr;
  }
  auto &state=states_[index];
  const size_t needed=(!state?sizeof(StateBuffer):0)+(!state||state->capacity<capacity?capacity:0);
  if(needed&&(heapFreeBytes()<needed+MemoryGuard::HealthyFree || heapMaxBlockBytes()<needed+MemoryGuard::HealthyBlock))return nullptr;
  if(!state)state.reset(new(std::nothrow) StateBuffer{});
  if(!state)return nullptr;
  if(state->capacity<capacity) {
    std::unique_ptr<char[]> bytes(new(std::nothrow) char[capacity]);
    if(!bytes)return nullptr;
    state->bytes=std::move(bytes);state->capacity=capacity;
  }
  return state.get();
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
  {Guard lock(self.mutex_);
    bool found=false;for(auto &c:*self.clients_)if(c.fd==fd){
      found=httpd_ws_get_fd_info(self.server_,fd)==HTTPD_WS_CLIENT_WEBSOCKET&&self.activate(c);break;
    }
    if(!found)return ESP_FAIL;
  }
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
  if(std::string_view(input.data,input.size)=="v=2&telemetry=1") {
    Guard lock(self.mutex_);
    for(auto &c:*self.clients_)if(c.fd==fd&&!c.controlOnly&&!c.closing) {
      c.binary=true;c.revision=0;c.metadataRevision=0;return ESP_OK;
    }
    return ESP_FAIL;
  }
  // Select before the first command, without needing a boot ID from a large
  // telemetry frame. IDF can finish the upgrade without calling our GET handler.
  if(std::string_view(input.data,input.size)=="v=1&control=1") {
    Guard lock(self.mutex_);
    for(auto &c:*self.clients_)if(c.fd==fd) {
      c.controlOnly=true;c.binary=false;c.revision=0;return ESP_OK;
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
    { Guard lock(mutex_);if(client.fd<0||client.closing||!client.buffers)continue;
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
      { Guard lock(mutex_); if(client.fd<0||!client.buffers||(client.closing&&(pass||!client.buffers->stop.size)))continue;
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
    Guard lock(mutex_);if(client.fd<0||client.closing||!client.buffers)continue;
    for(auto &p:client.buffers->pending)if(p.cid){const auto result=runtime_.hud.result(p.request).result;if(result!=Result::Deferred){
      char body[80];std::snprintf(body,sizeof body,"{\"ok\":%s,\"result\":\"%s\"}",result==Result::Ok||result==Result::Pending?"true":"false",Controller::resultName(result));
      reply(client,p.cid,body);p={};
    }}
  }
}
void LiveServer::publish(uint32_t now) {
  if(!server_)return;
  bool any=false,legacy=false,binary=false,missingState=false;{Guard lock(mutex_);
    for(auto &c:*clients_)if(c.fd>=0&&c.buffers) {
      any=true;legacy|=!c.controlOnly&&!c.binary;binary|=!c.controlOnly&&c.binary;
    }
    // Keep only current projections with consumers; slow peers retain their
    // immutable frame until its last byte has been written.
    for(unsigned i=0;i<states_.size();++i)if(states_[i]&&!states_[i]->users&&
        !(legacy&&stateSize_&&i==stateIndex_)&&!(binary&&metadataSize_&&i==metadataIndex_)&&
        uint32_t(now-states_[i]->unusedAt)>=1000) {
      states_[i].reset();if(i==stateIndex_)stateSize_=0;if(i==metadataIndex_)metadataSize_=0;
    }
    selection_=runtime_.hud.selectionEpoch;
    ingressRange_[0]=runtime_.controller.azRange;ingressRange_[1]=runtime_.controller.elRange;ingressEl_=runtime_.controller.model==Model::G5500;
    for(unsigned i=0;i<2;++i) {
      ingressReady_[i]=runtime_.controller.axes[i].calibrated &&
        (!runtime_.controller.variableSpeed(i) || runtime_.controller.motionSettings[i].ready);
      ingressSpeedLimit_[i]=runtime_.controller.runtimeSpeedLimit(i);
    }
    missingState=(legacy&&(!stateSize_||legacyPending_))||(binary&&!metadataSize_)||!revision_;
  }
  // The handshake/expiry pump below still runs with zero consumers. It does
  // not need a state fingerprint, float conversion or JSON projection.
  const uint32_t critical=any?(++controlChecks_,liveControlFingerprint(runtime_.controller,runtime_.hud)):controlHash_;
  const bool limited=networkLowMemory();
  const uint32_t interval=limited?500:WebPositionIntervalMs;
  const bool due=limited?uint32_t(now-publishedAt_)>=500:
    critical!=controlHash_||(uint32_t(now-publishedAt_)>=interval&&uint32_t(now-sampledAt_)>=std::min(sampleDelay_,interval));
  if(any&&(missingState||due)) {
    sampledAt_=now;sampleDelay_=interval;
    // One temporary JSON buffer, only when a legacy snapshot or changed
    // metadata is needed. v2 position-only ticks never serialize JSON.
    char data[LIVE_STATE_CAPACITY];
    const uint32_t metaHash=(binary||legacy)?liveMetadataFingerprint(runtime_.controller,runtime_.hud):metadataHash_;
    bool metadataReady=true;
    if(binary&&(!metadataSize_||metaHash!=metadataHash_)) {
      JsonWriter out(data,sizeof data);++metadataEncodes_;
      const uint32_t next=metadataRevision_+1;
      out << "{\"v\":2,\"type\":\"metadata\",\"boot\":" << jsonQuoted(boot_) << ",\"revision\":" << next << ",\"state\":";
      writeLiveMetadata(out,runtime_.controller,runtime_.hud);out << '}';
      Guard lock(mutex_);unsigned index=metadataIndex_;
      auto *state=out.good()?writableState(index,out.size(),true):nullptr;
      metadataReady=state!=nullptr;
      if(state) {
        std::memcpy(state->bytes.get()+4,out.data(),out.size());state->size=out.size();state->revision=next;state->unusedAt=now;
        writeLiveFrameHeader(state->bytes.get(),state->size,HTTPD_WS_TYPE_TEXT);
        metadataIndex_=index;metadataSize_=state->size;metadataRevision_=next;metadataHash_=metaHash;
      }
    }
    char sample[LIVE_BINARY_SIZE];
    writeLiveBinary(sample,runtime_.controller,runtime_.hud,now,0,metadataRevision_,limited);
    const bool changed=std::memcmp(sample+8,binary_.data()+8,LIVE_BINARY_SIZE-8)!=0||critical!=controlHash_||metaHash!=sampleMetadataHash_;
    // The old 250 ms unchanged-state heartbeat and 500 ms pressure interval
    // are unchanged. A no-change check at 200 ms waits until that heartbeat.
    if(metadataReady&&(missingState||changed||uint32_t(now-publishedAt_)>=250)) {
      const auto next=revision_+1;
      if(legacy) {
        JsonWriter body(data,sizeof data);++stateEncodes_;
        body << "{\"v\":1,\"type\":\"state\",\"boot\":" << jsonQuoted(boot_) << ",\"revision\":" << next << ",\"state\":";
        writeLiveState(body,runtime_.controller,runtime_.hud,now);body << '}';
        Guard lock(mutex_);unsigned index=stateIndex_;
        auto *state=body.good()?writableState(index,body.size(),false):nullptr;
        legacyPending_=state==nullptr;
        if(state) {
          std::memcpy(state->bytes.get()+4,body.data(),body.size());state->size=body.size();state->revision=next;state->unusedAt=now;
          writeLiveFrameHeader(state->bytes.get(),state->size,HTTPD_WS_TYPE_TEXT);
          stateIndex_=index;stateSize_=state->size;
        }
      }
      {
        Guard lock(mutex_);
        std::memcpy(binary_.data(),sample,LIVE_BINARY_SIZE);
        for(unsigned i=0;i<4;++i)binary_[4+i]=char(next>>(8*i));
        publishedAt_=now;revision_=next;controlHash_=critical;sampleMetadataHash_=metaHash;binaryMetadataRevision_=metadataRevision_;
      }
    } else if(metadataReady) {
      controlHash_=critical;sampleDelay_=250-std::min<uint32_t>(250,uint32_t(now-publishedAt_));
    }
  }
  bool queue=false;
  {Guard lock(mutex_);
    bool needed=false;for(const auto &c:*clients_)needed|=c.fd>=0&&(c.closing||!c.buffers||c.buffers->outgoing.active()||c.buffers->pongPending||c.buffers->count||c.buffers->cancelledFrom||
        (c.controlOnly&&uint32_t(now-c.controlSentAt)>=500)||
        ((c.revision!=currentRevision(c)||(c.binary&&metadataSize_&&c.metadataRevision!=metadataRevision_))&&(!c.ackWaiting||uint32_t(now-c.ackCheckedAt)>=20)));
    if(needed&&!workPending_&&uint32_t(now-wakeAt_)>=5){workPending_=true;wakeAt_=now;queue=true;}
  }
  if(queue&&httpd_queue_work(server_,sendWork,this)!=ESP_OK){Guard lock(mutex_);workPending_=false;++queueErrors_;}
}
void LiveServer::writeDiagnostics(JsonWriter &j) {
  if(!server_)return;
  Guard lock(mutex_);unsigned clients=0,buffers=0;
  for(const auto &c:*clients_){clients+=c.fd>=0;buffers+=bool(c.buffers);}
  unsigned states=0;size_t stateBytes=0;for(const auto &state:states_)if(state){++states;stateBytes+=sizeof(StateBuffer)+state->capacity;}
  j<<",\"liveBinaryVersion\":2,\"liveControlChannel\":true,\"liveHeartbeat\":true,\"liveClients\":"<<clients<<",\"liveRevision\":"<<revision_<<",\"liveStateBytes\":"<<stateSize_
   <<",\"liveBinaryBytes\":"<<LIVE_BINARY_SIZE<<",\"liveMetadataBytes\":"<<metadataSize_
   <<",\"liveControlChecks\":"<<controlChecks_<<",\"liveStateEncodes\":"<<stateEncodes_<<",\"liveMetadataEncodes\":"<<metadataEncodes_
   <<",\"liveStateBuffers\":"<<states<<",\"liveBufferBytes\":"<<(sizeof(Clients)+buffers*sizeof(ClientBuffers)+stateBytes)
   <<",\"liveHandshakeTimeouts\":"<<handshakeTimeouts_<<",\"liveAdmissionRejected\":"<<admissionRejected_
   <<",\"networkMemoryLimited\":"<<(networkLowMemory()?"true":"false")<<",\"telemetryIntervalMs\":"<<(networkLowMemory()?500:WebPositionIntervalMs)
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
      if(httpd_ws_get_fd_info(self.server_,fd)!=HTTPD_WS_CLIENT_WEBSOCKET) {
        if(uint32_t(uint32_t(esp_timer_get_time()/1000)-client.acceptedAt)<5000)continue;
        ++self.handshakeTimeouts_;close=true;
      } else if(!self.activate(client))close=true;
      if(close){client.closing=true;}
      else {
      const auto &b=*client.buffers;
      checkSpace=!client.closing&&!client.controlOnly&&!b.outgoing.active()&&!b.pongPending&&!b.cancelledFrom&&!b.count;
      }
    }
    if(close){httpd_sess_trigger_close(self.server_,fd);continue;}
    // Never take lwIP's core lock while holding the controller/HTTPD mutex.
    const size_t capacity=checkSpace?tcpPeerFrameCapacity(fd):size_t(-1);
    {Guard lock(self.mutex_);
      // The loop can publish a larger revision while the TCP snapshot is read.
      // Compare its current size here, rather than admitting an older length.
      const auto currentRevision=self.currentRevision(client);
      const bool binaryReady=self.binaryMetadataRevision_==self.metadataRevision_;
      const bool needsMetadata=client.binary&&self.metadataSize_&&client.metadataRevision!=self.metadataRevision_;
      const size_t pendingSize=client.binary&&!binaryReady?0:needsMetadata?self.metadataSize_:client.binary?LIVE_BINARY_SIZE:self.stateSize_;
      const bool waitingSpace=checkSpace&&(needsMetadata||client.revision!=currentRevision)&&pendingSize&&
          capacity<pendingSize+(pendingSize<126?2:4);
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
        else if((client.controlOnly&&(client.revision!=currentRevision||uint32_t(uint32_t(esp_timer_get_time()/1000)-client.controlSentAt)>=500))||
            (!client.controlOnly&&(needsMetadata||client.revision!=currentRevision)&&pendingSize&&!waitingSpace)){
          if(client.controlOnly) {
            const int size=std::snprintf(data,ControlCapacity,"{\"v\":1,\"type\":\"control\",\"boot\":\"%s\",\"selection\":%lu,\"memoryLimited\":%s}",self.boot_,(unsigned long)self.selection_,networkLowMemory()?"true":"false");
            if(size>0&&size_t(size)<ControlCapacity)n=size_t(size);
            client.controlSentAt=uint32_t(esp_timer_get_time()/1000);
          } else if(client.binary&&!needsMetadata) {
            std::memcpy(data,self.binary_.data(),LIVE_BINARY_SIZE);n=LIVE_BINARY_SIZE;opcode=HTTPD_WS_TYPE_BINARY;
          } else {
            auto &state=*self.states_[needsMetadata?self.metadataIndex_:self.stateIndex_];
            if(out.borrow(state.bytes.get(),state.size,uint32_t(esp_timer_get_time()/1000))){
              ++state.users;b.snapshot=&state;b.memoryWaits=0;
            }
            if(needsMetadata)client.metadataRevision=self.metadataRevision_;
          }
          if(!needsMetadata||client.controlOnly)client.revision=currentRevision;
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
      if(out.borrowed&&out.requested()-out.written()>256&&!networkBulkTxBudget(out.requested()-out.written())&&tcpPeerFrameCapacity(fd)!=0) {
        out.pause(now);Guard lock(self.mutex_);++self.memoryDeferred_;++client.buffers->memoryWaits;continue;
      }
      bool memoryWait=false;
      const auto step=out.pump(now,[=,&memoryWait](const char *data,size_t size)->int{
        if(size>256){
          size=networkBulkTxBudget(size);
          if(!size){memoryWait=true;errno=EAGAIN;return -1;}
        }
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
