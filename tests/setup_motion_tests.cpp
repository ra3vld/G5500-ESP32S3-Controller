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
    for(unsigned step=0;step<unsigned(SetupStep::Done);++step)for(bool paused:{false,true}) {
      r.setup.step=SetupStep(step);r.setup.paused=paused;r.setup.equipment=false;r.setup.motionConfirmed=false;
      assert(f.call("GET","/api/setup","",false).body.find("\"motionAllowed\":true")!=std::string::npos);
      HudState hud;hud.capture(c,r.protocol,fakeHardware.now);assert(!hud.driveFault);
      assert(f.call("POST","/api/move","d=R",false).status==200&&c.moving());
      assert(f.call("POST","/api/move","d=S",false).status==200&&!c.moving());
      assert(f.call("POST","/api/target","az=100",true).status==200&&c.moving());
      assert(f.call("POST","/api/move","d=S",true).status==200&&!c.moving());
    }
    r.setupStorageOk=false;
    assert(f.call("POST","/api/move","d=R",false).status==200&&c.moving());
    assert(f.call("POST","/api/move","d=S",false).status==200&&!c.moving());
    assert(f.storage.saveSetup(r.setup));
    Runtime reboot;assert(f.storage.loadSetup(reboot)&&reboot.setup.required&&!reboot.setup.motionConfirmed);
    reboot.controller.configureHardware(c.hardware);reboot.controller.feedback(90,45,true,true);
    assert(reboot.controller.jog(0,1,false,fakeHardware.now)==Result::Ok);
    c.configurationLocked=true;
    assert(f.call("POST","/api/move","d=R",false).status==503);
    assert(f.call("POST","/api/move","d=S",false).status==200);
    c.configurationLocked=false;r.recovery=true;
    assert(f.call("POST","/api/move","d=R",false).status==409);
    assert(f.call("POST","/api/move","d=S",false).status==200);
  }
  std::cout<<"PASS: setup never gates simulation or physical motion across active/paused steps, storage errors and reboot; legacy records, STOP and hardware/recovery locks retained\n";
}
