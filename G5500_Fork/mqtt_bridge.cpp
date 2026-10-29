// Bounded scalar commands, retained discovery and per-destination telemetry. GPL-3.0.
#include "mqtt_bridge.hpp"
#if MQTT_SUPPORT
#include <cstdio>
#include <cmath>
#include <new>
#include <algorithm>
namespace forkrot {
namespace {
bool hexId(std::string_view text,uint64_t &value) {
  if(text.size()!=12)return false;
  value=0;
  for(char c:text) {
    const int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
    if(n<0)return false;
    value=(value<<4)|unsigned(n);
  }
  return value!=0;
}
void idText(char *out,size_t capacity,uint64_t node) {
  if(node)std::snprintf(out,capacity,"%012llx",static_cast<unsigned long long>(node));
  else std::snprintf(out,capacity,"selected");
}
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
void haName(JsonWriter &j,const char *name) {
  bool any=false;for(const unsigned char *p=reinterpret_cast<const unsigned char *>(name);*p;++p)if(*p>=32)any=true;
  j << '"';
  if(any){for(const char *p=name;*p;++p)if(static_cast<unsigned char>(*p)>=32)j << jsonEscaped({p,1});}
  else j << "G5500";
  j << '"';
}
struct Entity { const char *domain,*id,*name,*field,*command;int axis; };
constexpr Entity entities[MqttBridge::EntityCount]={
  {"sensor","az","Азимут","az.current",nullptr,0},
  {"sensor","el","Элевация","el.current",nullptr,1},
  {"binary_sensor","moving","Движение","moving",nullptr,-1},
  {"sensor","fault","Ошибка","fault",nullptr,-1},
  {"binary_sensor","pending","Ожидает подтверждения","pending",nullptr,-1},
  {"number","target_az","Цель AZ","az.target","target_az",0},
  {"number","target_el","Цель EL","el.target","target_el",1},
  {"number","speed_az","Скорость AZ","az.speed","speed_az",0},
  {"number","speed_el","Скорость EL","el.speed","speed_el",1},
  {"select","mode","Режим","mode","mode",-1},
  {"button","stop","STOP",nullptr,"stop",-1},
  {"button","stop_az","STOP AZ",nullptr,"stop_az",0},
  {"button","stop_el","STOP EL",nullptr,"stop_el",1},
  {"button","execute","Подтвердить цель",nullptr,"execute",-1},
  {"button","preset_1","Пресет 1",nullptr,"preset_1",-1},
  {"button","preset_2","Пресет 2",nullptr,"preset_2",-1},
  {"button","preset_3","Пресет 3",nullptr,"preset_3",-1},
  {"button","preset_4","Пресет 4",nullptr,"preset_4",-1},
  {"sensor","name","Поворотка","name",nullptr,-1},
  {"sensor","node","Идентификатор","node",nullptr,-1}
};
#endif
}
MqttBridge::MqttBridge(Runtime &runtime,uint64_t gateway,Callbacks callbacks)
    :runtime_(runtime),gateway_(gateway),callbacks_(callbacks) {
  std::snprintf(base_,sizeof base_,"g5500/%012llx",static_cast<unsigned long long>(gateway));
  selection_.store(runtime_.hud.selectionEpoch);
}
void MqttBridge::connection(bool online) {
  if(!online)session_.fetch_add(1);
  connected_.store(online);
}
bool MqttBridge::receive(std::string_view topic,std::string_view payload,bool retained,uint32_t at) {
  const auto reject=[&](){rejected_.fetch_add(1);return false;};
  char birth[64];std::snprintf(birth,sizeof birth,"%s/status",runtime_.mqtt.discoveryPrefix);
  // Config is immutable throughout this bridge's lifetime; Runtime/Hud are loop-owned.
  if(topic==birth) {if(payload=="online")rediscover_.store(true);return true;}
  if(retained || !connected_.load() || payload.empty() || payload.size()>32)return reject();
  const std::string_view base(base_);
  if(topic.size()<=base.size() || topic.substr(0,base.size())!=base || topic[base.size()]!='/')return reject();
  topic.remove_prefix(base.size()+1);
  const auto slash=topic.find('/');if(slash==topic.npos)return reject();
  Request request;request.selected=topic.substr(0,slash)=="selected";
  if(!request.selected && !hexId(topic.substr(0,slash),request.node))return reject();
  topic.remove_prefix(slash+1);
  if(topic.substr(0,4)!="set/")return reject();
  topic.remove_prefix(4);
  if(topic=="target_az" || topic=="target_el" || topic=="speed_az" || topic=="speed_el") {
    request.op=topic.substr(0,6)=="target"?Operation::Target:Operation::Speed;
    request.axis=topic.substr(topic.size()-2)=="az"?0:1;
    if(!number(payload,request.value) || !std::isfinite(request.value))return reject();
  } else if(topic=="mode") {
    request.op=Operation::Mode;
    if(payload=="manual")request.mode=Mode::Manual;
    else if(payload=="auto")request.mode=Mode::Auto;
    else if(payload=="auto_1h")request.mode=Mode::AutoHour;
    else return reject();
  } else {
    if(payload!="PRESS")return reject();
    if(topic=="stop" || topic=="stop_az" || topic=="stop_el") {
      request.op=Operation::Stop;request.axis=topic=="stop"?-1:topic=="stop_az"?0:1;
    } else if(topic=="execute")request.op=Operation::Execute;
    else if(topic.size()==8 && topic.substr(0,7)=="preset_" && topic[7]>='1' && topic[7]<='4') {
      request.op=Operation::Preset;request.slot=uint8_t(topic[7]-'1');
    } else return reject();
  }
  request.selection=selection_.load();request.session=session_.load();request.at=at;request.serial=serial_.fetch_add(1)+1;
  if(lock_.test_and_set()){dropped_.fetch_add(1);return false;}
  bool ok=false;
  if(request.op==Operation::Stop) {
    for(unsigned i=0;i<stopCount_;++i)if(stops_[i].node==request.node && stops_[i].selected==request.selected && stops_[i].axis==request.axis) {
      stops_[i]=request;ok=true;break;
    }
    if(!ok && stopCount_<stops_.size()){stops_[stopCount_++]=request;ok=true;}
  } else if(count_<queue_.size()) {
    queue_[(head_+count_++)%queue_.size()]=request;ok=true;
  }
  lock_.clear();if(!ok)dropped_.fetch_add(1);return ok;
}
bool MqttBridge::pop(Request &out) {
  if(lock_.test_and_set())return false;
  bool ready=false;
  if(stopCount_){
    out=stops_[0];for(unsigned i=1;i<stopCount_;++i)stops_[i-1]=stops_[i];--stopCount_;ready=true;
    // Queue ownership covers the STOP and cancellation as one operation.
    // pop() runs only in the main loop; callbacks never inspect Hud.
    unsigned kept=0;
    for(unsigned i=0;i<count_;++i) {
      const auto &q=queue_[(head_+i)%queue_.size()];
      const auto destination=out.selected?runtime_.hud.config.selected:out.node;
      const auto target=q.selected?runtime_.hud.config.selected:q.node;
      const bool same=target==destination && (!out.selected || out.selection==runtime_.hud.selectionEpoch);
      const bool affects=out.axis<0 || q.axis==out.axis || q.op==Operation::Mode || q.op==Operation::Execute || q.op==Operation::Preset;
      if(same && affects && q.session==out.session && int32_t(q.serial-out.serial)<=0)continue;
      queue_[(head_+kept++)%queue_.size()]=q;
    }
    count_=kept;
  }
  else if(count_){out=queue_[head_];head_=(head_+1)%queue_.size();--count_;ready=true;}
  lock_.clear();return ready;
}
bool MqttBridge::wanted(uint64_t node) const {
  if(!runtime_.hud.isHud())return node==gateway_;
  if(!node)return true;
  const auto *peer=runtime_.hud.peer(node);
  return peer && peer->saved && (!peer->simulated() || runtime_.hud.config.simulationEnabled);
}
bool MqttBridge::fresh(uint64_t node,uint32_t at) const {
  if(runtime_.recovery)return false;
  if(!runtime_.hud.isHud())return node==gateway_;
  return runtime_.hud.available(node?node:runtime_.hud.config.selected,at);
}
const HudState *MqttBridge::state(uint64_t node,uint32_t at) {
  if(!runtime_.hud.isHud()) {
    if(node!=gateway_)return nullptr;
    local_.capture(runtime_.controller,runtime_.protocol,at);return &local_;
  }
  const auto *peer=runtime_.hud.peer(node?node:runtime_.hud.config.selected);
  return peer && peer->saved?&peer->state:nullptr;
}
const char *MqttBridge::nodeName(uint64_t node,const HudState *snapshot) const {
  if(!node)return "Текущая выбранная поворотка";
  const auto *peer=runtime_.hud.isHud()?runtime_.hud.peer(node):nullptr;
  return peer && peer->label[0]?peer->label:snapshot?snapshot->name:"Поворотка";
}
MqttBridge::Node *MqttBridge::ensureNode(uint64_t id) {
  auto *link=&nodes_;
  while(*link){if((*link)->id==id)return link->get();link=&(*link)->next;}
  if(callbacks_.allocate && !callbacks_.allocate(callbacks_.context,sizeof(Node)))return nullptr;
  link->reset(new(std::nothrow) Node);if(!*link){lastResult_="mqtt_memory_limited";return nullptr;}
  (*link)->id=id;return link->get();
}
MqttBridge::Node *MqttBridge::nextNode(Node *node) const {
  return node && node->next?node->next.get():nodes_.get();
}
size_t MqttBridge::nodeCount() const {size_t count=0;for(auto *p=nodes_.get();p;p=p->next.get())++count;return count;}
void MqttBridge::execute(const Request &request,uint32_t at) {
  if(!connected_.load() || request.session!=session_.load() || uint32_t(at-request.at)>=2000) {lastResult_="expired";return;}
  if(request.selected && (!runtime_.hud.isHud() || request.selection!=runtime_.hud.selectionEpoch)) {lastResult_="selection_changed";return;}
  const uint64_t destination=request.selected?runtime_.hud.config.selected:request.node;
  if(!wanted(request.selected?0:destination) || !fresh(request.selected?0:destination,at)){lastResult_="unavailable";return;}
  if(callbacks_.beforeCommand)callbacks_.beforeCommand(callbacks_.context);
  ControlCommand command;command.axis=request.axis;
  switch(request.op) {
  case Operation::Target:command.op=ControlOp::Target;command.external=true;command.target={request.axis==0?request.value:0,request.axis==1?request.value:0,request.axis==0,request.axis==1};break;
  case Operation::Speed:command.op=ControlOp::RuntimeSpeed;command.speed=request.value;break;
  case Operation::Mode:command.op=ControlOp::Mode;command.mode=request.mode;break;
  case Operation::Stop:command.op=ControlOp::Stop;break;
  case Operation::Execute:command.op=ControlOp::Execute;break;
  case Operation::Preset:command.op=ControlOp::Preset;command.slot=request.slot;break;
  }
  Result result=Result::Unavailable;
  if(runtime_.hud.isHud()) {
    result=runtime_.hud.submitTo(destination,command,at);
    if(result==Result::Deferred){pendingRequest_=runtime_.hud.requestId();pendingNode_=destination;}
  } else {
    auto &c=runtime_.controller;
    switch(command.op) {
    case ControlOp::Target:result=c.target(command.target,true,at);break;
    case ControlOp::RuntimeSpeed:result=c.setRuntimeSpeed(unsigned(command.axis),command.speed,at);break;
    case ControlOp::Mode:result=c.setMode(command.mode,at);break;
    case ControlOp::Stop:c.stop(command.axis);result=Result::Ok;break;
    case ControlOp::Execute:result=c.execute(at);break;
    case ControlOp::Preset:result=c.preset(command.slot,at);break;
    default:break;
    }
    runtime_.targetValid=c.targetKnown;
  }
  ++commands_;lastResult_=Controller::resultName(result);
  if(callbacks_.afterCommand)callbacks_.afterCommand(callbacks_.context,!runtime_.hud.isHud() && request.op==Operation::Mode && result==Result::Ok);
  for(auto *n=nodes_.get();n;n=n->next.get())if(n->id==destination || (!n->id && runtime_.hud.config.selected==destination))n->sentAt=at-1000;
}
bool MqttBridge::publish(const char *topic,JsonWriter &j,bool retained,int qos) {
  if(!j.good()){lastResult_="mqtt_payload_too_large";return false;}
  return callbacks_.publish && callbacks_.publish(callbacks_.context,topic,{j.data(),j.size()},retained,qos);
}
bool MqttBridge::publishState(Node &node,uint32_t at,bool reset) {
  const auto *snapshot=state(node.id,at);
  const bool available=!reset && fresh(node.id,at);
  char target[17],source[17];idText(target,sizeof target,node.id);
  const uint64_t actual=node.id?node.id:runtime_.hud.config.selected;idText(source,sizeof source,actual);
  std::snprintf(topic_,sizeof topic_,"%s/%s/state",base_,target);
  JsonWriter j(json_,sizeof json_);
  j << "{\"available\":" << (available?"true":"false") << ",\"name\":" << jsonQuoted(snapshot?snapshot->name:"")
    << ",\"node\":" << jsonQuoted(actual?source:"") << ",\"mode\":" << jsonQuoted(snapshot?Controller::modeName(snapshot->mode):"manual")
    << ",\"fault\":" << jsonQuoted(snapshot?snapshot->fault:"") << ",\"pending\":" << (snapshot && snapshot->pendingValid?"true":"false")
    << ",\"moving\":" << (snapshot && (snapshot->axes[0].direction || snapshot->axes[1].direction || snapshot->axes[0].tracking || snapshot->axes[1].tracking)?"true":"false");
  for(unsigned axis=0;axis<2;++axis) {
    const bool supported=snapshot && (axis==0 || snapshot->model==Model::G5500);
    j << (axis?",\"el\":{":",\"az\":{") << "\"current\":";
    if(supported && snapshot->axes[axis].calibrated)j.decimal(snapshot->axes[axis].current,2);else j << "null";
    j << ",\"target\":";
    const bool pending=supported && snapshot->pendingValid && (axis?snapshot->pending.hasEl:snapshot->pending.hasAz);
    if(pending)j.decimal(axis?snapshot->pending.el:snapshot->pending.az,2);
    else if(supported && snapshot->axes[axis].targetKnown)j.decimal(snapshot->axes[axis].target,2);else j << "null";
    j << ",\"pending\":";
    if(pending)j.decimal(axis?snapshot->pending.el:snapshot->pending.az,2);else j << "null";
    j << ",\"speed\":";if(supported)j.decimal(snapshot->runtimeSpeed[axis],3);else j << "null";
    j << ",\"adjustable\":" << (supported && snapshot->speedAvailable[axis]?"true":"false") << '}';
  }
  j << '}';
  if(!publish(topic_,j,true,0))return false;
  node.sentAt=at;node.selection=runtime_.hud.selectionEpoch;return true;
}
uint32_t MqttBridge::metadata(const HudState *s,uint64_t node) const {
  if(!s)return 0;
  uint32_t hash=2166136261u;
  const auto add=[&](const void *p,size_t n){auto *b=static_cast<const uint8_t *>(p);while(n--)hash=(hash^*b++)*16777619u;};
  add(&s->model,sizeof s->model);add(&s->minimum,sizeof s->minimum);add(&s->maximum,sizeof s->maximum);add(&s->elRange,sizeof s->elRange);
  add(s->speedAvailable,sizeof s->speedAvailable);add(s->speedLimit,sizeof s->speedLimit);
  for(const auto &m:s->motion){add(&m.mode,sizeof m.mode);add(&m.ready,sizeof m.ready);add(&m.kickEnabled,sizeof m.kickEnabled);add(&m.holdEnabled,sizeof m.holdEnabled);}
  for(const auto &p:s->presets)add(p.label,std::strlen(p.label));
  const char *name=nodeName(node,s);add(name,std::strlen(name));
  return hash;
}
void MqttBridge::discoveryTopic(const char *prefix,uint64_t node,unsigned entity) {
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
  char target[17];idText(target,sizeof target,node);
  std::snprintf(topic_,sizeof topic_,"%s/%s/%012llx/%s_%s/config",prefix,entities[entity].domain,
      static_cast<unsigned long long>(gateway_),target,entities[entity].id);
#else
  (void)prefix;(void)node;(void)entity;
#endif
}
bool MqttBridge::discovery(Node &node,uint32_t at) {
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
  const bool present=runtime_.mqtt.discovery && wanted(node.id);
  const auto *snapshot=state(node.id,at);
  const auto &config=runtime_.mqtt;
  if(!node.checked) {
    oldPrefix_[0]=0;
    node.registered=callbacks_.registryRead && callbacks_.registryRead(callbacks_.context,node.id,oldPrefix_,sizeof oldPrefix_);
    node.cleaning=node.registered && (!present || std::strcmp(oldPrefix_,config.discoveryPrefix));
    if(present && !node.registered) {
      if(callbacks_.registryWrite && !callbacks_.registryWrite(callbacks_.context,node.id,config.discoveryPrefix)){lastResult_="mqtt_registry_failed";return false;}
      node.registered=true;
    }
    node.checked=true;node.entity=0;
  }
  if(node.registered && !present && !node.cleaning){node.cleaning=true;node.entity=0;}
  if(node.cleaning) {
    if(node.entity<EntityCount) {
      if(!callbacks_.registryRead || !callbacks_.registryRead(callbacks_.context,node.id,oldPrefix_,sizeof oldPrefix_))return false;
      discoveryTopic(oldPrefix_,node.id,node.entity);JsonWriter j(json_,sizeof json_);
      if(!publish(topic_,j,true,1))return false;
      ++node.entity;return true;
    }
    if(callbacks_.idle && !callbacks_.idle(callbacks_.context))return false;
    if(callbacks_.registryWrite && !callbacks_.registryWrite(callbacks_.context,node.id,present?config.discoveryPrefix:nullptr))return false;
    node.registered=present;node.cleaning=false;node.entity=0;node.configured=!present;
    if(!present)return false;
  }
  if(!present || !snapshot)return false;
  const uint32_t hash=metadata(snapshot,node.id);
  if(hash!=node.meta){node.meta=hash;node.entity=0;node.configured=false;}
  if(node.configured)return false;
  const unsigned index=node.entity;const auto &entity=entities[index];
  discoveryTopic(config.discoveryPrefix,node.id,index);JsonWriter j(json_,sizeof json_);
  const bool supported=(entity.axis!=1 || snapshot->model==Model::G5500) &&
      ((index!=7 && index!=8) || snapshot->speedAvailable[unsigned(entity.axis)]);
  if(supported) {
    char destination[17],key[80],stateTopic[80],availability[64];idText(destination,sizeof destination,node.id);
    std::snprintf(key,sizeof key,"g5500_%012llx_%s",static_cast<unsigned long long>(gateway_),destination);
    std::snprintf(stateTopic,sizeof stateTopic,"%s/%s/state",base_,destination);
    std::snprintf(availability,sizeof availability,"%s/availability",base_);
    j << "{\"name\":";haName(j,index>=14 && index<=17 && snapshot->presets[index-14].label[0]?snapshot->presets[index-14].label:entity.name);
    j << ",\"unique_id\":\"" << key << '_' << entity.id << "\",\"dev\":{\"ids\":[" << jsonQuoted(key)
      << "],\"name\":";haName(j,nodeName(node.id,snapshot));j << ",\"mf\":\"G5500\"},\"o\":{\"name\":\"G5500\"}"
      << ",\"availability_mode\":\"all\",\"availability\":[{\"topic\":" << jsonQuoted(availability)
      << "},{\"topic\":" << jsonQuoted(stateTopic) << ",\"value_template\":\"{{ 'online' if value_json.available";
    if(index==7 || index==8)j << " and value_json." << (entity.axis?"el":"az") << ".adjustable";
    if(index==13)j << " and value_json.pending";
    j << " else 'offline' }}\"}]";
    if(entity.field) {
      j << ",\"state_topic\":" << jsonQuoted(stateTopic) << ",\"value_template\":\"{{ ";
      if(index==2 || index==4)j << "'ON' if value_json." << entity.field << " else 'OFF'";
      else if(index==5 || index==6)j << "value_json." << entity.field << " if value_json." << entity.field
          << " is not none else value_json." << (entity.axis?"el":"az") << ".current";
      else j << "value_json." << entity.field;
      j << " }}\"";
    }
    if(entity.command)j << ",\"command_topic\":\"" << base_ << '/' << destination << "/set/" << entity.command << "\",\"qos\":0,\"retain\":false";
    if(index==0 || index==1 || index==5 || index==6)j << ",\"unit_of_measurement\":\"°\"";
    if(index==2 || index==4)j << ",\"payload_on\":\"ON\",\"payload_off\":\"OFF\"";
    if(index>=10 && index<=17)j << ",\"payload_press\":\"PRESS\"";
    if(index==9)j << ",\"options\":[\"manual\",\"auto\",\"auto_1h\"]";
    if(index==5 || index==6)j << ",\"min\":" << (entity.axis?snapshot->elRange.start:snapshot->minimum)
        << ",\"max\":" << (entity.axis?snapshot->elRange.end:snapshot->maximum) << ",\"step\":0.1,\"mode\":\"box\"";
    if(index==7 || index==8) {
      const float limit=snapshot->speedLimit[unsigned(entity.axis)];
      j << ",\"min\":";j.decimal(limit/20.f,-1) << ",\"max\":";j.decimal(limit,-1)
        << ",\"step\":0.001,\"mode\":\"box\",\"unit_of_measurement\":"
        << jsonQuoted(snapshot->motion[unsigned(entity.axis)].mode==RegulatorMode::Cascade?"°/s":"%");
    }
    j << '}';
  }
  if(!publish(topic_,j,true,1))return false;
  if(++node.entity==EntityCount){node.configured=true;node.entity=0;}
  return true;
#else
  (void)node;(void)at;return false;
#endif
}
void MqttBridge::service(uint32_t at) {
  selection_.store(runtime_.hud.selectionEpoch);
  const bool online=connected_.load();const uint32_t epoch=session_.load();
  if(!online){observedConnection_=false;announced_=false;return;}
  if(!observedConnection_ || epoch!=observedSession_) {
    observedConnection_=true;observedSession_=epoch;announced_=false;lastScan_=at-1000;
    for(auto *n=nodes_.get();n;n=n->next.get()){n->resetSession=0;n->configured=false;n->entity=0;n->sentAt=at-1000;}
  }
  if(rediscover_.exchange(false))for(auto *n=nodes_.get();n;n=n->next.get()){n->configured=false;n->entity=0;n->sentAt=at-1000;}
  // Deliver reserved STOPs before normal requests, with bounded loop work.
  Request request;
  for(unsigned i=0;i<3 && pop(request);++i)execute(request,at);
  if(pendingRequest_) {
    const auto reply=runtime_.hud.result(pendingRequest_);
    if(reply.result!=Result::Deferred){lastResult_=Controller::resultName(reply.result);pendingRequest_=0;pendingNode_=0;}
  }
  if(uint32_t(at-lastScan_)>=1000) {
    lastScan_=at;
    if(runtime_.hud.isHud()) {
      ensureNode(0);
      for(auto *p=runtime_.hud.peers();p;p=p->next.get())if(wanted(p->id))ensureNode(p->id);
    } else ensureNode(gateway_);
  }
#if HOME_ASSISTANT_DISCOVERY_SUPPORT
  if(!registryDone_ && callbacks_.registryNext) {
    uint64_t id=0;if(callbacks_.registryNext(callbacks_.context,id))ensureNode(id);else registryDone_=true;
  }
#endif
  // Reclaim per-node metadata once retained configuration deletion is acknowledged.
  auto *link=&nodes_;
  while(*link) {
    auto &node=**link;
    if(!wanted(node.id) && node.checked && node.configured && !node.registered) {
      stateCursor_=discoveryCursor_=nullptr;auto next=std::move(node.next);*link=std::move(next);
    } else link=&node.next;
  }
  if(!nodes_)return;
  // Clear old retained availability before announcing the gateway online.
  if(!announced_) {
    if(!registryDone_ && callbacks_.registryNext)return;
    for(auto *n=nodes_.get();n;n=n->next.get())if(!n->resetSession) {
      if(publishState(*n,at,true))n->resetSession=1;
      return;
    }
    std::snprintf(topic_,sizeof topic_,"%s/availability",base_);
    JsonWriter j(json_,sizeof json_);j << "online";
    if(publish(topic_,j,true,1))announced_=true;
    return;
  }
  if(uint32_t(at-lastDiscovery_)>=50) {
    discoveryCursor_=nextNode(discoveryCursor_);
    if(discoveryCursor_ && discovery(*discoveryCursor_,at)){lastDiscovery_=at;return;}
  }
  stateCursor_=nextNode(stateCursor_);
  if(stateCursor_ && (uint32_t(at-stateCursor_->sentAt)>=1000 || (!stateCursor_->id && stateCursor_->selection!=runtime_.hud.selectionEpoch)))
    publishState(*stateCursor_,at);
}
}
#endif
