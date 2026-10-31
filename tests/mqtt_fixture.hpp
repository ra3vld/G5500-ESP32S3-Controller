#pragma once
#include "motion_fixture.hpp"
#include "mqtt_bridge.hpp"
#include "mqtt_config.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>
#include <deque>
#include <algorithm>
using namespace forkrot;
struct Published { std::string topic,payload;bool retain;int qos; };
struct Harness {
  Runtime runtime;
  std::vector<Published> messages;
  std::map<uint64_t,std::string> registry;
  std::vector<uint64_t> registryScan;
  size_t scan=0;
  bool writable=true,network=true,drained=true,allocations=true;
  unsigned before=0,after=0,persisted=0,publishAttempts=0;
  MqttBridge bridge;
  static Harness &self(void *p){return *static_cast<Harness *>(p);}
  static bool publish(void *p,const char *topic,std::string_view value,bool retained,int qos){
    auto &h=self(p);++h.publishAttempts;if(!h.network)return false;
    h.messages.push_back({topic,std::string(value),retained,qos});return true;
  }
  static bool idle(void *p){return self(p).drained;}
  static bool next(void *p,uint64_t &id){auto &h=self(p);if(h.scan==h.registryScan.size())return false;id=h.registryScan[h.scan++];return true;}
  static bool read(void *p,uint64_t id,char *out,size_t capacity){
    auto &h=self(p);auto it=h.registry.find(id);if(it==h.registry.end() || it->second.size()>=capacity)return false;
    std::memcpy(out,it->second.c_str(),it->second.size()+1);return true;
  }
  static bool write(void *p,uint64_t id,const char *prefix){auto &h=self(p);if(!h.writable)return false;if(prefix)h.registry[id]=prefix;else h.registry.erase(id);return true;}
  static bool allocate(void *p,size_t){return self(p).allocations;}
  static void pre(void *p){++self(p).before;}
  static void post(void *p,bool persist){++self(p).after;if(persist)++self(p).persisted;}
  Harness():bridge(runtime,0x42,{this,publish,idle,next,read,write,allocate,pre,post}){
    runtime.controller=forkrot::testController();runtime.controller.restoreMode(Mode::Auto);runtime.controller.feedback(20,30,true,true);
    runtime.hud.config.role=DeviceRole::Controller;runtime.hud.begin(0x42,0x1234);
    runtime.mqtt.enabled=true;std::strcpy(runtime.mqtt.host,"broker.local");
    bridge.connection(true);
  }
  void run(uint32_t start=0,uint32_t end=4000){for(auto at=start;at<=end;at+=10)bridge.service(at);}
  bool command(std::string_view operation,std::string_view value,uint32_t at=4100,bool retain=false,std::string_view node="000000000042"){
    return bridge.receive(std::string(bridge.baseTopic())+"/"+std::string(node)+"/set/"+std::string(operation),value,retain,at);
  }
  const Published *config(std::string_view suffix)const {
    for(auto i=messages.rbegin();i!=messages.rend();++i)if(i->topic.size()>=suffix.size() && i->topic.substr(i->topic.size()-suffix.size())==suffix)return &*i;
    return nullptr;
  }
};
