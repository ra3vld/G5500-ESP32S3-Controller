#include "mqtt_publish_mailbox.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
using namespace forkrot;
int main() {
  MqttPublishMailbox box;unsigned sent=0;
  const auto send=[&](const auto &m){++sent;assert(std::string_view(m.topic)=="state");return true;};
  assert(box.empty()&&box.offer("state","old",true,0,1,10));
  assert(!box.offer("other","value",true,0,1,11));
  assert(box.offer("state","latest",true,0,1,12));
  box.drain(1,13,[&](const auto &m){assert(std::string_view(m.payload,m.size)=="latest");return send(m);});
  assert(sent==1&&box.empty());
  assert(box.offer("state","retry",true,1,1,20));
  box.drain(1,21,[](const auto &){return false;});assert(!box.empty());
  box.drain(1,22,send);assert(sent==2&&box.empty());
  // Session changes and obsolete telemetry never replay after reconnect.
  assert(box.offer("state","stale",true,1,1,30));box.drain(2,31,send);assert(sent==2&&box.empty());
  assert(box.offer("state","stale",true,0,2,UINT32_MAX-100));box.drain(2,5000,send);assert(sent==2&&box.empty());
  assert(box.offer("state","",true,1,2,40));box.drain(2,41,[&](const auto &m){assert(m.size==0);return true;});
  assert(!box.offer(nullptr,"",true,1,2,42));
  assert(!box.offer(std::string(192,'a').c_str(),"",true,1,2,42));
  assert(!box.offer("state",std::string(1025,'a'),true,1,2,42));
  // A sender blocked by the SDK never holds a lock needed by the producer.
  assert(box.offer("state","blocked",true,1,2,50));std::atomic<bool> entered{false},release{false};
  std::thread worker([&]{box.drain(2,51,[&](const auto &){entered=true;while(!release.load())std::this_thread::yield();return true;});});
  while(!entered.load())std::this_thread::yield();
  for(unsigned i=0;i<10000;++i)assert(!box.offer("state","new",true,1,2,52));
  release=true;worker.join();assert(box.empty());
  // Concurrent publication/coalescing retains complete immutable messages.
  std::atomic<bool> done{false};
  std::thread reader([&]{while(!done.load()||!box.empty())box.drain(3,0,[](const auto &m){
    assert(m.size==1024);for(unsigned i=0;i<m.size;++i)assert(m.payload[i]==m.payload[0]);return true;
  });});
  for(unsigned i=0;i<10000;++i){const std::string value(1024,char('a'+i%26));while(!box.offer("state",value,true,1,3,0))std::this_thread::yield();}
  done=true;reader.join();
  std::cout<<"PASS: bounded MQTT slot, immutable/coalesced values, retry, empty deletion, rollover/epoch expiry and independent controller during blocked SDK writes\n";
}
