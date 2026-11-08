#include "mqtt_fixture.hpp"
int main(int argc,char**){
  Harness h;auto &c=h.runtime.controller;
  c.presets[0].label="Луна";c.presets[1].label="Солнце";
  h.run();
  const auto *config=h.config("g5500/000000000042/000000000042/config");assert(config && config->retain && config->qos==0);
  assert(config->payload.find("\"axes\":\"az_el\"")!=std::string::npos);
  assert(config->payload.find("\"direct_target\":true")!=std::string::npos);
  assert(config->payload.find("Луна")!=std::string::npos && config->payload.size()<1024);
  c.restoreMode(Mode::Manual);
  assert(h.command("move_az","80",4100));h.bridge.service(4101);
  assert(c.mode==Mode::Manual && !c.pendingValid && c.axes[0].targetKnown && c.axes[0].target==80);
  assert(h.command("move_el","0",4200));h.bridge.service(4201);
  assert(c.mode==Mode::Manual && !c.pendingValid && c.axes[1].target==0);
  assert(!h.command("move_az","NaN",4300));assert(!h.command("move_az","10",4300,true));
  c.setModel(Model::G800DXA);h.run(5000,8000);
  const auto *az=h.config("g5500/000000000042/000000000042/config");assert(az && az->payload.find("\"axes\":\"az\"")!=std::string::npos && az->payload.find("\"el\":null")!=std::string::npos);
  // A busy sender accepts one message per loop turn, not two publications at once.
  Harness slow;slow.drained=false;slow.run();slow.drained=true;slow.run(4100,8000);
  if(argc>1){for(const auto &m:h.messages){if(m.topic.size()>=7 && m.topic.substr(m.topic.size()-7)=="/config")std::cout<<m.payload<<'\n';}}
  if(argc==1)std::cout<<"PASS: controller limits/preset metadata, AZ-only contract, immediate short goals in manual mode, retained/NaN rejection\n";
}
