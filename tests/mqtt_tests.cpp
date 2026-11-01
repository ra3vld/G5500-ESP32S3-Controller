#include "mqtt_fixture.hpp"
int main(int argc,char **argv){
  {
    Harness h;h.run();h.network=false;h.bridge.service(5000);
    const auto attempts=h.publishAttempts;
    for(unsigned at=5001;at<5020;++at)h.bridge.service(at);
    h.runtime.controller.axes[0].direction=1;
    assert(h.command("stop","PRESS",5020));h.bridge.service(5021);
    assert(!h.runtime.controller.moving());
    for(unsigned at=5022;at<5050;++at)h.bridge.service(at);
    assert(h.publishAttempts==attempts);
    h.bridge.service(5050);assert(h.publishAttempts==attempts+1);
    h.network=true;h.bridge.service(5100);assert(h.publishAttempts>attempts+1);
  }
  {
    MqttConfig c;auto bytes=encodeMqtt(c);MqttConfig decoded;
    assert(decodeMqtt(bytes,decoded) && decoded==c);
    bytes[10]^=1;assert(!decodeMqtt(bytes,decoded));
    c.enabled=true;assert(c.validate());assert(mqttText("broker.local",c.host));assert(!c.validate());
    assert(!mqttText(std::string(96,'x'),c.host));assert(!mqttText(std::string_view("a\0b",3),c.host));
    assert(mqttText("bad/#",c.discoveryPrefix));assert(c.validate());
    auto notTerminated=encodeMqtt(MqttConfig{});std::fill(notTerminated.begin()+8,notTerminated.begin()+104,'x');assert(!decodeMqtt(notTerminated,decoded));
  }
  Harness base;base.run();
  assert(base.registry.at(0x42)=="homeassistant");
  assert(base.bridge.nodeCount()==1);
  for(const auto &message:base.messages)assert(message.payload.size()<1024);
  assert(base.config("000000000042_target_az/config") && base.config("000000000042_stop/config"));
  for(const char *direction:{"left","right","up","down"}) {
    const auto suffix=std::string("000000000042_")+direction+"/config";
    const auto *toggle=base.config(suffix);
    assert(toggle && toggle->retain && toggle->qos==1);
    assert(toggle->topic.find("homeassistant/switch/")==0);
    assert(toggle->payload.find(std::string("/set/")+direction+"\"")!=std::string::npos);
    assert(toggle->payload.find("\"payload_on\":\"ON\",\"payload_off\":\"OFF\"")!=std::string::npos);
    assert(toggle->payload.find("\"optimistic\":false")!=std::string::npos);
    assert(toggle->payload.find("\"retain\":false")!=std::string::npos);
    const bool elevation=std::string(direction)=="up" || std::string(direction)=="down";
    const int sign=std::string(direction)=="right" || std::string(direction)=="up"?1:-1;
    assert(toggle->payload.find(std::string("value_json.")+(elevation?"el":"az")+".direction == "+std::to_string(sign))!=std::string::npos);
    const auto oldTopic="homeassistant/button/000000000042/"+suffix;
    const auto old=std::find_if(base.messages.begin(),base.messages.end(),[&](const auto &m){return m.topic==oldTopic;});
    assert(old!=base.messages.end() && old->payload.empty() && old->retain && old->qos==1);
    assert(&*old<toggle); // Retire the old domain before registering the replacement.
  }
  const auto *elevation=base.config("000000000042_el/config");
  assert(elevation && elevation->payload.find("\"name\":\"Элевация\"")!=std::string::npos);
  assert(base.config("000000000042_mode/config")->payload.find("auto_1h")!=std::string::npos);
  const auto before=base.messages.size();
  assert(base.bridge.receive("homeassistant/status","online",true,4100));base.run(4100,6500);
  assert(base.messages.size()>before+MqttBridge::EntityCount);
  // Direction switches are immediate operator commands in every control mode.
  for(auto model:{Model::G5500,Model::G800DXA,Model::G800SA})for(auto mode:{Mode::Manual,Mode::Auto,Mode::AutoHour}) {
    Harness h;auto &c=h.runtime.controller;c.setModel(model);c.setMode(mode,4000);h.run();
    for(const char *direction:{"left","right","up","down"}) {
      const bool elevation=std::string(direction)=="up" || std::string(direction)=="down";
      const unsigned axis=elevation?1:0;
      assert(h.command(direction,"ON"));h.bridge.service(4101);
      if(elevation && model!=Model::G5500) {
        assert(std::string(h.bridge.lastResult())=="invalid" && !c.moving());
      } else {
        assert(std::string(h.bridge.lastResult())=="ok");
        assert(c.axes[axis].wanted==((std::string(direction)=="right" || std::string(direction)=="up")?1:-1));
        assert(!c.axes[axis].tracking && !c.pendingValid);
      }
      assert(c.mode==mode);
      assert(h.command(direction,"OFF",4102));h.bridge.service(4103);assert(!c.moving());
    }
  }
  {
    Harness h;h.run();auto &c=h.runtime.controller;
    const auto state=[&](uint32_t at,int az,int el){
      h.bridge.service(at);
      const auto *message=h.config("/state");assert(message);
      const auto azAt=message->payload.find("\"az\":{");
      const auto elAt=message->payload.find("\"el\":{");
      assert(azAt!=std::string::npos && elAt>azAt);
      assert(message->payload.substr(azAt,elAt-azAt).find("\"direction\":"+std::to_string(az))!=std::string::npos);
      assert(message->payload.substr(elAt).find("\"direction\":"+std::to_string(el))!=std::string::npos);
    };
    assert(h.command("left","ON",4100));state(4101,-1,0);
    assert(h.command("left","ON",4110));state(4111,-1,0); // Explicit ON is idempotent.
    assert(h.command("up","ON",4120));state(4121,-1,1);
    assert(h.command("right","ON",4130));state(4131,1,1);
    assert(h.command("right","OFF",4140));state(4141,0,1);
    assert(h.command("right","OFF",4150));state(4151,0,1);
    assert(h.command("down","OFF",4160));state(4161,0,0); // OFF always stops only its axis.
    assert(h.command("left","PRESS",4170));state(4171,-1,0); // Legacy automation payload.
    c.stopNow(0,4180);state(5180,0,0); // A different control source updates switch feedback.
    assert(h.command("right","ON",5190));state(5191,1,0);
    c.feedback(c.maximum,30,true,true);c.tick(5200);state(6200,0,0); // Limit clears intent.
    assert(h.command("left","ON",6210));state(6211,-1,0);
    c.driveFault=true;c.tick(6220);state(7220,0,0); // Protection clears intent.
  }
  {
    Harness h;h.run();auto &c=h.runtime.controller;
    for(unsigned i=0;i<4;++i)assert(h.command("right","ON",4100));
    assert(!h.command("up","ON",4100));
    assert(h.command("right","OFF",4100));h.bridge.service(4101);
    assert(!c.moving() && c.axes[0].wanted==0 && h.bridge.commands()==1);
    assert(h.command("left","ON",4110));assert(h.command("up","ON",4110));
    assert(h.command("left","OFF",4110));h.bridge.service(4111);
    assert(c.axes[0].wanted==0 && c.axes[1].wanted==1);
    assert(h.command("up","OFF",4120));h.bridge.service(4121);assert(!c.moving());
    for(const char *payload:{"ON","OFF"}) {
      assert(!h.command("left",payload,4130,true));
      assert(h.command("left",payload,4130));h.bridge.service(6130);
      assert(std::string(h.bridge.lastResult())=="expired" && !c.moving());
      assert(h.command("right",payload,6140));h.bridge.connection(false);h.bridge.connection(true);h.bridge.service(6141);
      assert(std::string(h.bridge.lastResult())=="expired" && !c.moving());
    }
    for(const char *payload:{"on","off","TOGGLE","RELEASE",""})assert(!h.command("left",payload));
  }
  {
    Harness h;h.run();auto &c=h.runtime.controller;
    assert(!h.command("left","PRESS",4100,true));assert(!h.command("right","RELEASE"));
    assert(h.command("up","PRESS"));h.bridge.service(6100);assert(!c.moving());
    assert(h.command("right","PRESS",6200));h.bridge.connection(false);h.bridge.connection(true);h.bridge.service(6201);assert(!c.moving());
    assert(h.command("left","PRESS",6210));assert(h.command("up","PRESS",6210));
    assert(h.command("stop_az","PRESS",6210));h.bridge.service(6211);
    assert(c.axes[0].wanted==0 && c.axes[1].wanted==1);
    assert(h.command("stop_el","PRESS",6220));h.bridge.service(6221);assert(!c.moving());
    for(unsigned i=0;i<4;++i)assert(h.command("right","PRESS",6230));
    assert(!h.command("left","PRESS",6230));
    assert(h.command("stop","PRESS",6230));h.bridge.service(6231);assert(!c.moving());
    assert(h.command("right","PRESS",6240));h.bridge.service(6241);assert(c.axes[0].wanted==1);
    assert(h.command("stop","PRESS",6250));h.bridge.service(6251);assert(!c.moving());
    c.configurationLocked=true;
    assert(h.command("right","PRESS",6260));h.bridge.service(6261);
    assert(std::string(h.bridge.lastResult())=="unavailable" && !c.moving());
  }
  {
    Harness h;h.run();const auto oldTarget=h.runtime.controller.axes[0].target;
    assert(!h.command("target_az","70",4100,true));
    assert(!h.command("target_az","nan"));assert(!h.command("target_az","70junk"));
    assert(!h.command("mode","other"));assert(!h.command("preset_9","PRESS"));
    assert(h.command("target_az","70"));h.bridge.service(4101);
    assert(h.runtime.controller.axes[0].target==70 && h.runtime.controller.axes[0].target!=oldTarget);
    assert(h.command("mode","auto",4110));h.bridge.service(4111);assert(h.persisted==1);
    assert(h.command("target_az","9999",4120));h.bridge.service(4121);assert(h.runtime.controller.axes[0].target==70);
    assert(h.command("target_az","90",4130));h.bridge.connection(false);h.bridge.connection(true);h.bridge.service(4140);
    assert(h.runtime.controller.axes[0].target==70);
    assert(h.command("target_az","100",4200));h.bridge.service(6200);assert(h.runtime.controller.axes[0].target==70);
  }
  {
    Harness h;h.run();
    for(unsigned i=0;i<4;++i)assert(h.command("target_az","100",4100));
    assert(!h.command("target_az","200",4100));
    assert(h.command("stop","PRESS",4100));h.bridge.service(4101);
    assert(!h.runtime.controller.moving() && h.bridge.commands()==1); // STOP discards preceding movement requests.
    assert(h.command("target_az","200",4102));h.bridge.service(4103);assert(h.runtime.controller.axes[0].target==200);
    assert(h.bridge.dropped()==1);
  }
  {
    Harness h;h.runtime.hud.config.role=DeviceRole::Hud;h.runtime.hud.begin(0x42,0x1234);
    auto *a=h.runtime.hud.add(1,{1,4534,false},"First");auto *b=h.runtime.hud.add(2,{2,4534,false},"Second");assert(a&&b);
    for(auto *p:{a,b}){p->received=p->saved=true;p->receivedAt=4000;p->boot=3;p->wireVersion=5;p->state.capture(h.runtime.controller,h.runtime.protocol,4000);}
    assert(h.runtime.hud.select(1));h.run();assert(h.bridge.nodeCount()==3);
    assert(h.config("selected_target_az/config") && h.config("000000000002_target_az/config"));
    assert(h.config("selected_left/config") && h.config("000000000002_up/config"));
    assert(h.command("right","ON",4100,false,"selected"));assert(h.runtime.hud.select(2));
    const auto selection=h.runtime.hud.selectionEpoch;h.bridge.service(4101);
    assert(std::string(h.bridge.lastResult())=="selection_changed");
    assert(h.command("target_az","120",4110,false,"000000000001"));h.bridge.service(4111);
    assert(h.runtime.hud.config.selected==2 && h.runtime.hud.selectionEpoch==selection);
    assert(h.bridge.pendingRequest()!=0);
    assert(h.command("left","ON",4120,false,"000000000001"));h.bridge.service(4121);
    assert(std::string(h.bridge.lastResult())=="deferred");
    assert(h.runtime.hud.config.selected==2 && h.runtime.hud.selectionEpoch==selection);
    assert(h.command("left","OFF",4130,false,"000000000001"));h.bridge.service(4131);
    assert(std::string(h.bridge.lastResult())=="deferred");
    assert(h.runtime.hud.config.selected==2 && h.runtime.hud.selectionEpoch==selection);
    assert(h.runtime.hud.remove(1));h.run(4200,7000);assert(!h.registry.count(1));
    const auto *deleted=h.config("000000000001_target_az/config");assert(deleted && deleted->payload.empty());
    deleted=h.config("000000000001_left/config");assert(deleted && deleted->payload.empty());
  }
  {
    Harness h;h.registry[0x42]="previous";h.run();
    for(const char *domain:{"button","switch"})for(const char *direction:{"left","right","up","down"}) {
      const auto topic=std::string("previous/")+domain+"/000000000042/000000000042_"+direction+"/config";
      const auto found=std::find_if(h.messages.begin(),h.messages.end(),[&](const auto &m){return m.topic==topic;});
      assert(found!=h.messages.end() && found->payload.empty());
    }
    assert(h.registry.at(0x42)=="homeassistant");
    assert(h.config("000000000042_left/config")->topic.find("homeassistant/switch/")==0);
    h.runtime.mqtt.discovery=false;h.run(4010,7000);assert(h.registry.empty());
    for(const char *direction:{"left","right","up","down"})assert(h.config(std::string("000000000042_")+direction+"/config")->payload.empty());
  }
  {
    Harness h;h.registry[99]="previous";h.registryScan.push_back(99);h.run();
    assert(!h.registry.count(99));
    bool cleared=false;for(const auto &m:h.messages)if(m.topic.find("previous/")==0 && m.payload.empty())cleared=true;assert(cleared);
  }
  {
    Harness h;h.runtime.controller.setModel(Model::G800DXA);h.run();
    const auto *el=h.config("000000000042_el/config");assert(el && el->payload.empty());
    for(const char *direction:{"up","down"}){const auto *toggle=h.config(std::string("000000000042_")+direction+"/config");assert(toggle && toggle->payload.empty());}
    assert(!h.config("000000000042_left/config")->payload.empty());
    h.network=false;const auto count=h.messages.size();h.run(5000,7000);assert(h.messages.size()==count);
    h.network=true;h.run(7010,9000);assert(h.messages.size()>count);
    h.runtime.controller.setModel(Model::G5500);h.run(9010,11000);
    assert(!h.config("000000000042_up/config")->payload.empty());
    h.runtime.controller.setModel(Model::G800SA);h.run(11010,13000);
    assert(h.config("000000000042_up/config")->payload.empty() && h.config("000000000042_down/config")->payload.empty());
  }
  {
    Harness h;h.runtime.controller.hardware.simulation=true;
    h.runtime.controller.motionSettings[0].mode=RegulatorMode::Cascade;
    h.runtime.controller.motionSettings[0].velocity=.05f;
    h.runtime.protocol.rotorName=std::string(31,'\x01');
    for(auto &p:h.runtime.controller.presets)p.label=std::string(32,'"');
    h.run();const auto *speed=h.config("000000000042_speed_az/config");assert(speed && speed->payload.find("\"min\":0.0025")!=std::string::npos);
    for(const auto &m:h.messages)assert(m.payload.size()<1024);
    assert(h.command("speed_az","0.01",4100));h.bridge.service(4101);assert(std::abs(h.runtime.controller.runtimeSpeed(0)-.01f)<1e-6f);
    h.runtime.controller.restoreMode(Mode::Manual);
    assert(h.command("target_az","100",4110));h.bridge.service(4111);assert(h.runtime.controller.pendingValid);
    const auto *state=h.config("/state");assert(state && state->payload.find("\"pending\":100.00")!=std::string::npos);
    assert(h.command("execute","PRESS",4120));h.bridge.service(4121);assert(!h.runtime.controller.pendingValid);
    assert(h.command("stop","PRESS",4130));h.bridge.service(4131);assert(!h.runtime.controller.moving());
    assert(h.command("mode","auto_1h",4140));h.bridge.service(4141);assert(h.runtime.controller.mode==Mode::AutoHour);
    for(unsigned slot=0;slot<4;++slot){h.runtime.controller.presets[slot].target={float(60+slot),40,true,true};assert(h.command("preset_"+std::to_string(slot+1),"PRESS",4150+slot*10));h.bridge.service(4151+slot*10);assert(h.runtime.controller.axes[0].target==float(60+slot));}
  }
  if(argc>1 && std::string(argv[1])=="--dump-hud"){
    Harness h;h.runtime.hud.config.role=DeviceRole::Hud;h.runtime.hud.begin(0x42,0x1234);
    for(uint64_t id:{1,2}){auto *p=h.runtime.hud.add(id,{uint32_t(id),4534,false},id==1?"First":"Second");assert(p);p->received=p->saved=true;p->receivedAt=4000;p->boot=3;p->wireVersion=5;p->state.capture(h.runtime.controller,h.runtime.protocol,4000);}
    assert(h.runtime.hud.select(1));h.run(0,6000);
    for(const auto &m:h.messages)std::cout<<m.topic<<'\t'<<m.payload<<'\n';
    return 0;
  }
  if(argc>1 && std::string(argv[1])=="--dump"){
    for(const auto &m:base.messages){std::cout<<m.topic<<'\t'<<m.payload<<'\n';}return 0;
  }
  std::cout<<"PASS: MQTT direction switches/feedback/migration, config, bounded commands/STOP, discovery, birth/LWT state, catalogue/selected routing, retained rejection, reconnect expiry and cleanup\n";
}
