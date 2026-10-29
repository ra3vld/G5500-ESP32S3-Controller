#include "device_api_fixture.hpp"

static void bookmark(Fixture &f) {
  auto &r=f.runtime;
  r.hud.config.role=DeviceRole::Controller;r.controller.remote=nullptr;
  r.controller.driveFault=false;r.controller.fault.clear();
  r.setup.required=true;r.setup.paused=true;r.setup.step=SetupStep::Calibration;r.setup.generation=77;
  r.controller.setupRequired=true;
  assert(f.storage.saveSetup(r.setup));
}
static void navigationIsPassive() {
  Fixture f;auto &r=f.runtime;auto &c=r.controller;
  r.hud.config.role=DeviceRole::Controller;c.remote=nullptr;c.driveFault=false;c.fault.clear();
  WiFi.connectionStatus=WL_CONNECTED;
  assert(c.setMode(Mode::Auto,fakeHardware.now)==Result::Ok);
  assert(r.protocol.line("M005 100 120",fakeHardware.now).result==Result::Ok);
  const auto sequence=r.protocol.line("N",fakeHardware.now).reply;
  assert(sequence.find("/2")!=std::string::npos&&c.moving());
  const auto stop=c.stopEpoch,selection=r.hud.selectionEpoch;
  auto intact=[&]{assert(c.moving()&&c.stopEpoch==stop&&r.hud.selectionEpoch==selection);
    assert(r.protocol.line("N",fakeHardware.now).reply==sequence);assert(!f.restart.pending());};
  for(const char *action:{"start","pause","resume"}) {
    assert(f.call("POST","/api/setup",std::string("action=")+action,false).status==200);intact();
  }
  const auto token="&setupToken="+std::to_string(r.setupLease);
  for(const char *action:{"begin_save","end_save"}) {
    assert(f.call("POST","/api/setup",std::string("action=")+action+token,false).status==200);intact();
  }
  assert(f.call("POST","/api/setup","action=advance&generation="+std::to_string(r.setup.generation),false).status==200);intact();
  assert(f.call("POST","/api/setup","action=back",false).status==200);intact();
  Preferences::failedBytesKey="state1";
  assert(f.call("POST","/api/setup","action=pause",false).status==503);intact();
  Preferences::failedBytesKey.clear();
}
static void pausedProgressDoesNotAffectSettings() {
  Fixture f;bookmark(f);auto &r=f.runtime;auto &c=r.controller;
  const auto saved=Preferences::database["setup"]["state1"];
  r.setupSaving=true;r.setupRestartRequired=true;r.setupTestToken=123;r.setupRenewed=0;
  assert(c.jog(0,1,false,fakeHardware.now)==Result::Ok);
  const auto stop=c.stopEpoch,selection=r.hud.selectionEpoch;
  f.api.serviceSetup(fakeHardware.now+Runtime::SetupTestTimeoutMs+1);
  assert(c.moving()&&c.stopEpoch==stop&&!r.setupTestToken);
  Preferences::failedBytesKey="state1";
  assert(f.call("POST","/api/settings","rotorName=Ordinary",false).status==200);
  assert(c.moving()&&c.stopEpoch==stop&&r.hud.selectionEpoch==selection);
  assert(f.call("POST","/api/settings","model=G800SA",false).status==200);
  assert(r.setup.paused&&r.setup.step==SetupStep::Calibration&&r.setup.generation==77);
  assert(Preferences::database["setup"]["state1"]==saved);
  Preferences::failedBytesKey.clear();
  assert(f.call("POST","/api/setup","action=resume",false).status==200);
  assert(!r.setup.paused&&r.setup.step==SetupStep::Calibration);
}
static void staleBatchDoesNotCaptureOrdinaryRequests() {
  {
    Fixture f;bookmark(f);auto &r=f.runtime;
    r.setupSaving=true;r.setupLease=12;
    assert(f.call("POST","/api/wifi","ssid=Router&pass=password123",false).status==200);
    assert(f.network.wifiConnecting()&&!f.restart.pending()&&!r.setupRestartRequired);
  }
  {
    Fixture f;bookmark(f);f.runtime.setupSaving=true;f.runtime.hud.config.wifi=HudWifi::Host;
    assert(f.call("POST","/api/wifi","ssid=Router&pass=password123",false).status==409);
  }
  {
    Fixture f;bookmark(f);auto &r=f.runtime;r.setupSaving=true;r.setup.drive=3;
    Preferences::failedBytesKey="state1";
    assert(f.call("POST","/api/hardware","token=11&generation=1&driveInterface=0&buttonActiveLow=0",false).status==200);
    assert(f.restart.pending()&&!r.setupRestartRequired);
    assert(r.setup.paused&&r.setup.step==SetupStep::Calibration&&r.setup.generation==77);
  }
  {
    Fixture f;bookmark(f);auto &r=f.runtime;r.setupSaving=true;
    Preferences::failedBytesKey="state1";
    assert(f.call("POST","/api/network","choice=0&wifiMode=dhcp&ethMode=dhcp",false).status==200);
    assert(f.restart.pending()&&!r.setupRestartRequired&&r.setup.step==SetupStep::Calibration);
  }
#if MQTT_SUPPORT
  {
    Fixture f;bookmark(f);auto &r=f.runtime;r.setupSaving=true;
    Preferences::failedBytesKey="state1";
    assert(f.call("POST","/api/mqtt","enabled=1&host=broker.local",false).status==200);
    assert(f.restart.pending()&&!r.setupRestartRequired);
  }
#endif
  // An abandoned active save is also local to requests carrying its token.
  {
    Fixture f;assert(f.call("POST","/api/setup","action=start",false).status==200);
    const auto token="&setupToken="+std::to_string(f.runtime.setupLease);
    assert(f.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    assert(f.call("POST","/api/ap","pass=accesspoint123",false).status==200);
    assert(f.restart.pending()&&!f.runtime.setupRestartRequired);
  }
}
static void progressFailureDoesNotCancelOrdinaryWork() {
  {
    Fixture f;auto &r=f.runtime;
    assert(f.call("POST","/api/setup","action=start",false).status==200);
    r.setup.step=SetupStep::Network;r.setup.pendingNext=true;WiFi.connectionStatus=WL_CONNECTED;
    assert(r.controller.jog(0,1,false,fakeHardware.now)==Result::Ok);
    const auto stop=r.controller.stopEpoch;
    Preferences::failedBytesKey="state1";f.api.serviceSetup(fakeHardware.now);
    assert(!r.setupStorageOk&&r.controller.moving()&&r.controller.stopEpoch==stop);
  }
  {
    Fixture f;auto &r=f.runtime;
    assert(f.call("POST","/api/setup","action=start",false).status==200);
    const auto token="&setupToken="+std::to_string(r.setupLease);
    assert(f.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    r.hardwareRecord.phase=ConfigPhase::Testing;r.hardwareTrial=true;r.hardwareProbe=true;r.controller.configurationLocked=false;
    Preferences::failedBytesKey="state1";
    assert(f.call("POST","/api/hardware/confirm","token=11&generation=1"+token,false).status==200);
    assert(!r.setupStorageOk&&!r.hardwareTrial&&r.hardwareRecord.phase==ConfigPhase::Confirmed&&!f.restart.pending());
  }
  {
    Fixture f;auto &r=f.runtime;
    assert(f.call("POST","/api/setup","action=start",false).status==200);
    const auto token="&setupToken="+std::to_string(r.setupLease);
    assert(f.call("POST","/api/setup","action=begin_save"+token,false).status==200);
    assert(f.call("POST","/api/ap","pass=accesspoint123"+token,false).status==200);
    assert(r.setupRestartRequired&&!f.restart.pending());
    Preferences::failedBytesKey="state1";
    assert(f.call("POST","/api/setup","action=pause",false).status==503);
    assert(f.restart.pending()); // Saved settings still take effect if the bookmark cannot be written.
  }
  Fixture f;bookmark(f);auto &r=f.runtime;auto &c=r.controller;
  auto hw=c.hardware;hw.simulation=false;hw.drive=DRIVE_YAESU;c.configureHardware(hw);c.speedEnabled=false;
  c.feedback(90,45,true,true);
  c.coast.phase=CoastMeasurement::Complete;c.coast.measured[0]=3;c.coast.revision=c.rangeRevision;
  c.coast.degrees[0][0]=1;c.coast.degrees[0][1]=2;
  Preferences::failedBytesKey="state1";
  assert(f.call("POST","/api/motion/coast","action=apply&axis=0",false).status==200);
  assert(c.speeds[0].coastDegrees==2&&r.setup.step==SetupStep::Calibration);
  Preferences::failedBytesKey.clear();
  // Even a damaged/unwritable bookmark leaves confirmed hardware available.
  Preferences::database["setup"]["state1"]=Preferences::Bytes(16,0);Preferences::failedBytesKey="state1";
  Runtime boot;r.setupStorageOk=f.storage.loadSetup(boot);assert(!r.setupStorageOk);
  f.storage.loadHardware(boot);
  assert(!boot.recovery&&!boot.controller.configurationLocked);
}
int main() {
  navigationIsPassive();pausedProgressDoesNotAffectSettings();staleBatchDoesNotCaptureOrdinaryRequests();progressFailureDoesNotCancelOrdinaryWork();
  std::cout<<"PASS: wizard navigation/bookmark failures preserve motion and protocol state; paused checkpoints and ordinary settings, Wi-Fi, MQTT, hardware, coast and restart behavior remain independent\n";
}
