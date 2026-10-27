// Host adapter for exercising the production bridge against a real MQTT broker.
#include "mqtt_bridge.hpp"
#include "motion_fixture.hpp"
#include <iostream>
#include <sstream>
#include <map>
using namespace forkrot;
struct Host {
 Runtime r;
 std::map<uint64_t,std::string> registry;
 MqttBridge bridge;
 static bool publish(void *,const char *topic,std::string_view data,bool retain,int qos){std::cout<<"P\t"<<qos<<'\t'<<retain<<'\t'<<topic<<'\t'<<data<<'\n';return true;}
 static bool read(void *p,uint64_t id,char *out,size_t cap){auto &h=*static_cast<Host *>(p);auto it=h.registry.find(id);if(it==h.registry.end()||it->second.size()>=cap)return false;std::strcpy(out,it->second.c_str());return true;}
 static bool write(void *p,uint64_t id,const char *prefix){auto &h=*static_cast<Host *>(p);if(prefix)h.registry[id]=prefix;else h.registry.erase(id);return true;}
 Host():bridge(r,0x42,{this,publish,nullptr,nullptr,read,write}){
  r.controller=testController();r.controller.restoreMode(Mode::Auto);r.controller.feedback(20,30,true,true);r.hud.config.role=DeviceRole::Controller;r.hud.begin(0x42,0x1234);
  r.mqtt.enabled=true;std::strcpy(r.mqtt.host,"localhost");
 }
};
int main(){Host h;std::string line;while(std::getline(std::cin,line)){
 std::istringstream s(line);char op;unsigned at,retained;s>>op;
 if(op=='C'){unsigned online;s>>online;h.bridge.connection(online);}
 else if(op=='T'){s>>at;h.bridge.service(at);}
 else if(op=='R'){std::string topic,payload;s>>retained>>at>>topic;std::getline(s,payload);if(!payload.empty())payload.erase(0,1);h.bridge.receive(topic,payload,retained,at);}
 else if(op=='S'){char data[6400];JsonWriter j(data,sizeof data);writeStateJson(j,h.r.controller,h.r.protocol,h.r.loggerSettings,0,false);std::cout<<"S\t"<<std::string_view(data,j.size())<<'\n';}
 std::cout<<"D\t"<<h.bridge.commands()<<'\t'<<h.bridge.lastResult()<<std::endl;
}}
