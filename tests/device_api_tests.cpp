#include "device_service_fixture.hpp"
static void useLocalController(Fixture &f) {
  auto &r=f.runtime;r.hud.config.role=DeviceRole::Controller;r.controller.remote=nullptr;
  r.controller.driveFault=false;r.controller.fault.clear();r.controller.feedback(90,45,true,true);
}
static void settingsOutputSafetyTests() {
  for(bool wire:{false,true})for(const char *fields:{"azCoastDegrees=3","azMotionVelocityKp=1.23","model=G800SA","speedEnabled=1","controlHz=100","rotorName=Running"}) {
    Fixture f;auto &c=f.runtime.controller;auto config=c.hardware;
    config.simulation=false;config.drive=DRIVE_YAESU;config.pins=PinMap{};
    if(!config.hasDirections(0)||!config.hasDirections(1))continue;
    c.configureHardware(config);c.speedEnabled=false;c.feedback(90,45,true,true);
    f.hardware.beginDirections();assert(!c.driveFault);
    assert(f.call("POST","/api/move","d=R",wire).status==200);
    assert(f.call("POST","/api/move","d=U",wire).status==200);
    const int az=c.hardware.pins.azPositive,el=c.hardware.pins.elPositive;
    assert(fakeHardware.levels[az]==HIGH&&fakeHardware.levels[el]==HIGH);
    const auto epoch=c.stopEpoch;
    const bool nameOnly=std::string(fields).find("rotorName=")==0;
    const bool axisOnly=std::string(fields).find("az")==0;
    unsigned writes=0;
    Preferences::beforeWrite=[&](const char *) {
      ++writes;
      assert(fakeHardware.levels[az]==(nameOnly?HIGH:LOW));
      assert(fakeHardware.levels[el]==(nameOnly||axisOnly?HIGH:LOW));
    };
    const auto response=f.call("POST","/api/settings",fields,wire);
    Preferences::beforeWrite={};assert(response.status==200&&writes);
    assert(nameOnly?c.stopEpoch==epoch:c.stopEpoch!=epoch);
  }
  for(bool wire:{false,true}) {
    Fixture f;
    assert(f.call("POST","/api/settings","rotorName=Before",wire).status==200);
    Preferences::failedStringKey=f.runtime.hud.isHud()?"hudname":"name";
    const auto response=f.call("POST","/api/settings","rotorName=After",wire);
    Preferences::failedStringKey.clear();
    assert(response.status==503&&response.body.find("motion_storage_failed")!=std::string::npos);
    Runtime reboot;reboot.hud.config.role=f.runtime.hud.config.role;
    f.storage.loadForkSettings(reboot);assert(reboot.protocol.rotorName=="Before");
  }
}
static void liveTrialConfirmationTests() {
  for(bool wire:{false,true}) {
    Fixture f;useLocalController(f);auto &r=f.runtime;auto candidate=r.controller.hardware;
    candidate.buttonLow=!candidate.buttonLow;
    assert(f.storage.stageHardware(r,candidate));f.storage.loadHardware(r);f.ready();
    assert(r.hardwareTrial&&!r.controller.configurationLocked&&!r.recovery);
    if(!wire) {
      r.setup.required=true;r.setup.paused=false;r.setup.step=SetupStep::Equipment;r.setup.pendingNext=true;
      r.setupLease=7;r.setupSaving=true;
    }
    const std::string stamp="token=11&generation="+std::to_string(r.hardwareRecord.generation);
    assert(f.call("GET","/api/status",{},wire).body.find("\"hardwareTrial\":{\"remainingMs\":289999,\"ready\":true}")!=std::string::npos);
    assert(f.call("POST","/api/move","d=R",wire).status==200&&r.controller.moving());
    const auto stop=r.controller.stopEpoch;
    assert(f.call("POST","/api/hardware/confirm",stamp,wire).status==409);
    assert(r.hardwareTrial&&r.controller.moving()&&r.controller.stopEpoch==stop&&!f.restart.pending());
    assert(f.call("POST","/api/hardware/probe",stamp,wire).status==200);
    Preferences::failedBytesKey="config1";
    assert(f.call("POST","/api/hardware/confirm",stamp,wire).status==503);
    assert(r.hardwareTrial&&r.controller.moving()&&r.controller.stopEpoch==stop&&!f.restart.pending());
    Preferences::failedBytesKey.clear();
    assert(f.call("POST","/api/hardware/confirm",stamp+(!wire?"&setupToken=7":""),wire).status==200);
    assert(!r.hardwareTrial&&!r.controller.configurationLocked&&r.controller.moving());
    assert(r.controller.stopEpoch==stop&&!f.restart.pending()&&!r.setupRestartRequired);
    if(!wire) {
      assert(!r.setup.pendingNext&&r.setup.step==SetupStep::Equipment);
      assert(f.call("POST","/api/setup","action=end_save&setupToken=7",false).status==200);
      assert(f.call("POST","/api/setup","action=advance&setupToken=7&generation="+std::to_string(r.setup.generation),false).status==200);
      assert(r.setup.step==SetupStep::Inputs&&r.controller.moving()&&r.controller.stopEpoch==stop);
    }
    assert(f.call("GET","/api/status",{},wire).body.find("\"hardwareTrial\":null")!=std::string::npos);
    Runtime reboot;f.storage.loadHardware(reboot);
    assert(!reboot.hardwareTrial&&!reboot.recovery&&reboot.controller.hardware==candidate);
    assert(r.memoryGuard.sample(MemoryGuard::TrialMs,60000,30000,0,r.hardwareTrial)==RecoveryReason::None);
  }
}
static void rs485DiscoveryApiTests() {
  {
    Fixture scan;scan.runtime.hud.config.role=DeviceRole::Hud;scan.runtime.hud.config.uart=true;scan.runtime.hud.begin(1,0x1234);scan.runtime.hud.begin(1,0x1234);
    assert(scan.call("POST","/api/hud/discover","transport=rs485").status==400);
    scan.runtime.modbus.bus=1;
    assert(scan.call("POST","/api/hud/discover","transport=rs485&first=3&last=7").status==200);
    assert(scan.call("GET","/api/hud/peers","discovered=1").body.find("\"scanActive\":true")!=std::string::npos);
    for(auto bad:{"first=0","first=248","first=8&last=7","last=-1","last=999","first=no"})
      assert(scan.call("POST","/api/hud/discover",std::string("transport=rs485&")+bad).status==400);
    assert(scan.runtime.hud.add(100,{3,4534,true},"RS485 three"));
    assert(scan.runtime.hud.add(101,{7,4534,true},"RS485 seven"));
    assert(scan.storage.saveHudPeers(scan.runtime.hud));
    assert(scan.runtime.hud.select(101)&&scan.storage.saveHudConfig(scan.runtime.hud.config));
    Runtime restored;scan.storage.loadHud(restored);
    assert(restored.hud.peer(100)->endpoint.address==3&&restored.hud.peer(101)->endpoint.address==7);
    assert(restored.hud.config.selected==101);
  }
}
static void hudBackgroundTests() {
  Fixture f;auto &h=f.runtime.hud;h.config.role=DeviceRole::Hud;h.begin(1,0x1234);
  auto *blue=h.add(2,{0x0100007f,4534,false},"Blue mast");
  auto *green=h.add(3,{7,4534,true},"Green mast");
  assert(blue && green && f.storage.saveHudConfig(h.config));
  const auto update=[&](const std::string &fields) {return f.call("POST","/api/hud/peers",fields);};
  assert(update("id=2&background=1").status==200);
  assert(!h.config.selected && blue->background==1 && std::string(blue->label)=="Blue mast");
  assert(h.select(2));const auto epoch=h.selectionEpoch;
  assert(update("id=3&background=2").status==200);
  assert(h.config.selected==2 && h.selectionEpoch==epoch && green->background==2);
  for(const char *bad:{"", "-1", "9", "255", "256", "1.0", "green", "2x"}) {
    assert(update(std::string("id=2&name=Changed&background=")+bad).status==400);
    assert(blue->background==1 && std::string(blue->label)=="Blue mast");
  }
  Preferences::failedBytesKey="peers";
  assert(update("id=2&name=Changed&background=3").status==507);
  assert(blue->background==1 && blue->saved && std::string(blue->label)=="Blue mast");
  Preferences::failedBytesKey.clear();
  assert(update("id=2&name=Renamed").status==200 && blue->background==1);
  assert(update("id=2&background=0").status==200 && blue->background==0);
  for(unsigned i=0;i<hudBackgroundCount;++i) {
    assert(update("id=2&background="+std::to_string(i)).status==200);
    Runtime reboot;f.storage.loadHud(reboot);
    assert(!reboot.hud.catalogError && reboot.hud.count()==2);
    assert(reboot.hud.peer(2)->background==i && reboot.hud.peer(3)->background==2);
    assert(std::string(reboot.hud.peer(2)->label)=="Renamed");
    assert(reboot.hud.peer(3)->endpoint.uart && reboot.hud.peer(3)->endpoint.address==7);
    assert(f.call("GET","/api/hud/peers","limit=8").body.find("\"background\":"+std::to_string(i))!=std::string::npos);
  }
  assert(h.select(3) && h.active()->background==2);
  assert(h.select(2) && h.active()->background==hudBackgroundCount-1);
  assert(update("id=2&action=remove").status==200);
  assert(!h.peer(2) && h.peer(3)->background==2);
}
static void mqttApiTests() {
#if MQTT_SUPPORT
  {
    Fixture f;
    const auto summary=[&](){const auto r=f.call("GET","/api/status",{},false);assert(r.status==200);return r.body;};
    assert(summary().find("\"mqtt\":{\"enabled\":false,\"storageOk\":true,\"supported\":true,\"connected\":false,\"error\":\"disabled\"}")!=std::string::npos);
    f.runtime.mqtt.enabled=true;std::strcpy(f.runtime.mqtt.host,"broker.local");
    std::strcpy(f.runtime.mqtt.username,"private-user");std::strcpy(f.runtime.mqtt.password,"private-password");
    const auto active=summary();
    assert(active.find("\"mqtt\":{\"enabled\":true,\"storageOk\":true,\"supported\":true,\"connected\":false,\"error\":\"not_started\"}")!=std::string::npos);
    for(const char *privateValue:{"private-user","private-password","broker.local","baseTopic","outboxBytes"})assert(active.find(privateValue)==std::string::npos);
    f.runtime.mqttStorageOk=false;
    assert(summary().find("\"error\":\"mqtt_storage_failed\"")!=std::string::npos);
    f.runtime.mqttStorageOk=true;
    MqttService mqtt{f.runtime,f.actions,f.hardware};
    DeviceApi api{f.runtime,f.actions,f.calibration,f.hardware,f.diagnostics,f.network,f.storage,f.ota,f.restart,&mqtt};
    char data[8000];JsonWriter out(data,sizeof data);int status=200;const char *type=nullptr;
    api.execute("GET","/api/status",{},out,status,type,false);
    assert(status==200&&out.good());
    const std::string connectedService(data,out.size());
    assert(connectedService.find("\"mqtt\":{\"enabled\":true,\"storageOk\":true,\"supported\":true,\"connected\":false,\"error\":\"not_started\"}")!=std::string::npos);
  }
  {
    Fixture f;useLocalController(f);auto &r=f.runtime;
    WiFi.connectionStatus=WL_CONNECTED;r.setup.required=true;r.setup.equipment=true;r.setup.step=SetupStep::Review;r.controller.setupRequired=true;
    assert(f.call("POST","/api/setup","action=resume",false).status==200);
    const auto token="&setupToken="+std::to_string(r.setupLease);
    assert(f.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    Preferences::failedBytesKey="config1";
    assert(f.call("POST","/api/mqtt","enabled=1&host=broker.local"+token,false).status==503);
    assert(r.setup.required&&!r.setupRestartRequired&&!f.restart.pending());Preferences::failedBytesKey.clear();
    assert(f.call("POST","/api/mqtt","enabled=1&host=broker.local&password=wizard-secret"+token,false).status==200);
    assert(r.setupRestartRequired&&!f.restart.pending());
    assert(f.call("POST","/api/setup","action=end_save"+token,false).status==200);
    WiFi.connectionStatus=0;
    assert(f.call("POST","/api/setup","action=complete&generation="+std::to_string(r.setup.generation)+token,false).status==409);
    assert(r.setup.required&&!f.restart.pending());
    const auto saved=f.call("GET","/api/mqtt",{},false);
    assert(saved.status==200&&saved.body.find("\"host\":\"broker.local\"")!=std::string::npos&&saved.body.find("\"hasPassword\":true")!=std::string::npos);
    assert(!r.mqtt.enabled&&r.mqtt.password[0]==0); // The active client keeps its original credentials until restart.
    const auto active=f.call("GET","/api/status",{},false);
    assert(active.body.find("\"mqtt\":{\"enabled\":false")!=std::string::npos&&active.body.find("wizard-secret")==std::string::npos);
    assert(f.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    assert(f.call("POST","/api/mqtt","host=corrected.local"+token,false).status==200);
    assert(f.call("POST","/api/setup","action=end_save"+token,false).status==200);
    WiFi.connectionStatus=WL_CONNECTED;
    assert(f.call("POST","/api/setup","action=complete&generation="+std::to_string(r.setup.generation)+token,false).status==200);
    assert(!r.setup.required&&!r.controller.setupRequired&&r.setup.step==SetupStep::Done&&f.restart.pending());
    Runtime reboot;MqttConfig mqtt;assert(f.storage.loadSetup(reboot)&&!reboot.setup.required);
    assert(f.storage.loadMqtt(mqtt)&&mqtt.enabled&&std::string(mqtt.host)=="corrected.local"&&std::string(mqtt.password)=="wizard-secret");
  }
  {
    Fixture f;f.runtime.hud.config.role=DeviceRole::Controller;f.runtime.hud.begin(1,0x1234);
    auto hardware=f.runtime.controller.hardware;hardware.simulation=true;
    f.runtime.controller=Controller{};f.runtime.controller.configureHardware(hardware);
    f.runtime.controller.feedback(90,45,true,true);
    MqttConfig config;assert(f.storage.loadMqtt(config)&&!config.enabled);
    assert(f.call("GET","/api/mqtt").body.find("\"hasPassword\":false")!=std::string::npos);
    assert(f.call("POST","/api/mqtt","enabled=1").status==400);
    for(const char *bad:{"enabled=yes","port=0","port=65536","unknown=1","host=bad%2Fhost","discoveryPrefix=a%2F%2B"})assert(f.call("POST","/api/mqtt",bad).status==400);
    bool stopped=false;
    assert(f.runtime.controller.jog(0,1,false,millis())==Result::Ok);
    Preferences::beforeBytesWrite=[&](const char *key){if(std::string(key)=="config1"){stopped=!f.runtime.controller.moving();}};
    const auto result=f.call("POST","/api/mqtt","enabled=1&host=broker.local&username=radio&password=secret%2B%26");
    Preferences::beforeBytesWrite={};assert(result.status==200&&stopped);
    assert(!f.runtime.mqtt.enabled); // Client configuration is immutable until reboot.
    assert(f.storage.loadMqtt(config)&&config.enabled&&std::string(config.password)=="secret+&");
    f.runtime.mqtt=config;
    auto get=f.call("GET","/api/mqtt");assert(get.body.find("secret")==std::string::npos && get.body.find("\"hasPassword\":true")!=std::string::npos);
    assert(f.call("POST","/api/mqtt","username=new").status==200);
    assert(f.storage.loadMqtt(config)&&std::string(config.password)=="secret+&");f.runtime.mqtt=config;
    assert(f.call("POST","/api/mqtt","password=").status==200);assert(f.storage.loadMqtt(config)&&!config.password[0]);
  }
  {
    Fixture f;Preferences::failedBytesKey="config1";
    assert(f.call("POST","/api/mqtt","host=broker").status==503);
    assert(!f.runtime.mqtt.enabled);Preferences::failedBytesKey.clear();
    MqttConfig config;assert(f.storage.saveMqtt(config));
    auto &record=std::get<Preferences::Bytes>(Preferences::database["mqtt"]["config1"]);record[2]^=1;
    assert(!f.storage.loadMqtt(config));
  }
#endif
}
int main(int argc,char **argv) {
  settingsOutputSafetyTests();
  liveTrialConfirmationTests();
  hudBackgroundTests();
  mqttApiTests();
  {
    Fixture hud;auto &r=hud.runtime;r.hud.config.role=DeviceRole::Hud;r.controller.remote=&r.hud;
    r.setup.required=true;r.setup.step=SetupStep::Role;r.setup.equipment=true;r.controller.setupRequired=true;
    assert(hud.call("POST","/api/setup","action=resume",false).status==200);
    const auto token="&setupToken="+std::to_string(r.setupLease);
    assert(hud.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    assert(hud.call("POST","/api/settings","rotorName=RemoteHud"+token,false).status==200);
    assert(r.protocol.rotorName=="RemoteHud");
    Runtime loaded;loaded.hud.config.role=DeviceRole::Hud;hud.storage.loadForkSettings(loaded);
    assert(loaded.protocol.rotorName=="RemoteHud");
    const auto rejected=hud.call("POST","/api/settings","rotorName=Wrong&model=G800SA"+token,false);
    assert(rejected.status==400&&rejected.body.find("controller_settings_only")!=std::string::npos);
    assert(r.protocol.rotorName=="RemoteHud");
    assert(hud.call("POST","/api/setup","action=end_save"+token,false).status==200);
    assert(hud.call("POST","/api/setup","action=advance&generation="+std::to_string(r.setup.generation)+token,false).status==200);
    assert(r.setup.step==SetupStep::Equipment&&!r.controller.moving());
  }
  rs485DiscoveryApiTests();
  if(argc==2 && std::string(argv[1])=="--rs485-check") {std::cout<<"PASS: RS485 discovery API, ranges, saved addresses and selection reload\n";return 0;}
  if(argc==2 && std::string(argv[1])=="--serve-http") {
    Fixture f;f.runtime.setup.required=true;f.runtime.setup.equipment=true;f.runtime.setup.step=SetupStep::Role;
    f.runtime.controller.setupRequired=true;WiFi.connectionStatus=WL_CONNECTED;
    std::string line;
    while(std::getline(std::cin,line)) {
      const auto a=line.find('\t'),b=line.find('\t',a+1),c=line.find('\t',b+1);
      if(a==line.npos||b==line.npos||c==line.npos)return 2;
      fakeHardware.now=uint32_t(std::stoul(line.substr(0,a)));
      const auto method=line.substr(a+1,b-a-1),path=line.substr(b+1,c-b-1),body=line.substr(c+1);
      Response reply;
      if(path=="/__fixture/hud") {
        f.runtime.hud.config.role=DeviceRole::Hud;f.runtime.controller.remote=&f.runtime.hud;
        reply={200,"{}"};
      } else if(path=="/__fixture/controller") {
        f.runtime.hud.config.role=DeviceRole::Controller;f.runtime.controller.remote=nullptr;
        f.runtime.controller.driveFault=false;f.runtime.controller.fault.clear();
        f.runtime.controller.configureHardware(f.runtime.hardwareRecord.good);
        reply={200,"{}"};
      } else if(path=="/__fixture/reboot") {
        auto &r=f.runtime;
        assert(f.restart.pending());f.restart=RestartDelay{};
        r.setupLease=0;r.setupTestToken=0;r.setupSaving=false;r.setupRestartRequired=false;r.hardwareProbe=false;++r.hardwareToken;
        assert(f.storage.loadSetup(r));f.storage.loadHardware(r);
        r.memoryGuard.begin(fakeHardware.now-10001,0);r.memoryGuard.sample(fakeHardware.now-10001,60000,30000,0,r.hardwareTrial);
        reply={200,"{}"};
      } else if(path=="/__fixture/tuning") {
        auto &c=f.runtime.controller;auto config=c.hardware;config.simulation=false;config.pins=PinMap{};config.drive=DRIVE_BTS7960;
        c.configureHardware(config);c.feedback(90,45,true,true);
        for(unsigned i=0;i<2;++i){auto m=c.motionSettings[i];m.mode=RegulatorMode::Cascade;m.ready=true;assert(c.configureMotion(i,m,fakeHardware.now));}
        f.runtime.setup.step=SetupStep::Tuning;reply={200,"{}"};
      } else if(path=="/__fixture/step") {
        f.runtime.setup.step=SetupStep(std::stoul(body));f.runtime.setup.pendingNext=false;
        reply={200,"{}"};
      } else reply=f.call(method.c_str(),path.c_str(),body,false);
      std::cout << "{\"status\":" << reply.status << ",\"body\":" << reply.body
        << ",\"moving\":" << (f.runtime.controller.moving()?"true":"false")
        << ",\"testToken\":" << f.runtime.setupTestToken << "}\n" << std::flush;
    }
    return 0;
  }
  {
    Fixture f;f.runtime.setup.required=false;
    auto &n=f.network.credentials().network;n.choice=NetworkChoice::Wifi;
    f.runtime.hud.config.wifi=HudWifi::Existing;
    f.runtime.controller.hardware.forceAp=false;assert(!f.network.apAlwaysOn());
    f.runtime.controller.hardware.forceAp=true;assert(f.network.apAlwaysOn());
    f.runtime.controller.hardware.forceAp=false;f.runtime.setup.required=true;
    for(unsigned step=0;step<unsigned(SetupStep::Done);++step)for(bool paused:{false,true}) {
      f.runtime.setup.step=SetupStep(step);f.runtime.setup.paused=paused;
      assert(!f.network.apAlwaysOn());
      WifiFallback policy;policy.reset(0,true);
      assert(policy.update(1,true,true,true,f.network.apAlwaysOn())==WifiAction::StopAp);
    }
    assert(f.call("GET","/api/network","",false).body.find("\"apAlwaysOn\":false")!=std::string::npos);
    f.runtime.setup.required=false;f.runtime.recovery=true;assert(f.network.apAlwaysOn());
    f.runtime.recovery=false;n.choice=NetworkChoice::Ap;assert(f.network.apAlwaysOn());
    n.choice=NetworkChoice::SharedHost;assert(f.network.apAlwaysOn());
    n.choice=NetworkChoice::SharedClient;assert(!f.network.apAlwaysOn());
    n.choice=NetworkChoice::Ethernet;assert(!f.network.apAlwaysOn());
    n.choice=NetworkChoice::Wifi;f.runtime.hud.config.wifi=HudWifi::Host;assert(f.network.apAlwaysOn());
  }

  {
    Preferences::database.clear();Runtime fresh;Storage storage;
    assert(storage.loadSetup(fresh));assert(fresh.setup.required && fresh.setup.step==SetupStep::Network);
    storage.loadHardware(fresh);Hardware newMotor(fresh);fakeHardware={};newMotor.beginDirections();newMotor.beginAdc();newMotor.applyOutputs();
    assert(!fresh.controller.moving()&&!fresh.controller.movementBlocked());
    fresh.setup.step=SetupStep::Equipment;fresh.setup.paused=true;assert(storage.saveSetup(fresh.setup));
    Runtime restored;assert(storage.loadSetup(restored));assert(restored.setup.required&&restored.setup.paused&&restored.setup.step==SetupStep::Equipment);
    for(int drive=-1;drive<=3;++drive)for(bool simulation:{false,true}){
      if(drive<0&&simulation)continue;
      SetupState source;source.drive=drive;source.simulation=simulation;source.fresh=true;source.pendingNext=true;SetupState decoded;
      assert(decodeSetup(encodeSetup(source),decoded)&&decoded.drive==drive&&decoded.simulation==simulation&&decoded.pendingNext);
    }
    SetupState old;old.drive=2;old.simulation=true;old.fresh=true;old.pendingNext=true;
    auto legacy=encodeSetup(old);legacy[0]=1;legacy[7]=31;uint32_t crc=2166136261u;
    for(unsigned i=0;i<12;++i)crc=(crc^legacy[i])*16777619u;
    for(unsigned i=0;i<4;++i)legacy[12+i]=crc>>(8*i);
    SetupState decoded;assert(decodeSetup(legacy,decoded)&&decoded.drive==2&&decoded.simulation&&decoded.fresh&&decoded.pendingNext);
    auto bytes=encodeSetup(fresh.setup);for(unsigned i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;SetupState state;assert(!decodeSetup(bad,state));}
    Preferences::database["setup"]["state1"]=Preferences::Bytes(16,0);Preferences::failedBytesKey="state1";assert(!storage.loadSetup(restored));
    Preferences::failedBytesKey.clear();assert(storage.loadSetup(restored)&&restored.setup.required&&!restored.setup.equipment&&restored.setup.step==SetupStep::Network);
    Preferences::database.clear();Preferences::database["g5500"]["appass"]=String("old-password");
    assert(storage.loadSetup(restored)&&!restored.setup.required);
  }
  {
    Fixture setup;assert(setup.storage.loadSetup(setup.runtime)&&!setup.runtime.setup.required);
    auto physical=setup.runtime.hardwareRecord.good;physical.simulation=false;setup.runtime.controller.configureHardware(physical);
    assert(setup.call("POST","/api/setup","action=start",true).status==405);
    auto result=setup.call("POST","/api/setup","action=start",false);assert(result.status==200);
    auto &r=setup.runtime;auto &c=r.controller;assert(c.setupRequired && r.setup.required && r.setupLease);
    HudState active;active.capture(c,r.protocol,fakeHardware.now);assert(!active.driveFault);
    assert(c.jog(0,1,false,fakeHardware.now)==Result::Ok);c.stopNow(-1,fakeHardware.now);
    assert(c.target({100,0,true,false},false,fakeHardware.now)==Result::Ok);c.stopNow(-1,fakeHardware.now);
    assert(setup.call("POST","/api/setup","action=advance&generation=0&checked=1",false).status==409);
    assert(setup.call("POST","/api/setup","action=advance&generation="+std::to_string(r.setup.generation)+"&checked=1",false).status==409);
    WiFi.connectionStatus=WL_CONNECTED;
    assert(setup.call("POST","/api/setup","action=advance&generation="+std::to_string(r.setup.generation),false).status==200);
    assert(r.setup.step==SetupStep::Role);
    r.setup.step=SetupStep::Review;
    const auto session=r.setupLease;
    assert(setup.call("POST","/api/setup","action=arm&setupToken="+std::to_string(session),false).status==200);
    const auto permit=r.setupTestToken;
    const auto token=std::to_string(session)+"&setupTestToken="+std::to_string(permit);
    assert(setup.call("POST","/api/move","d=R",false).status==200);
    assert(setup.call("POST","/api/move","d=R&setupToken="+token,true).status==503);
    assert(setup.call("POST","/api/move","d=R&setupToken="+token,false).status==200);
    assert(c.moving());
    c.stopNow(-1,fakeHardware.now);r.setupStorageOk=false;
    assert(setup.call("POST","/api/move","d=R&setupToken="+token,false).status==200);
    r.setupStorageOk=true;r.recovery=true;
    assert(setup.call("POST","/api/move","d=S",false).status==200);
    assert(setup.call("POST","/api/move","d=R&setupToken="+token,false).status==409);
    r.recovery=false;
    struct Sink:CommandSink {unsigned modes=0;Result submit(const ControlCommand &command) override {if(command.op==ControlOp::Mode)++modes;return Result::Ok;}bool available()const override{return true;}uint32_t requestId()const override{return 1;}Result lastResult()const override{return Result::Ok;}} sink;
    c.stopNow(-1,fakeHardware.now);c.remote=&sink;r.setup.step=SetupStep::Programs;
    assert(setup.call("POST","/api/mode","mode=manual",false).status==200);
    assert(setup.call("POST","/api/mode","mode=manual&setupToken="+token,false).status==200&&sink.modes==2);
    assert(setup.call("POST","/api/mode","mode=auto&setupToken="+token,false).status==200);
    c.remote=nullptr;r.setup.step=SetupStep::Review;
    fakeHardware.now+=1501;setup.api.serviceSetup(fakeHardware.now);assert(!c.moving()&&r.setupLease==session&&!r.setupTestToken);
    assert(setup.call("POST","/api/setup","action=renew&setupToken="+token,false).status==409);
    assert(setup.call("POST","/api/move","d=S",false).status==200);
    assert(setup.call("POST","/api/setup","action=begin_save&setupToken="+std::to_string(session),false).status==200);
    fakeHardware.now+=60000;setup.api.serviceSetup(fakeHardware.now);
    assert(r.setupLease==session && !r.setupTestToken && r.setupSaving);
    assert(setup.call("POST","/api/settings","rotorName=AfterDelay&setupToken="+std::to_string(session),false).status==200);
    assert(setup.call("POST","/api/setup","action=end_save&setupToken="+std::to_string(session),false).status==200);
    assert(setup.call("POST","/api/setup","action=arm&setupToken="+std::to_string(session),false).status==200);
    assert(r.setupTestToken && r.setupTestToken!=permit);
    assert(setup.call("POST","/api/move","d=R&setupToken="+token,false).status==503 && !c.moving());
    const auto current=std::to_string(session)+"&setupTestToken="+std::to_string(r.setupTestToken);
    assert(setup.call("POST","/api/move","d=R&setupToken="+current,false).status==200);
    fakeHardware.now+=Runtime::SetupTestTimeoutMs+1;setup.api.serviceSetup(fakeHardware.now);
    assert(!c.moving() && !r.setupTestToken && r.setupLease==session);
    assert(setup.call("POST","/api/setup","action=renew&setupToken="+current,false).status==409);
    assert(setup.call("POST","/api/move","d=R&setupToken="+current,false).status==503 && !c.moving());
    assert(setup.call("POST","/api/setup","action=pause",false).status==200&&r.setup.paused&&c.setupRequired&&!r.setupLease&&!r.setupTestToken);
    Preferences::failedBytesKey="state1";assert(setup.call("POST","/api/setup","action=resume",false).status==503);
    Preferences::failedBytesKey.clear();
  }
  {
    Fixture hardwareForm;
    const auto full=hardwareForm.call("GET","/api/hardware","",false);
    const auto form=hardwareForm.call("GET","/api/hardware","view=form",false);
    assert(full.status==200 && form.status==200 && form.body.size()<full.body.size());
    for(const char *key:{"token","generation","role","configured","remainingMs","failureCount","heapFree","backend","sppSupported","separateDrive"})
      assert(form.body.find(std::string("\"")+key+"\":")!=std::string::npos);
    for(const char *key:{"effective","confirmed"}) {
      assert(full.body.find(std::string("\"")+key+"\":")!=std::string::npos);
      assert(form.body.find(std::string("\"")+key+"\":")==std::string::npos);
    }
  }
  {
    Fixture tuningStep;auto &r=tuningStep.runtime;auto &c=r.controller;
    auto config=c.hardware;config.simulation=false;config.pins=PinMap{};config.drive=DRIVE_BTS7960;c.configureHardware(config);c.feedback(90,45,true,true);
    for(unsigned i=0;i<2;++i){auto m=c.motionSettings[i];m.mode=RegulatorMode::Cascade;m.ready=true;assert(c.configureMotion(i,m,fakeHardware.now));}
    assert(tuningStep.call("POST","/api/setup","action=start",false).status==200);r.setup.step=SetupStep::Tuning;
    const auto token="&setupToken="+std::to_string(r.setupLease);
    assert(tuningStep.call("GET","/api/setup","",false).body.find("\"ready\":true")!=std::string::npos);
    assert(tuningStep.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    assert(!r.setupTestToken);
    assert(tuningStep.call("POST","/api/settings","controlHz=100"+token,false).status==200);
    assert(c.controlPeriodMs()==10 && !c.moving() && !r.setupTestToken);
    assert(tuningStep.call("POST","/api/settings","azMotionVelocityKp=1.23&elMotionVelocityKp=1.34"+token,false).status==200);
    assert(tuningStep.call("POST","/api/setup","action=end_save"+token,false).status==200);
    assert(tuningStep.call("POST","/api/setup","action=advance&generation="+std::to_string(r.setup.generation)+token,false).status==200);
    assert(r.setup.step==SetupStep::Programs && c.motionSettings[0].velocityKp==1.23f && c.motionSettings[1].velocityKp==1.34f);
    Runtime restored;restored.controller.hardware=c.hardware;tuningStep.storage.loadForkSettings(restored);
    assert(restored.controller.controlPeriodMs()==10 && restored.controller.motionSettings[0].velocityKp==1.23f && restored.controller.motionSettings[1].velocityKp==1.34f);
    assert(tuningStep.call("POST","/api/move","d=R"+token,false).status==200);
    assert(tuningStep.call("POST","/api/target","az=100"+token,false).status==200);c.stopNow(-1,fakeHardware.now);
    r.setup.step=SetupStep::Tuning;
    c.resetMotion(-1,fakeHardware.now);c.feedbackAt(0,90,true,fakeHardware.now);c.feedbackAt(1,45,true,fakeHardware.now);
    const auto tune=tuningStep.call("POST","/api/motion/autotune","action=start&axis=az&mode=cascade&id=0&travel=60&outputLimit=50"+token,false);
    if(tune.status!=200)std::cerr<<"Setup autotune: "<<tune.body<<'\n';
    assert(tune.status==200);c.cancelAutoTune(fakeHardware.now);
    c.configurationLocked=true;
    assert(tuningStep.call("POST","/api/settings","controlHz=200"+token,false).status==503 && c.controlPeriodMs()==10);
    assert(!c.moving() && !r.setupTestToken);
  }
  {
    Fixture draft;auto &r=draft.runtime;
    assert(draft.call("POST","/api/setup","action=start",false).status==200);
    r.setup.step=SetupStep::Role;const auto token="&setupToken="+std::to_string(r.setupLease);
    assert(draft.call("POST","/api/setup","action=begin_save",false).status==409);
    assert(draft.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    const auto generation=r.setup.generation;
    fakeHardware.now+=9000;draft.api.serviceSetup(fakeHardware.now);
    assert(r.setupLease && !r.setupTestToken && r.setup.generation==generation);
    assert(draft.call("POST","/api/settings","rotorName=NewName"+token,false).status==200);
    fakeHardware.now+=9000;draft.api.serviceSetup(fakeHardware.now);
    assert(!draft.restart.pending()&&!r.setupRestartRequired);
    assert(draft.call("POST","/api/device","role=controller&wifi=existing&uart=0&port=4534"+token,false).status==200);
    assert(!draft.restart.pending()&&!r.setupRestartRequired); // unchanged role/link is live
    assert(draft.call("POST","/api/setup","action=end_save"+token,false).status==200);
    assert(draft.call("POST","/api/setup","action=advance&generation="+std::to_string(r.setup.generation),false).status==200);
    assert(r.setup.step==SetupStep::Model);
    const auto active=r.controller.hardware;
    assert(draft.call("POST","/api/setup","action=drive&driveInterface=0&simulation=0&generation="+std::to_string(r.setup.generation)+token,false).status==200);
    assert(r.setup.drive==0&&!r.setup.simulation&&r.controller.hardware==active&&!draft.restart.pending());
    Runtime restored;assert(draft.storage.loadSetup(restored)&&restored.setup.drive==0&&!restored.setup.simulation);
    assert(draft.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    assert(draft.call("POST","/api/hardware","token=11&generation=1&driveInterface=2"+token,false).body.find("setup_drive_mismatch")!=std::string::npos);
    assert(draft.call("POST","/api/setup","action=end_save"+token,false).status==200);
    assert(draft.call("POST","/api/setup","action=drive&driveInterface=3&simulation=0&generation="+std::to_string(r.setup.generation)+token,false).status==200);
    assert(r.setup.drive==3&&r.controller.hardware==active);
    assert(draft.storage.loadSetup(restored)&&restored.setup.drive==3);
    assert(draft.call("POST","/api/setup","action=drive&driveInterface=4&simulation=0&generation="+std::to_string(r.setup.generation)+token,false).status==400);
    assert(draft.call("POST","/api/setup","action=drive&driveInterface=2&simulation=1&generation=0"+token,false).status==409);
  }
  {
    Fixture batch;auto &r=batch.runtime;
    assert(batch.call("POST","/api/setup","action=start",false).status==200);
    const auto token="&setupToken="+std::to_string(r.setupLease);
    assert(batch.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    assert(batch.call("POST","/api/network","choice=0&wifiMode=dhcp&ethMode=dhcp"+token,false).status==200);
    assert(batch.call("POST","/api/wifi","ssid=NewNetwork&pass=password123"+token,false).status==200);
    assert(batch.call("POST","/api/ap","pass=accesspoint123"+token,false).status==200);
    assert(r.setupRestartRequired&&!batch.restart.pending());
    assert(batch.call("POST","/api/setup","action=end_save"+token,false).status==200);
    assert(batch.call("POST","/api/setup","action=advance&generation="+std::to_string(r.setup.generation),false).status==409);
    assert(batch.call("POST","/api/setup","action=apply"+token,false).status==200);
    assert(batch.restart.pending()&&r.setup.pendingNext);
    Runtime boot;assert(batch.storage.loadSetup(boot)&&boot.setup.pendingNext); // power loss retains continuation
    assert(batch.restart.consume(fakeHardware.now+1500));r.setupRestartRequired=false;r.setupLease=0;
    WiFi.connectionStatus=0;batch.api.serviceSetup(fakeHardware.now);assert(r.setup.step==SetupStep::Network);
    WiFi.connectionStatus=WL_CONNECTED;batch.api.serviceSetup(fakeHardware.now);assert(r.setup.step==SetupStep::Role&&!r.setup.pendingNext);
  }
  {
    Fixture hardware;auto &r=hardware.runtime;
    assert(hardware.call("POST","/api/setup","action=start",false).status==200);r.setup.step=SetupStep::Equipment;
    const auto token="&setupToken="+std::to_string(r.setupLease);
    assert(hardware.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    const std::string body="token=11&generation=1&buttonActiveLow=0&mb.address=2";
    assert(hardware.call("POST","/api/hardware",body+"&mb.invalid=3"+token,false).status==400);
    Preferences::failedBytesKey="config1";
    assert(hardware.call("POST","/api/hardware",body+token,false).status==503);
    Preferences::failedBytesKey.clear();
    assert(hardware.call("POST","/api/hardware",body+token,false).status==200);
    Runtime modbusBoot;assert(hardware.storage.loadModbus(modbusBoot)&&modbusBoot.modbus.address==2);
    assert(r.hardwareRecord.candidate.buttonLow==false&&r.setupRestartRequired&&!hardware.restart.pending());
    assert(hardware.call("POST","/api/setup","action=apply"+token,false).status==200&&hardware.restart.pending());
  }
  {
    Fixture role;assert(role.call("POST","/api/setup","action=start",false).status==200);
    assert(role.runtime.setup.equipment);
    role.runtime.setup.step=SetupStep::Programs;
    assert(role.call("POST","/api/device","role=hud&wifi=existing&uart=0&port=4534",false).status==200);
    assert(!role.runtime.setup.equipment && role.runtime.setup.step==SetupStep::Role);
  }
  {
    Fixture sa;assert(sa.call("POST","/api/settings","model=G800SA",false).status==200);
    assert(sa.runtime.controller.model==Model::G800SA);
    assert(sa.call("GET","/api/status","",false).body.find("\"model\":\"G800SA\"")!=std::string::npos);
    Runtime boot;sa.storage.loadForkSettings(boot);assert(boot.controller.model==Model::G800SA);
  }
  {
    Fixture net;NetworkConfig stored;
    assert(net.runtime.controller.jog(0,1,false,fakeHardware.now)==Result::Ok);bool stoppedBeforeNetworkWrite=false;
    Preferences::beforeBytesWrite=[&](const char *key){if(std::string(key)=="ipv4v1"){stoppedBeforeNetworkWrite=true;assert(!net.runtime.controller.moving());}};
    assert(net.call("POST","/api/network","wifiMode=static&wifiAddress=192.168.8.10&wifiMask=255.255.255.0&wifiGateway=192.168.8.1&wifiDns1=192.168.8.1",false).status==200);
    assert(stoppedBeforeNetworkWrite);Preferences::beforeBytesWrite={};
    assert(net.storage.loadNetwork(stored)&&!stored.wifi.automatic&&stored.wifi.address==0xc0a8080a);
    const auto body=net.call("GET","/api/network","",false).body;assert(body.find("192.168.8.10")!=body.npos);
    assert(net.call("POST","/api/network","wifiGateway=192.168.9.1",false).status==400);
    assert(net.call("POST","/api/network","wifiMask=255.0.255.0",false).status==400);
    assert(net.call("POST","/api/network","wifiAddress=192.168.8.0",false).status==400);
    assert(net.call("POST","/api/network","wifiAddress=192.168.8.256",false).status==400);
    assert(net.call("POST","/api/network","wifiMode=dhcp",false).status==200);
    assert(net.storage.loadNetwork(stored)&&stored.wifi.automatic);
    auto bytes=encodeNetwork(stored);for(unsigned i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;NetworkConfig n;assert(!decodeNetwork(bad,n));}
    Preferences::failedBytesKey="ipv4v1";assert(net.call("POST","/api/network","choice=1",false).status==503);
    Preferences::failedBytesKey.clear();
  }

#if SELECTED_BOARD == BOARD_ESP32
  {
    Fixture pot;auto cfg=pot.runtime.controller.hardware;cfg.simulation=false;cfg.pins=PinMap{};cfg.sensors[6]=cfg.sensors[7]=1;
    pot.runtime.controller.configureHardware(cfg);fakeHardware.millivolts[cfg.pins.azAdc]=2000;fakeHardware.millivolts[cfg.pins.elAdc]=1000;
    pot.hardware.beginAdc();const auto diag=pot.call("GET","/api/adcdiag","",false);
    assert(diag.body.find("\"azMv\":2000")!=diag.body.npos && diag.body.find("\"azValue\":1300")!=diag.body.npos);
    assert(diag.body.find("\"elMv\":1000")!=diag.body.npos && diag.body.find("\"elValue\":2300")!=diag.body.npos);
  }
  {
    Fixture sensors;
    auto &c=sensors.runtime.controller;
    auto cfg=c.hardware;cfg.simulation=false;cfg.pins=PinMap{};cfg.sensors={1,6,0,2,54,0};
    cfg.pins.azSensorSda=cfg.pins.elSensorSda=21;cfg.pins.azSensorScl=cfg.pins.elSensorScl=22;
    c.configureHardware(cfg);fakeI2c[0]={};fakeI2c[1]={};
    fakeI2c[0].devices[6][3]=0xff;fakeI2c[0].devices[6][4]=0xfc;
    fakeI2c[0].devices[54][0xb]=0x20;fakeI2c[0].devices[54][0xc]=0x0a;fakeI2c[0].devices[54][0xd]=0xbc;
    sensors.hardware.beginAdc();
    const auto diag=sensors.call("GET","/api/adcdiag","",false);
    assert(diag.status==200 && diag.body.find("\"azRaw\":16383")!=diag.body.npos);
    assert(diag.body.find("\"azMv\":null")!=diag.body.npos && diag.body.find("\"elValue\":2748")!=diag.body.npos);
    assert(diag.body.find("\"azSensor\":\"MT6701\"")!=diag.body.npos);
    assert(sensors.call("GET","/api/config","",false).body.find("\"elSensor\":\"AS5600\"")!=std::string::npos);
    fakeI2c[0].devices[54][0xb]=0;
    assert(sensors.call("GET","/api/adcdiag","",false).body.find("\"elValue\":-1")!=std::string::npos);
    const auto unavailable=sensors.call("POST","/api/cal","p=el0",false);
    assert(unavailable.status==409 && sensors.calibration.state().elCalMv[0]==-1);
  }
  {
    Fixture sensors;
    const auto body="token=11&generation=1&simulation=0&azSensor=1&azSensorAddress=6&pin.azSensorSda=21&pin.azSensorScl=22&pin.azAdc=-1";
    assert(sensors.call("POST","/api/hardware",body,false).status==200);
    const auto state=sensors.call("GET","/api/hardware","",false);
    assert(state.body.find("\"azSensor\":1")!=state.body.npos);
    assert(sensors.runtime.hardwareRecord.good.sensorType(0)==0 && sensors.runtime.hardwareRecord.candidate.sensorType(0)==1);
    const auto bad=sensors.call("POST","/api/hardware","token=11&generation=2&azSensor=2&azSensorAddress=6",false);
    assert(bad.status==400 && bad.body.find("invalid_position_sensor")!=bad.body.npos);
  }
#endif
  {
    Fixture mb;
    assert(mb.storage.loadModbus(mb.runtime)&&mb.runtime.modbus==ModbusConfig{});
    assert(mb.call("GET","/api/hardware/modbus").body.find("\"address\":1")!=std::string::npos);
    assert(mb.call("POST","/api/hardware/modbus","token=11&generation=1&pc=1").body.find("modbus_pc_pins")!=std::string::npos);
    auto wiring=mb.runtime.controller.hardware;wiring.pins.uartDe=8;wiring.pins.hudRx=9;wiring.pins.hudTx=10;wiring.pins.hudDe=11;
    mb.runtime.controller.configureHardware(wiring);mb.runtime.hardwareRecord.candidate=wiring;mb.runtime.hardwareHudUart=true;
    const std::string fields="token=11&generation=1&pc=1&bus=1&address=1&node=202&first=201&count=2";
    CommandForm duplicate;assert(!duplicate.parse(fields+"&count=2"));
    assert(mb.call("POST","/api/hardware/modbus","token=11&generation=1&count=5").status==400);
    assert(mb.call("POST","/api/hardware/modbus","token=11&generation=1&address=201").status==400);
    assert(mb.call("POST","/api/hardware/modbus","token=10&generation=1").status==409);
    Preferences::failedBytesKey="config1";assert(mb.call("POST","/api/hardware/modbus",fields).status==503);Preferences::failedBytesKey.clear();
    assert(mb.call("POST","/api/hardware/modbus",fields).status==200);
    assert(mb.storage.loadModbus(mb.runtime)&&mb.runtime.modbus.pc==1&&mb.runtime.modbus.bus==1&&mb.runtime.modbus.node==202);
    assert(mb.call("POST","/api/device","role=hud&wifi=existing&uart=0&port=4534").status==400);
    auto bytes=encodeModbus(mb.runtime.modbus);ModbusConfig decoded;assert(decodeModbus(bytes,decoded)&&decoded==mb.runtime.modbus);
    for(unsigned i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;assert(!decodeModbus(bad,decoded));}
    Preferences::database["modbus"]["config1"]=Preferences::Bytes(15,0);assert(!mb.storage.loadModbus(mb.runtime));
  }
  {
    Fixture ports;
    const std::string auth="token=11&generation=1";
    assert(ports.runtime.modbus.uart0.baud==9600&&ports.runtime.modbus.uart1.baud==115200);
    const auto saved=ports.runtime.modbus;
    for(const auto *bad:{"uart0Baud=0","uart1Baud=12345","uart0Parity=3","uart1Parity=256","uart0StopBits=0","uart1StopBits=3","uart0Baud=-9600","uart1Baud=4294967296"}) {
      assert(ports.call("POST","/api/hardware/modbus",auth+"&"+bad).status==400);
      assert(ports.runtime.modbus==saved);
    }
    auto wiring=ports.runtime.controller.hardware;
    wiring.pins.uartDe=8;wiring.pins.hudRx=9;wiring.pins.hudTx=10;wiring.pins.hudDe=11;
    ports.runtime.controller.configureHardware(wiring);ports.runtime.hardwareHudUart=true;
    for(unsigned parity=0;parity<3;++parity)for(unsigned stops=1;stops<=2;++stops) {
      const auto fields=auth+"&uart0Baud=38400&uart0Parity="+std::to_string(parity)+"&uart0StopBits="+std::to_string(stops)+
        "&uart1Baud=2400&uart1Parity="+std::to_string(2-parity)+"&uart1StopBits="+std::to_string(3-stops);
      assert(ports.call("POST","/api/hardware/modbus",fields).status==200);
      assert(ports.storage.loadModbus(ports.runtime));
      const auto line0=ports.runtime.modbus.uart0,line1=ports.runtime.modbus.uart1;
      assert(line0.baud==38400&&line0.parity==parity&&line0.stopBits==stops);
      assert(line1.baud==2400&&line1.parity==2-parity&&line1.stopBits==3-stops);
      for(const auto *protocol:{"&pc=1&bus=1","&pc=0&bus=0"}) {
        assert(ports.call("POST","/api/hardware/modbus",auth+protocol).status==200);
        assert(ports.storage.loadModbus(ports.runtime));
        assert(ports.runtime.modbus.uart0==line0&&ports.runtime.modbus.uart1==line1);
      }
    }
    const auto response=ports.call("GET","/api/hardware/modbus");
    assert(response.body.find("\"uart0Baud\":38400")!=response.body.npos&&response.body.find("\"uart1Baud\":2400")!=response.body.npos);
    wiring.pins.uartDe=-1;wiring.pins.hudRx=wiring.pins.hc05Rx;wiring.pins.hudTx=wiring.pins.hc05Tx;wiring.pins.hudDe=wiring.pins.hc05State;
    wiring.bluetooth=false;ports.runtime.controller.configureHardware(wiring);
    assert(ports.call("POST","/api/device","role=hud&wifi=existing&uart=1&port=4534&baud=115200").body.find("uart_settings_in_hardware")!=std::string::npos);
    assert(ports.call("POST","/api/device","role=hud&wifi=existing&uart=1&port=4534").status==200);
    Runtime reboot;ports.storage.loadHud(reboot);assert(ports.storage.loadModbus(reboot));
    assert(reboot.modbus==ports.runtime.modbus&&reboot.hud.config.link.baud==2400);
    CommandForm form;HardwareForm parsed;ModbusConfig combined;
    assert(form.parse(auth+"&simulation=1&mb.uart0Baud=57600&mb.uart0Parity=2&mb.uart0StopBits=2&mb.uart1Baud=4800&mb.uart1Parity=1&mb.uart1StopBits=1"));
    assert(parseHardwareArguments(form.view(),parsed,true,ports.runtime.controller.hardware,&combined));
    assert(combined.uart0.baud==57600&&combined.uart0.parity==2&&combined.uart0.stopBits==2&&combined.uart1.baud==4800&&combined.uart1.parity==1);
  }
  {
    // Migrate exact legacy line settings once, including the old HUD UART0 rate.
    Fixture legacy;
    LegacyModbusBytes bytes{'M','B',1,1,1,1,201,201,1};modbusChecksum(bytes);
    Preferences::database["modbus"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
    assert(legacy.storage.loadModbus(legacy.runtime));
    assert((legacy.runtime.modbus.uart0==UartConfig{19200,1,1}));
    assert((legacy.runtime.modbus.uart1==UartConfig{115200,1,1}));
    assert(legacy.storage.loadModbus(legacy.runtime)&&legacy.runtime.modbus.uart0.baud==19200);
    for(unsigned i=0;i<bytes.size();++i){auto bad=bytes;bad[i]^=1;ModbusConfig result;assert(!decodeModbus(bad,result));}
    Preferences::database.erase("modbus");
    legacy.runtime.hud.config.uart=true;legacy.runtime.hud.config.link.baud=4800;
    assert(legacy.storage.saveHudConfig(legacy.runtime.hud.config));
    assert(legacy.storage.loadModbus(legacy.runtime)&&legacy.runtime.modbus.uart0.baud==4800);
    legacy.runtime.hud.config.link.baud=115200;
    assert(legacy.storage.loadModbus(legacy.runtime)&&legacy.runtime.modbus.uart0.baud==4800);
    Preferences::database.erase("modbus");Preferences::failedBytesKey="config1";
    assert(!legacy.storage.loadModbus(legacy.runtime));Preferences::failedBytesKey.clear();
  }
  {
    Fixture local;
    const std::string fields="token=11&generation=1&simulation=1&localInputMode=3&pin.localAzAdc=36&pin.localElAdc=39&localAzMin=100&localAzCenter=1900&localAzMax=3800&localDeadzone=180&localHysteresis=40&pin.localAzNegative="+
        std::to_string(SELECTED_BOARD==BOARD_WT32_ETH01?32:21)+"&pin.localAzPositive="+std::to_string(SELECTED_BOARD==BOARD_WT32_ETH01?33:22);
    assert(local.call("POST","/api/hardware",fields,false).status==200);
    assert(local.runtime.hardwareRecord.candidate.local[4]==1900);
    local.storage.loadHardware(local.runtime);
    assert(local.runtime.hardwareTrial);
    local.runtime.hardwareTrial=false;local.runtime.controller.configurationLocked=false;
    local.hardware.beginInputs();
    fakeHardware.millivolts[36]=1900;fakeHardware.millivolts[39]=2048;
    local.hardware.serviceLocalInputs();
    assert(local.call("POST","/api/hardware/joystick/start","token=11&generation=2",false).status==200);
    assert(local.runtime.controller.configurationLocked);
    assert(local.runtime.controller.jog(0,1,false,fakeHardware.now)==Result::Unavailable);
    fakeHardware.now+=10;local.hardware.serviceLocalInputs();
    auto raw=local.call("POST","/api/hardware/joystick/poll","token=11&generation=2",false);
    assert(raw.status==200 && raw.body.find("[1900,2048]")!=raw.body.npos);
    assert(local.call("POST","/api/hardware/joystick/poll","token=12&generation=2").status==409);
    fakeHardware.now+=2001;local.hardware.serviceLocalInputs();
    assert(!local.hardware.calibratingJoystick() && !local.runtime.controller.configurationLocked);
    assert(local.call("POST","/api/hardware/joystick/poll","token=11&generation=2").status==409);
    assert(local.call("POST","/api/hardware","token=11&generation=2&localAzCenter=100").status==400);
    assert(local.call("POST","/api/hardware","token=11&generation=2&localInputMode=1&pin.localAzPositive=-1").status==400);
    assert(local.runtime.hardwareRecord.candidate.local[4]==1900);
    assert(local.hardware.startJoystickCalibration());
    Preferences::failedBytesKey="config1";
    assert(local.call("POST","/api/hardware/rollback","token=11&generation=2").status==503);
    Preferences::failedBytesKey.clear();
    assert(!local.hardware.calibratingJoystick() && local.runtime.controller.configurationLocked);
    fakeHardware.now+=2001;local.hardware.serviceLocalInputs();
    assert(local.runtime.controller.configurationLocked); // failed configuration stays locked
  }
  if(argc==2 && std::string(argv[1])=="--serve") {
    Fixture fixture;CommandServer server;
    server.begin(fixture.runtime.hud.boot,100,[](void *context,std::string_view method,std::string_view path,FormView form,JsonWriter &out,int &status,const char *&type) {
      static_cast<Fixture *>(context)->api.execute(method,path,form,out,status,type,true);
    },&fixture,&fixture.runtime.hud.selectionEpoch);
    std::string line;char response[COMMAND_REPLY_MAX+1];
    while(std::getline(std::cin,line)) {
      const auto size=server.process(1,line,++fakeHardware.now,response,sizeof response);
      std::cout.write(response,size);std::cout << '\n' << std::flush;
    }
    return 0;
  }
#if SELECTED_BOARD == BOARD_WT32_ETH01
  {
    Fixture wired;ETH={};
    auto network=wired.call("GET","/api/network").body;
    assert(network.find("\"ethernetSupported\":true")!=network.npos);
    assert(network.find("\"ethLink\":false")!=network.npos);
    assert(network.find("\"ethConnected\":false")!=network.npos);
    ETH.link=true;
    network=wired.call("GET","/api/network").body;
    assert(network.find("\"ethLink\":true")!=network.npos);
    assert(network.find("\"ethConnected\":false")!=network.npos);
    ETH.ip=true;
    network=wired.call("GET","/api/network").body;
    assert(network.find("\"ethConnected\":true")!=network.npos);
    assert(network.size()<=COMMAND_PAGE_MAX);
    auto status=wired.call("GET","/api/status","",false).body;
    assert(status.find("\"wifiMode\":\"Ethernet + AP\"")!=status.npos);
    assert(status.find("\"ethConnected\":true")!=status.npos);
    assert(status.find("\"ethStarted\":false")!=status.npos);
    assert(status.find("\"rssi\":null")!=status.npos);
    ETH.link=false;
    status=wired.call("GET","/api/status","",false).body;
    assert(status.find("\"wifiMode\":\"Access Point\"")!=status.npos);
    assert(status.find("\"ethConnected\":false")!=status.npos);
    auto hardware=wired.call("GET","/api/hardware","",false).body;
    assert(hardware.find("\"classicEsp32\":true")!=hardware.npos);
    const auto reserved=wired.call("POST","/api/hardware","token=11&generation=1&pin.azPositive=25",false);
    assert(reserved.status==400 && reserved.body.find("ethernet.rmii")!=reserved.body.npos);
    assert(!wired.restart.pending());
    const auto boot=wired.call("POST","/api/hardware","token=11&generation=1&buttonGpio=0",false);
    assert(boot.status==400 && boot.body.find("ethernet.rmii")!=boot.body.npos);
  }
  std::cout << "PASS: WT32 Ethernet DHCP/carrier API, Wi-Fi fallback status and reserved-pin HTTP rejection\n";
  return 0;
#endif
  {
    Fixture held;auto &c=held.runtime.controller;auto hw=c.hardware;
    hw.drive=DRIVE_BTS7960;hw.simulation=false;hw.pins=PinMap{};c.configureHardware(hw);
    held.hardware.beginDirections();c.feedback(90,45,true,true);
    const std::string identity="&client=123&session=456&gesture=1&mask=3";
    assert(held.call("POST","/api/move","d=H&az=-1&el=1&serial=1"+identity,false).status==200);
    assert(c.axes[0].direction==-1&&c.axes[1].direction==1);
    fakeHardware.now+=1000;c.tick(fakeHardware.now);held.hardware.applyOutputs();
    assert(!c.moving()&&!c.axes[0].wanted&&!c.axes[1].wanted);
    assert(fakeHardware.levels[c.hardware.pins.azSpeedEnable]==LOW&&fakeHardware.levels[c.hardware.pins.elSpeedEnable]==LOW);
    assert(held.call("POST","/api/move","d=H&az=-1&el=1&serial=2"+identity,false).status==400);
    assert(!c.moving()); // No release request arrived, but the expired press cannot restart.
  }
  {
    // Regression for the ESP32 panic: an HTTP speed/mode request performs a
    // position solve while the other axis is already running. STOP must still
    // reach both outputs before the request returns.
    Fixture motion;auto &c=motion.runtime.controller;
    auto hw=c.hardware;hw.drive=DRIVE_BTS7960;hw.simulation=false;hw.pins=PinMap{};
    c.configureHardware(hw);
    motion.hardware.beginDirections();
    for(unsigned axis=0;axis<2;++axis) {
      auto settings=c.motionSettings[axis];settings.ready=true;
      settings.mode=RegulatorMode::Cascade;settings.kickEnabled=false;
      assert(c.configureMotion(axis,settings,fakeHardware.now));
      c.feedbackAt(axis,axis?45:90,true,fakeHardware.now);
    }
    assert(motion.call("POST","/api/mode","mode=auto",false).status==200);
    assert(motion.call("POST","/api/target","az=150&el=100",false).status==200);
    for(unsigned n=0;n<100;++n) {
      fakeHardware.now+=20;
      c.feedbackAt(0,90+n*.02f,true,fakeHardware.now);
      c.feedbackAt(1,45+n*.02f,true,fakeHardware.now);
      const auto speed=motion.call("POST","/api/speed","axis=az&value=1",false);
      assert(speed.status==200);
      assert(c.axes[0].motionError==MotionError::None && c.axes[1].motionError==MotionError::None);
    }
    assert(c.moving());
    assert(motion.call("POST","/api/move","d=S",false).status==200);
    assert(!c.moving() && c.axes[0].speedOutputPercent==0 && c.axes[1].speedOutputPercent==0);
    assert(fakeHardware.levels[c.hardware.pins.azSpeedEnable]==LOW &&
           fakeHardware.levels[c.hardware.pins.elSpeedEnable]==LOW);
  }
  {
    Fixture tuning;
    auto &c=tuning.runtime.controller;
    c.hardware.drive=DRIVE_BTS7960;c.hardware.simulation=false;c.hardware.pins=PinMap{};
    c.feedbackAt(0,90,true,fakeHardware.now);c.feedbackAt(1,45,true,fakeHardware.now);
    const auto defaults=tuning.call("GET","/api/motion/autotune","full=1");
    assert(defaults.status==200 && defaults.body.find("\"tuneSearchStartPercent\":5")!=std::string::npos);
    assert(tuning.call("POST","/api/mode","mode=auto",false).status==200);
    const std::string start="action=start&axis=az&mode=simple&id=0&travel=60&outputLimit=50&tuneSearchStartPercent=15&tuneSearchStepPercent=10&tuneLambdaFactor=2";
    assert(tuning.call("POST","/api/motion/autotune",start).status==200);
    assert(c.autoTune.active() && c.autoTune.options.searchStartPercent==15);
    assert(c.mode==Mode::Manual);
    const auto compact=tuning.call("GET","/api/motion/autotune");
    assert(compact.status==200 && compact.body.find("options")==std::string::npos);
    const auto full=tuning.call("GET","/api/motion/autotune","full=1");
    assert(full.status==200 && full.body.find("\"tuneSearchStepPercent\":10")!=std::string::npos);
    // HTTP and USB project the same volatile methodology from DeviceApi.
    const auto http=tuning.call("GET","/api/motion/autotune","",false);
    assert(http.status==200 && http.body.find("\"tuneLambdaFactor\":2")!=std::string::npos);
    assert(tuning.call("POST","/api/motion/autotune","action=cancel").status==200);
    assert(!c.autoTune.active() && !c.moving());
    // Replay an expired coast: its context must survive STOP and later motion,
    // and both transports must project the same immutable failure snapshot.
    auto &t=c.autoTune;t.phase=AutoTunePhase::Coast;t.phaseAt=fakeHardware.now-11000;
    t.startedAt=fakeHardware.now;t.threshold=.2f;
    fakeHardware.now+=20; // The preceding API calls already ticked at the old stamp.
    c.tick(fakeHardware.now);
    if(t.phase!=AutoTunePhase::Failed || std::string(t.reason)!="coast_timeout")
      std::cerr<<"expired coast "<<Controller::autoTunePhaseName(t.phase)<<" reason "<<t.reason
        <<" ticked "<<t.ticked<<" tickAt "<<t.tickAt<<" now "<<fakeHardware.now<<'\n';
    assert(t.phase==AutoTunePhase::Failed && std::string(t.reason)=="coast_timeout");
    c.axes[0].current=123;
    const auto failureWire=tuning.call("GET","/api/motion/autotune","full=1");
    const auto failureHttp=tuning.call("GET","/api/motion/autotune","",false);
    assert(failureWire.status==200 && failureWire.body==failureHttp.body);
    assert(failureWire.body.find("\"failure\":{\"phase\":\"coast\"")!=std::string::npos);
    assert(failureWire.body.find("\"position\":90")!=std::string::npos);
    assert(failureWire.body.find("\"velocityWindowMs\":200")!=std::string::npos);
    t.reason="reposition_timeout";t.preparationRequired=29.46f;t.preparationAvailable=28.18f;
    t.preparationAttempts=7;
    const auto travelWire=tuning.call("GET","/api/motion/autotune","full=1");
    const auto travelHttp=tuning.call("GET","/api/motion/autotune","",false);
    assert(travelWire.status==200 && travelWire.body==travelHttp.body);
    assert(travelWire.body.find("\"requiredTravel\":29.46")!=std::string::npos);
    assert(travelWire.body.find("\"availableTravel\":28.18")!=std::string::npos);
    assert(travelWire.body.find("\"preparationAttempts\":7")!=std::string::npos);
    t.trackingDurationMs={5200,10500,4600,7500};t.trackingWarmupMs={2200,4500,2200,4500};
    t.trackingPeriodMs=1500;t.trackingTimeoutMs=763000;
    t.trackingResults[0].failedCase=6;
    const auto planWire=tuning.call("GET","/api/motion/autotune","full=1");
    const auto planHttp=tuning.call("GET","/api/motion/autotune","",false);
    assert(planWire.status==200 && planWire.body==planHttp.body);
    assert(planWire.body.find("\"durationMs\":[5200,10500,4600,7500]")!=std::string::npos);
    assert(planWire.body.find("\"warmupMs\":[2200,4500,2200,4500]")!=std::string::npos);
    assert(planWire.body.find("\"failedCase\":6")!=std::string::npos);
    assert(planWire.body.find("\"methodVersion\":14")!=std::string::npos);
    assert(planWire.body.find("\"timeoutMs\":763000")!=std::string::npos);
    assert(tuning.call("GET","/api/motion/autotune").body.find("failure")==std::string::npos);
  }
  {
    Fixture merged;
    merged.runtime.recoveryHistory.record(RecoveryReason::LowMemory);
    const auto hardware=merged.call("GET","/api/hardware","",false).body;
    assert(hardware.find("\"failureCount\":1")!=hardware.npos);
    assert(hardware.find("\"failureLimit\":3")!=hardware.npos);
    assert(hardware.find("\"failureResetMs\":1800000")!=hardware.npos);
    assert(hardware.find("\"lastFailure\":\"low_memory\"")!=hardware.npos);
    assert(hardware.find("\"pins\":")!=hardware.npos);
    auto status=merged.call("GET","/api/status","",false).body;
    assert(status.find("\"connectedSsid\":\"\"")!=status.npos);
    assert(status.find("\"wifiConnecting\":false")!=status.npos);
    assert(status.find("\"localFeedback\":[null,null]")!=status.npos);
    WiFi.connectionStatus=WL_CONNECTED;WiFi.connectedSsid="station";
    status=merged.call("GET","/api/status","",false).body;
    assert(status.find("\"connectedSsid\":\"station\"")!=status.npos);
  }
  for(const char *method:{"GET","POST"}) {
    Fixture wifi;
    wifi.runtime.hud.config.wifi=HudWifi::Host;
    assert(wifi.call(method,"/api/wifi","ssid=station&pass=12345678",false).status==409);
    assert(wifi.network.credentials().wifiSsid.length()==0 && !wifi.restart.pending());
    wifi.runtime.hud.config.wifi=HudWifi::Existing;
    assert(wifi.call(method,"/api/wifi","ssid=station&pass=bad",false).status==400);
    assert(!wifi.network.wifiConnecting());
    assert(wifi.call(method,"/api/wifi","ssid=station&pass=12345678",false).status==200);
    assert(wifi.network.credentials().wifiSsid=="station" && wifi.network.credentials().wifiPass=="12345678");
    assert(wifi.network.wifiConnecting() && wifi.network.apAlwaysOn() && !wifi.restart.pending());
    assert(wifi.call("GET","/api/status","",false).body.find("\"wifiConnecting\":true")!=std::string::npos);
  }
  // Recovery retries of the confirmed map use one restart; real edits keep a trial.
  for(const char *path:{"/api/hardware","/api/hardware/rollback"}) {
    Fixture retry;auto &r=retry.runtime;
    const auto good=r.hardwareRecord.good;
    assert(retry.storage.enterRecovery(r,RecoveryReason::Requested));retry.storage.loadHardware(r);
    assert(r.recovery && r.controller.configurationLocked);
    r.setup.required=true;r.setup.equipment=false;r.controller.setupRequired=true;assert(retry.storage.saveSetup(r.setup));
    const auto fields=std::string("token=11&generation=1")+(std::string(path)=="/api/hardware"?"&buttonGpio="+std::to_string(good.buttonGpio):"");
    const auto response=retry.call("POST",path,fields,false);
    assert(response.status==200 && response.body.find("\"confirmationRequired\":false")!=std::string::npos);
    assert(retry.restart.pending() && r.controller.configurationLocked && !r.controller.moving());
    retry.storage.loadHardware(r);assert(!r.recovery && !r.hardwareTrial && !r.controller.configurationLocked);
    assert(r.controller.hardware==good && !r.controller.moving());
    assert(retry.call("POST","/api/hardware/confirm","token=11&generation=2",false).status==409);
    assert(retry.storage.loadSetup(r) && !r.setup.equipment);
    assert(retry.call("GET","/api/setup","",false).body.find("\"equipment\":true")!=std::string::npos);
  }
  {
    Fixture migration;HardwareRecord legacy;
    legacy.hasConfirmed=true;legacy.phase=ConfigPhase::Confirmed;
    legacy.good.drive=DRIVE_BTS7960;legacy.good.pwmInverted=true;
    legacy.good.bluetooth=true;legacy.good.bluetoothMode=BT_ESP32_SPP;
    legacy.candidate=legacy.good;
    const auto bytes=encodeHardware(legacy);
    Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
    auto &runtime=migration.runtime;migration.storage.loadHardware(runtime);
    assert(!runtime.recovery && runtime.hardwareRecord.hasConfirmed);
    assert(runtime.hardwareRecord.good.bluetooth && runtime.hardwareRecord.good.drive==DRIVE_BTS7960);
    assert(runtime.controller.hardware.drive==DRIVE_BTS7960 && runtime.controller.hardware.bluetooth);
    assert(runtime.controller.hardware.backend()==BT_NONE);
    assert(migration.call("POST","/api/hardware","token=11&generation=1&bluetoothEnabled=0").status==501);
    assert(migration.call("POST","/api/hardware","token=11&generation=1&forceApSta=1").status==200);
    assert(runtime.hardwareRecord.candidate.bluetooth && runtime.hardwareRecord.candidate.pwmInverted && runtime.hardwareRecord.candidate.forceAp);
    migration.storage.loadHardware(runtime);
    assert(runtime.hardwareTrial && !runtime.recovery && runtime.controller.hardware.drive==DRIVE_BTS7960);
  }
  {
    Fixture pins;
    const auto original=pins.runtime.hardwareRecord.good;
    pins.runtime.controller.configureHardware(original);
    const auto changed=pins.call("POST","/api/hardware","token=11&generation=1&pin.azAdc=36",false);
    assert(changed.status==200 && changed.body.find("\"confirmationRequired\":true")!=std::string::npos);
    assert(pins.runtime.hardwareRecord.good==original);
    assert(pins.runtime.hardwareRecord.candidate.pins.azAdc==36);
    assert(pins.runtime.controller.hardware.pins.azAdc==original.pins.azAdc);
    assert(pins.call("POST","/api/hardware","token=11&generation=1&pin.azAdc=39").status==409);
    const auto bad=pins.call("POST","/api/hardware","token=11&generation=2&pin.azAdc=25");
    assert(bad.status==400 && bad.body.find("invalid_adc_gpio")!=bad.body.npos && bad.body.find("pin.azAdc")!=bad.body.npos);
    assert(pins.runtime.hardwareRecord.generation==2 && pins.runtime.hardwareRecord.candidate.pins.azAdc==36);
    assert(pins.call("POST","/api/hardware","token=11&generation=2%26pin.led=0&simulation=1").status==400);
    const auto conflict=pins.call("POST","/api/hardware","token=11&generation=2&pin.led=25");
    assert(conflict.status==400 && conflict.body.find("pin.azNegative")!=conflict.body.npos);
    pins.storage.loadHardware(pins.runtime);
    assert(pins.runtime.hardwareTrial && pins.runtime.controller.hardware.pins.azAdc==36);
    assert(pins.call("GET","/api/status","",false).body.find("\"azAdc\":36")!=std::string::npos);
    assert(pins.call("GET","/api/hardware","view=confirmed").body.find("\"azAdc\":34")!=std::string::npos);
  }
  {
    Fixture incompatible;
    auto record=incompatible.runtime.hardwareRecord;record.good.buttonGpio=100;
    record.candidate=record.good;record.generation=42;
    const auto bytes=encodeHardware(record);
    Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
    incompatible.storage.loadHardware(incompatible.runtime);
    assert(incompatible.runtime.recovery && incompatible.runtime.hardwareRecord.hasConfirmed);
    const auto reply=incompatible.call("POST","/api/hardware/rollback","token=11&generation=42",false);
    assert(reply.status==400 && reply.body.find("buttonGpio")!=std::string::npos);
    assert(!incompatible.restart.pending() && incompatible.runtime.hardwareRecord.generation==42);
    assert(incompatible.call("POST","/api/hardware","token=11&generation=42&buttonGpio=-1",false).status==200);
    incompatible.storage.loadHardware(incompatible.runtime);
    assert(incompatible.runtime.hardwareTrial && incompatible.runtime.hardwareRecord.hasConfirmed);
    assert(incompatible.storage.confirmHardware(incompatible.runtime));
  }
  {
    Fixture simulation;
    const auto confirmed=simulation.runtime.hardwareRecord.good;
    assert(simulation.call("POST","/api/hardware","token=11&generation=1&simulation=1&pin.azAdc=25&pin.azNegative=25&pin.azSensorSda=25&azSensor=1",false).status==200);
    const auto &candidate=simulation.runtime.hardwareRecord.candidate;
    assert(candidate.simulation && simulation.runtime.hardwareRecord.good==confirmed);
    for(const auto &d:pinDefinitions)
      if(d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl)
        assert(candidate.pins.*d.member==-1);
    simulation.storage.loadHardware(simulation.runtime);
    assert(simulation.runtime.hardwareTrial && simulation.runtime.controller.hardware==candidate);
  }
  {
    Fixture recovery;recovery.runtime.recovery=true;
    assert(recovery.storage.addResetHistoryEntry(4,"PANIC"));
    assert(recovery.storage.addResetHistoryEntry(3,"SOFTWARE"));
    const auto log=recovery.call("GET","/api/hardware/reboots","",false);
    assert(log.status==200&&log.body.find("\"count\":2")!=std::string::npos);
    assert(log.body.find("SOFTWARE")<log.body.find("PANIC"));
    assert(log.body.find("\"current\":")!=std::string::npos);
    const auto before=Preferences::database["g5500rst"];
    assert(recovery.call("GET","/api/hardware/reboots").status==200);
    assert(Preferences::database["g5500rst"]==before);
    assert(recovery.call("POST","/api/hardware/reboots","token=11&generation=1",false).status==404);
    assert(recovery.call("POST","/api/resethistory/clear","",false).status==409);
    assert(Preferences::database["g5500rst"]==before);
    Preferences::readOnlyBeginSucceeds=false;
    assert(recovery.call("GET","/api/hardware/reboots","",false).body.find("\"available\":false")!=std::string::npos);
    Preferences::readOnlyBeginSucceeds=true;
  }
  {
    Fixture hud;auto &r=hud.runtime;
    auto role=r.hud.config;role.role=DeviceRole::Hud;role.uart=false;role.simulationEnabled=false;
    assert(hud.storage.saveHudConfig(role));
    HardwareRecord record;record.hasConfirmed=true;
    record.good.simulation=false;record.good.bluetooth=false;
    for(const auto &d:pinDefinitions)
      if(d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl)
        record.good.pins.*d.member=-1;
    record.candidate=record.good;const auto bytes=encodeHardware(record);
    Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
    hud.storage.loadHud(r,false);hud.storage.loadHardware(r);
    assert(!r.recovery && r.configuredAsHud() && !r.hud.config.simulationEnabled);
    r.setup.required=r.setup.equipment=true;r.setup.step=SetupStep::Equipment;
    r.setup.drive=DRIVE_DIR_PWM;r.setup.simulation=true;
    assert(hud.call("GET","/api/setup","",false).body.find("\"ready\":true")!=std::string::npos);
    r.setup.required=false;r.setup.drive=-1;
    assert(hud.call("GET","/api/hardware","",false).body.find("\"role\":\"hud\"")!=std::string::npos);
    assert(hud.call("POST","/api/hardware","token=11&generation=1&pin.led=-1",false).status==200);
    assert(!r.hardwareRecord.candidate.simulation && r.hardwareRecord.candidate.pins.azAdc==-1);
    assert(hud.call("POST","/api/hardware","token=11&generation=2&pin.uartRx=1&pin.uartTx=-1",false).status==400);
    assert(hud.storage.enterRecovery(r,RecoveryReason::Requested));
    hud.storage.loadHardware(r);r.hud.config.role=DeviceRole::Controller;
    assert(r.recovery && r.configuredAsHud());
    assert(hud.call("GET","/api/hardware","",false).body.find("\"role\":\"hud\"")!=std::string::npos);
    assert(hud.call("POST","/api/hardware","token=11&generation=2&pin.led=-1",false).status==200);
    r.recovery=false;r.hud.config.role=DeviceRole::Hud;
    assert(hud.call("POST","/api/device","role=hud&wifi=existing&uart=1&port=4534",false).status==200);
    assert(hud.call("POST","/api/device","role=controller&wifi=existing&uart=1&port=4534",false).status==400);
  }
  {
    Fixture failure;auto &c=failure.runtime.controller;
    assert(c.jog(0,1,false,fakeHardware.now)==Result::Ok);
    c.tick(fakeHardware.now);assert(c.moving());
    bool attempted=false;
    Preferences::failedBytesKey="config1";
    Preferences::beforeBytesWrite=[&](const char *key) {
      if(std::string(key)=="config1") {attempted=true;assert(!c.moving() && c.configurationLocked);}
    };
    assert(failure.call("POST","/api/hardware","token=11&generation=1&pin.led=-1").status==503);
    Preferences::beforeBytesWrite={};Preferences::failedBytesKey.clear();
    assert(attempted && !c.moving() && c.configurationLocked);
    assert(failure.runtime.hardwareRecord.generation==1 && !failure.restart.pending());
  }
  {
    Fixture complete; CommandServer server;
    server.begin(1,100,[](void *context,std::string_view method,std::string_view path,FormView form,JsonWriter &out,int &status,const char *&type) {
      static_cast<Fixture *>(context)->api.execute(method,path,form,out,status,type,true);
    },&complete);
    char reply[COMMAND_REPLY_MAX+1];
    server.process(1,"@v=1&cid=1&method=GET&path=/api/hello&client=1",0,reply,sizeof reply);
    std::string request="@v=1&cid=2&sid=101&boot=0000000000000001&selection=0&page=0&method=POST&path=/api/hardware&token=11&generation=1&driveInterface=0&simulation=1&bluetoothEnabled=0&bluetoothMode=3&forceApSta=0&buttonGpio=0&buttonActiveLow=1&buttonPullup=1&pwmInverted=0";
    // Send the complete active map. Inactive roles grow with supported display
    // types and need not consume the bounded UART command payload.
    auto selected=HardwareConfig{};selected.simulation=true;selected.bluetooth=false;
    for(const auto &d:pinDefinitions)if(board::pinActive(selected,d))
      request+='&'+std::string(d.field)+'='+std::to_string(selected.pins.*d.member);
    assert(request.size()<COMMAND_INPUT_MAX);
    assert(server.process(1,request,1,reply,sizeof reply)>0);
    assert(std::string(reply).find("\"status\":200")!=std::string::npos);
    assert(complete.runtime.hardwareRecord.generation==2);
  }
  {
    Fixture disabled;auto config=disabled.runtime.controller.hardware;
    config.pins.uartRx=config.pins.uartTx=-1;disabled.runtime.controller.configureHardware(config);
    const auto response=disabled.call("POST","/api/device","role=hud&wifi=existing&uart=1&port=4534");
    assert(response.status==400 && response.body.find("uart_disabled")!=response.body.npos);
  }
  {
    Fixture relay;auto config=relay.runtime.controller.hardware;
    config.pins.uartRx=config.pins.uartTx=-1;
    config.pins.hudRx=config.pins.hc05Rx;config.pins.hudTx=config.pins.hc05Tx;config.pins.hudDe=config.pins.hc05State;
    relay.runtime.controller.configureHardware(config);
    const auto enabled=relay.call("POST","/api/device","role=hud&wifi=existing&uart=1&port=4534");
    assert(enabled.status==200); // Dedicated RS-485 works with UART0 disabled.
  }
  {
    Fixture bridge;
    const std::string base="role=hud&wifi=existing&uart=1&port=4534";
    for (const auto &bad : {"&baud=12345","&baud=0","&intervalMs=99","&intervalMs=10001","&readyHigh=2"})
      assert(bridge.call("POST","/api/device",base+bad).status==400);
    assert(bridge.call("POST","/api/device",base+"&baud=9600&intervalMs=2000&readyHigh=0").status==200);
    Runtime restored;bridge.storage.loadHud(restored);
    const auto &link=restored.hud.config.link;
    assert(link.baud==9600 && link.intervalMs==2000 && !link.readyHigh);
    bridge.runtime.hud.config=restored.hud.config;
    const auto body=bridge.call("GET","/api/device").body;
    assert(body.find("\"hudBaud\":9600")!=body.npos && body.find("\"hudIntervalMs\":2000")!=body.npos);
  }
  Fixture f;auto &r=f.runtime;
  assert(f.call("GET","/api/status").body.size()<=COMMAND_PAGE_MAX);
  const auto axis=f.call("GET","/api/status","view=axis&axis=az");
  assert(axis.status==200 && axis.body.size()<=COMMAND_PAGE_MAX && axis.body.find("motionPhase")!=axis.body.npos);
  const auto configuration=f.call("GET","/api/config").body;
  r.controller.axes[0].current+=0.1f;r.controller.axes[0].speedOutputPercent=15;
  assert(f.call("GET","/api/config").body==configuration); // telemetry does not invalidate settings pages
  assert(f.call("GET","/api/hardware").body.find("\"pins\"")!=std::string::npos);
  const auto capabilities=f.call("GET","/api/hardware/pins").body;
  assert(capabilities.size()<STATUS_JSON_CAPACITY && capabilities.find("\"roles\"")!=capabilities.npos);
  assert(f.call("GET","/api/hardware/pins","",false).body==capabilities);
  assert(f.call("GET","/api/network").body.size()<=COMMAND_PAGE_MAX);
  const auto http=f.call("GET","/api/status","",false);assert(http.status==200 && http.body.find("\"livePort\":0")!=http.body.npos);
  assert(f.call("POST","/api/mode","mode=auto").status==200 && r.controller.mode==Mode::Auto);
  assert(f.call("POST","/api/target","az=100&el=50").status==200);
  assert(f.call("POST","/api/move","d=S").status==200 && !r.controller.moving());
  const auto speed=r.controller.speeds[0].speedPercent;
  assert(f.call("POST","/api/settings","speedPercent=101").status==400 && r.controller.speeds[0].speedPercent==speed);
  assert(f.call("POST","/api/settings","pin.led=0").status==400);
  assert(f.call("POST","/api/settings","commandPort=4534").status==400);
  assert(f.call("POST","/api/settings","commandPort=3232").status==400);
  assert(f.call("POST","/api/settings","commandPort=5353").status==400);
  assert(f.call("POST","/api/settings","commandPort=32769").status==400);
  assert(f.call("POST","/api/settings","commandPort=4600&speedPercent=60").status==200);
  Runtime loaded;f.storage.loadForkSettings(loaded);assert(loaded.loggerSettings.commandPort==4600);
  assert(f.call("GET","/api/motion").body.find("PositionKp")!=std::string::npos);
  assert(f.call("POST","/api/motion/reset","axis=all").status==200);
  assert(f.call("POST","/api/azrange","profile=custom&start=-90&span=450").status==200 && f.actions.rangeCalls==1);
  assert(f.call("POST","/api/elrange","profile=custom&start=-20&span=160").status==200 && f.actions.rangeCalls==2);
  assert(f.call("POST","/api/azcal","op=add&angle=50").status==200 && f.actions.pointCalls==1);
  f.actions.failCal=true;
  assert(f.call("POST","/api/azrange","profile=p36").status==503);
  f.actions.failCal=false;
  // Configuration from a UART command remains accessible without Wi-Fi.
  r.recovery=true;r.controller.configurationLocked=true;
  const auto positionReads=f.actions.positionReads;
  assert(f.call("GET","/api/status").status==200 && f.actions.positionReads==positionReads);
  assert(f.call("POST","/api/target","az=120").status==409);
  const auto feedbackDiagnostics=f.call("GET","/api/diagnostics");
  assert(feedbackDiagnostics.status==200);
  assert(feedbackDiagnostics.body.find("\"feedbackRejected\":")!=std::string::npos);
  assert(feedbackDiagnostics.body.find("\"feedbackInvalid\":")!=std::string::npos);
  assert(feedbackDiagnostics.body.find("\"rejectedIntervalMs\":")!=std::string::npos);
  assert(feedbackDiagnostics.body.find("\"lastGoodPosition\":")!=std::string::npos);
  AllocationFailure previous;previous.size=4096;previous.caps=3;previous.freeBytes=22000;previous.largestBlock=4000;
  previous.function="malloc";previous.pc[0]=0x12345678;allocationRecordText(previous.task,sizeof previous.task,"tcpip_task");
  uint8_t previousElf[32]{};previousElf[0]=0xab;
  captureAllocationRecord(runtimeMetrics.previousFailures[1],previous,previousElf);
  const auto persisted=f.call("GET","/api/diagnostics","previousFailure=1");
  assert(persisted.status==200&&persisted.body.size()<=COMMAND_PAGE_MAX);
  assert(persisted.body.find("\"size\":4096")!=std::string::npos&&persisted.body.find("\"elf\":\"ab00")!=std::string::npos);
  assert(f.call("GET","/api/hardware/reboots").body.find("\"previousAllocations\":[{\"ready\":false},{\"ready\":true")!=std::string::npos);
  runtimeMetrics.previousFailures[1]={};assert(f.call("GET","/api/diagnostics","previousFailure=2").status==400);
  for(const auto *axis:{"az","el"}) {
    const auto one=f.call("GET","/api/diagnostics",std::string("axis=")+axis);
    assert(one.status==200 && one.body.size()<=COMMAND_PAGE_MAX);
    assert(one.body.find(std::string("\"axis\":\"")+axis+'"')!=std::string::npos);
    assert(one.body.find("\"timing\":{")!=std::string::npos);
  }
  assert(f.call("GET","/api/diagnostics","axis=bad").status==400);
  assert(f.call("POST","/api/wifi","ssid=station&pass=12345678").status==200 && !f.restart.pending());
  assert(f.network.wifiConnecting() && f.network.apAlwaysOn());
  f.restart.consume(fakeHardware.now+2000);assert(!f.restart.pending());
  std::string fields="token=11&generation="+std::to_string(r.hardwareRecord.generation)+
    "&driveInterface=2&simulation=1&bluetoothEnabled=0&bluetoothMode=3&forceApSta=0&buttonGpio=-1&buttonActiveLow=1&buttonPullup=0&pwmInverted=1";
  assert(f.call("POST","/api/hardware",fields).status==200);
  f.storage.loadHardware(r);assert(r.hardwareTrial && !r.recovery);f.ready();
  for(uint32_t elapsed:{120000u,299999u,300000u}) {
    fakeHardware.now=elapsed;
    const auto status=f.call("GET","/api/hardware","",false);
    assert(status.body.find("\"remainingMs\":"+std::to_string(300000u-elapsed))!=std::string::npos);
  }
  f.ready();
  const std::string stamp="token=11&generation="+std::to_string(r.hardwareRecord.generation);
  assert(f.call("POST","/api/hardware/confirm",stamp).status==409);
  Preferences::failedBytesKey="config1";
  assert(f.call("POST","/api/hardware/probe",stamp).status==409);
  Preferences::failedBytesKey.clear();
  assert(f.call("POST","/api/hardware/probe",stamp).status==200);
  assert(f.call("POST","/api/hardware/confirm",stamp).status==200);
  assert(!r.controller.configurationLocked);
  f.storage.loadHardware(r);assert(!r.hardwareTrial && !r.recovery);assert(f.storage.finishHardwareBoot(r));
  assert(f.call("POST","/api/hardware/probe","token=11&generation=1").status==409);
  const auto otaStatus=f.call("GET","/api/ota");assert(otaStatus.status==200 && otaStatus.body.size()<=COMMAND_PAGE_MAX);
  assert(f.call("POST","/api/ota/arduino","token=wrong").status==403);
  assert(f.call("POST","/api/ota/reboot","token=wrong").status==403);
  assert(f.call("POST","/api/device","role=hud&wifi=existing&uart=0&port=4600").status==400);
  assert(f.call("POST","/api/device","role=hud&wifi=existing&uart=0&port=4534").status==200);
  r.hud.config.role=DeviceRole::Hud;
  assert(r.hud.add(2,{0x0100007f,4534,false},"first",true));
  assert(r.hud.add(3,{0x0200007f,4534,false},"second",true));
  const auto catalog=f.call("GET","/api/hud/peers");
  assert(catalog.body.find("\"total\":2")!=catalog.body.npos);
  const auto firstTransport=catalog.body.find("\"transport\"");
  assert(firstTransport!=catalog.body.npos && catalog.body.find("\"transport\"",firstTransport+1)==catalog.body.npos);
  assert(f.call("GET","/api/hud/peers").status==200);
  assert(f.call("POST","/api/azrange","profile=p36").status==403);
  r.hud.begin(1, 10);
  f.restart.consume(fakeHardware.now+2000);assert(!f.restart.pending());
  assert(!r.hud.peer(HUD_SIMULATION_ID));
  assert(f.call("POST","/api/device","simulationEnabled=bad").status==400);
  assert(f.call("POST","/api/device","simulationEnabled=1&port=4535").status==400);
  Preferences::failedBytesKey="config";
  assert(f.call("POST","/api/device","simulationEnabled=1").status==507);
  assert(!r.hud.config.simulationEnabled && !r.hud.peer(HUD_SIMULATION_ID));
  Preferences::failedBytesKey.clear();
  assert(f.call("POST","/api/device","simulationEnabled=1").status==200);
  assert(r.hud.config.simulationEnabled);
  Runtime restoredSimulation;f.storage.loadHud(restoredSimulation);
  assert(restoredSimulation.hud.config.simulationEnabled);
  assert(r.hud.peer(HUD_SIMULATION_ID));
  assert(f.call("POST","/api/hud/peers","id=fffffffffffe&action=remove").status==400);
  assert(f.call("POST","/api/hud/peers","id=fffffffffffe&name=Other").status==400);
  assert(r.hud.count()==3);
  assert(f.call("GET","/api/device").body.find("\"hudSimulationEnabled\":true")!=std::string::npos);
  assert(r.hud.select(HUD_SIMULATION_ID));
  Preferences::failedBytesKey="config";
  assert(f.call("POST","/api/device","simulationEnabled=0").status==507);
  assert(r.hud.config.simulationEnabled && r.hud.config.selected==HUD_SIMULATION_ID);
  Preferences::failedBytesKey.clear();
  assert(f.call("POST","/api/device","simulationEnabled=0").status==200);
  assert(!r.hud.config.simulationEnabled && !r.hud.config.selected && !r.hud.peer(HUD_SIMULATION_ID));
  Runtime disabledSimulation;f.storage.loadHud(disabledSimulation);
  assert(!disabledSimulation.hud.config.simulationEnabled && !disabledSimulation.hud.config.selected);
  assert(!f.restart.pending());
  // The hardware test/confirmation path never arms motion until reboot.
  assert(r.controller.driveFault && !r.controller.moving());
  std::cout << "PASS: shared device API: HTTP/wire status, motion, settings/NVS, calibration routing, recovery without Wi-Fi, hardware probe/confirm, OTA and HUD\n";
}
