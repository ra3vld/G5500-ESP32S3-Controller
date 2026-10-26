#include "device_api_fixture.hpp"

static void physical(Fixture &f) {
  auto &r=f.runtime;auto config=r.hardwareRecord.good;
  r.hud.config.role=DeviceRole::Controller;r.controller.remote=nullptr;r.controller.driveFault=false;r.controller.fault.clear();
  config.simulation=false;config.bluetooth=false;
  if(SELECTED_BOARD==BOARD_ESP32_3248S035C) {
    config.buttonGpio=-1;config.pins.azAdc=36;config.pins.azNegative=0;config.pins.azPositive=22;
  }
  assert(!board::validateHardware(config));
  r.controller.configureHardware(config);r.controller.feedback(90,45,true,true);
  r.hardwareRecord.good=r.hardwareRecord.candidate=config;r.hardwareRecord.hasConfirmed=true;
  r.setup.required=true;r.setup.equipment=true;r.setup.step=SetupStep::Equipment;r.setup.generation=10;
  r.controller.setupRequired=true;assert(f.storage.saveSetup(r.setup));
}
static Response confirm(Fixture &f,bool allowed=true) {
  return f.call("POST","/api/setup","action=motion&confirmed="+std::to_string(allowed)+"&generation="+std::to_string(f.runtime.setup.generation),false);
}
int main() {
  {
    SetupState source;source.required=true;source.equipment=true;source.motionConfirmed=true;
    source.step=SetupStep::Equipment;source.drive=3;source.simulation=true;
    SetupState decoded;assert(decodeSetup(encodeSetup(source),decoded)&&decoded.motionConfirmed&&decoded.drive==3);
    for(unsigned version:{1u,2u}) {
      auto bytes=encodeSetup(source);bytes[0]=version;bytes[7]=version==1?31:51;
      uint32_t hash=2166136261u;for(unsigned i=0;i<12;++i)hash=(hash^bytes[i])*16777619u;
      for(unsigned i=0;i<4;++i)bytes[12+i]=uint8_t(hash>>(8*i));
      assert(decodeSetup(bytes,decoded)&&!decoded.motionConfirmed&&decoded.simulation);
    }
  }
  {
    Fixture f;auto &r=f.runtime;auto &c=r.controller;
    r.hud.config.role=DeviceRole::Controller;c.remote=nullptr;c.driveFault=false;c.fault.clear();r.setup.required=true;r.setup.equipment=true;c.setupRequired=true;
    assert(f.call("GET","/api/setup","",false).body.find("\"motionAllowed\":true")!=std::string::npos);
    HudState hud;hud.capture(c,r.protocol,fakeHardware.now);assert(!hud.driveFault);
    assert(f.call("POST","/api/move","d=R",false).status==200&&c.moving());
    assert(f.call("POST","/api/move","d=S",false).status==200&&!c.moving());
    assert(f.call("POST","/api/setup","action=pause",false).status==200&&r.setup.required&&r.setup.paused);
    assert(f.call("POST","/api/target","az=100",true).status==200&&c.moving());
    c.configurationLocked=true;
    assert(f.call("POST","/api/move","d=S",false).status==200&&!c.moving());
    assert(f.call("POST","/api/move","d=R",false).status==503);
    c.configurationLocked=false;r.recovery=true;
    assert(f.call("POST","/api/move","d=R",false).status==409);
    assert(f.call("POST","/api/move","d=S",false).status==200);
  }
  {
    Fixture f;physical(f);auto &r=f.runtime;auto &c=r.controller;
    assert(f.call("POST","/api/move","d=R",false).status==503);
    assert(f.call("POST","/api/setup","action=motion&confirmed=1&generation=9",false).status==409);
    assert(f.call("POST","/api/setup","action=motion&confirmed=2&generation=10",false).status==400);
    assert(f.call("POST","/api/setup","action=motion&confirmed=1&generation=10",true).status==405);
    for(unsigned condition=0;condition<3;++condition) {
      r.hardwareTrial=condition==0;c.configurationLocked=condition==1;r.setup.equipment=condition!=2;
      assert(confirm(f).status==409&&!c.setupMotionConfirmed);
    }
    r.hardwareTrial=false;c.configurationLocked=false;r.setup.equipment=true;
    Preferences::failedBytesKey="state1";assert(confirm(f).status==503&&!c.setupMotionConfirmed);
    Preferences::failedBytesKey.clear();r.setupStorageOk=true;
    assert(confirm(f).status==200&&c.setupMotionConfirmed&&r.setup.required&&r.setup.motionConfirmed);
    Runtime reboot;assert(f.storage.loadSetup(reboot)&&reboot.setup.required&&reboot.setup.motionConfirmed&&reboot.controller.setupMotionConfirmed);
    assert(f.call("POST","/api/move","d=R",true).status==200&&c.moving());
    assert(confirm(f,false).status==200&&!c.moving()&&!c.setupMotionConfirmed&&r.setup.required);
    assert(f.call("POST","/api/move","d=R",false).status==503);
    assert(confirm(f).status==200);
    assert(f.call("POST","/api/setup","action=pause",false).status==200&&r.setup.paused);
    assert(f.call("POST","/api/move","d=R",false).status==200&&c.moving());
    assert(f.call("POST","/api/move","d=S",false).status==200&&!c.moving());
    assert(f.call("POST","/api/settings","model=G800SA",false).status==200);
    assert(!r.setup.motionConfirmed&&!c.setupMotionConfirmed);
  }
  std::cout<<"PASS: setup motion: automatic simulation, explicit persisted physical permission, legacy migration, revoke/STOP, configuration invalidation and boot guards\n";
}
