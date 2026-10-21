#include "device_api.hpp"
#include "command_api.hpp"
#include "runtime_metrics.hpp"
#include <ArduinoOTA.h>
#include <cassert>
#include <iostream>
#include <string>
using namespace forkrot;
// Radio/clock adapters are absent; the production DeviceApi, Controller,
// Calibration, Storage, OTA and command protocol run unchanged on the host.
namespace forkrot {
bool Network::sendHud(const HudEndpoint &,const uint8_t *,size_t) {return false;}
void Network::resetHudInputs() {}
bool Network::hudPortValid(unsigned h,unsigned n,unsigned p,unsigned r) const {return portsValid(h,n,p,r,runtime_.loggerSettings.commandPort);}
void Network::setupLoggerUdp() {}
void Network::setupCommandUdp() {}
void Network::connectWifi(const String &ssid,const String &password) {
  credentials_.wifiSsid=ssid;credentials_.wifiPass=password;
  wifiSetupAp_=true;wifiConnectPending_=true;wifiConnectRequestedMs_=millis();
}
void Diagnostics::dbgPrintln(const String &line) {diagLog_.append({line.c_str(),line.length()},true);}
void Diagnostics::diagClear() {diagLog_.clear();}
void Diagnostics::writeLog(JsonWriter &out) const {char data[2300];const auto n=copyLog(data,sizeof data);out << std::string_view(data,n);}
bool Diagnostics::writeHistoryJson(JsonWriter &out,RssiPeriod) {out << "{\"samples\":[]}";return true;}
}
struct Actions : DeviceActions {
  Runtime &r;Calibration &cal;Storage &storage;WifiCredentials credentials;
  bool failCal=false;unsigned rangeCalls=0,pointCalls=0,positionReads=0;
  Actions(Runtime &runtime,Calibration &c,Storage &s):r(runtime),cal(c),storage(s) {}
  void updatePosition() override {++positionReads;r.currentAz=r.controller.axes[0].current;r.currentEl=r.controller.axes[1].current;}
  bool saveCal() override {return !failCal && storage.saveCal(r,cal);}
  void setCalibrationMode(CalibrationMode mode) override {cal.state().calibrationMode=mode;saveCal();}
  void setGsAzMode(uint8_t mode) override {configureAzRange(mode,0,mode==36?360:450);}
  void captureGs232Cal(const String &p) override {cal.captureAzEndpoint(p=="F",fakeHardware.millivolts[board::PIN_AZ_ADC]);saveCal();}
  bool configureAzRange(uint8_t p,float start,float span) override {++rangeCalls;return !failCal && cal.configureAz(p,start,span) && saveCal();}
  bool configureElRange(uint8_t p,float start,float span) override {++rangeCalls;return !failCal && cal.configureEl(p,start,span) && saveCal();}
  bool editAzPoint(const char *op,float angle) override {++pointCalls;return !failCal && cal.editAzPoint(op,angle,100) && saveCal();}
  bool editElPoint(const char *op,float angle) override {++pointCalls;return !failCal && cal.editElPoint(op,angle,100) && saveCal();}
  void loadPrefs() override {storage.loadPrefs(r,cal,credentials);}
  void loadMotionSettings() override {storage.loadMotionSettings(r);}
  void loadProfilePresets() override {storage.loadProfilePresets(r);}
  void saveProfilePresets() override {storage.saveProfilePresets(r);}
  void saveForkSettings() override {if(!storage.saveForkSettings(r))r.controller.fault="motion_storage_failed";}
  void setupWeb() override {}
  void invalidateDisplay() override {}
};
struct Fixture {
  Runtime runtime;
  Calibration calibration{runtime.controller};Storage storage;Hardware hardware{runtime};Diagnostics diagnostics{storage};
  Actions actions{runtime,calibration,storage};Network network{runtime,actions,calibration,hardware,diagnostics};
  Ota ota;RestartDelay restart;DeviceApi api{runtime,actions,calibration,hardware,diagnostics,network,storage,ota,restart};
  Fixture() {
    Preferences::database.clear();Preferences::failedBytesKey.clear();fakeHardware={};WiFi={};
    storage.loadHardware(runtime);assert(!runtime.recovery);assert(storage.finishHardwareBoot(runtime));
    auto config=runtime.controller.hardware;config.simulation=true;runtime.controller.configureHardware(config);
    runtime.controller.feedback(90,45,true,true);runtime.hardwareToken=11;
    runtime.hud.begin(1,0x1234);ota.begin(nullptr,nullptr);ready();
  }
  void ready() {runtime.memoryGuard.begin(0,0);runtime.memoryGuard.sample(0,40000,16000,0,false);fakeHardware.now=10001;}
  Response call(const char *method,const char *path,const std::string &fields="",bool wire=true) {
    CommandForm form;assert(form.parse(fields));char data[8000];JsonWriter out(data,sizeof data);
    int status=200;const char *type="application/json";
    api.execute(method,path,form.view(),out,status,type,wire);assert(out.good());return {status,{data,out.size()}};
  }
};
int main(int argc,char **argv) {
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
  {
    // Regression for the ESP32 panic: an HTTP speed/mode request performs a
    // position solve while the other axis is already running. STOP must still
    // reach both outputs before the request returns.
    Fixture motion;auto &c=motion.runtime.controller;
    auto hw=c.hardware;hw.drive=DRIVE_BTS7960;hw.simulation=false;
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
    c.hardware.drive=DRIVE_BTS7960;c.hardware.simulation=false;
    c.feedbackAt(0,90,true,fakeHardware.now);c.feedbackAt(1,45,true,fakeHardware.now);
    const auto defaults=tuning.call("GET","/api/motion/autotune","full=1");
    assert(defaults.status==200 && defaults.body.find("\"tuneSearchStartPercent\":5")!=std::string::npos);
    const std::string start="action=start&axis=az&mode=simple&id=0&travel=60&outputLimit=50&tuneSearchStartPercent=15&tuneSearchStepPercent=10&tuneLambdaFactor=2";
    assert(tuning.call("POST","/api/motion/autotune",start).status==200);
    assert(c.autoTune.active() && c.autoTune.options.searchStartPercent==15);
    const auto compact=tuning.call("GET","/api/motion/autotune");
    assert(compact.status==200 && compact.body.find("options")==std::string::npos);
    const auto full=tuning.call("GET","/api/motion/autotune","full=1");
    assert(full.status==200 && full.body.find("\"tuneSearchStepPercent\":10")!=std::string::npos);
    // HTTP and USB project the same volatile methodology from DeviceApi.
    const auto http=tuning.call("GET","/api/motion/autotune","",false);
    assert(http.status==200 && http.body.find("\"tuneLambdaFactor\":2")!=std::string::npos);
    assert(tuning.call("POST","/api/motion/autotune","action=cancel").status==200);
    assert(!c.autoTune.active() && !c.moving());
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
  {
    Fixture migration;HardwareRecord legacy;
    legacy.hasConfirmed=true;legacy.phase=ConfigPhase::Confirmed;
    legacy.good.drive=DRIVE_BTS7960;legacy.good.pwmInverted=true;
    legacy.good.bluetooth=true;legacy.good.bluetoothMode=BT_ESP32_SPP;
    legacy.candidate=legacy.good;
    const auto bytes=encodeHardware(legacy);
    Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
    auto &runtime=migration.runtime;migration.storage.loadHardware(runtime);
    assert(runtime.recovery && runtime.hardwareRecord.hasConfirmed);
    assert(runtime.hardwareRecord.good.bluetooth && runtime.hardwareRecord.good.drive==DRIVE_BTS7960);
    assert(runtime.controller.hardware.drive==DRIVE_BTS7960 && !runtime.controller.hardware.bluetooth);
    assert(migration.call("POST","/api/hardware/rollback","token=11&generation=1").status==200);
    assert(!runtime.hardwareRecord.candidate.bluetooth && runtime.hardwareRecord.candidate.pwmInverted);
    migration.storage.loadHardware(runtime);
    assert(runtime.hardwareTrial && !runtime.recovery && runtime.controller.hardware.drive==DRIVE_BTS7960);
  }
  {
    Fixture pins;
    const auto original=pins.runtime.hardwareRecord.good;
    assert(pins.call("POST","/api/hardware","token=11&generation=1&pin.azAdc=36",false).status==200);
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
    for(const auto &d:pinDefinitions) request+='&'+std::string(d.field)+'='+std::to_string(PinMap{}.*d.member);
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
  assert(f.call("GET","/api/diagnostics").status==200);
  assert(f.call("POST","/api/wifi","ssid=station&pass=12345678").status==200 && !f.restart.pending());
  assert(f.network.wifiConnecting() && f.network.apAlwaysOn());
  f.restart.consume(fakeHardware.now+2000);assert(!f.restart.pending());
  std::string fields="token=11&generation="+std::to_string(r.hardwareRecord.generation)+
    "&driveInterface=2&simulation=1&bluetoothEnabled=0&bluetoothMode=3&forceApSta=0&buttonGpio=-1&buttonActiveLow=1&buttonPullup=0&pwmInverted=1";
  assert(f.call("POST","/api/hardware",fields).status==200);
  f.storage.loadHardware(r);assert(r.hardwareTrial && !r.recovery);f.ready();
  const std::string stamp="token=11&generation="+std::to_string(r.hardwareRecord.generation);
  assert(f.call("POST","/api/hardware/confirm",stamp).status==409);
  Preferences::failedBytesKey="config1";
  assert(f.call("POST","/api/hardware/probe",stamp).status==409);
  Preferences::failedBytesKey.clear();
  assert(f.call("POST","/api/hardware/probe",stamp).status==200);
  assert(f.call("POST","/api/hardware/confirm",stamp).status==200);
  assert(r.controller.configurationLocked);
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
  // The hardware test/confirmation path never arms motion until reboot.
  assert(r.controller.driveFault && !r.controller.moving());
  std::cout << "PASS: shared device API: HTTP/wire status, motion, settings/NVS, calibration routing, recovery without Wi-Fi, hardware probe/confirm, OTA and HUD\n";
}
