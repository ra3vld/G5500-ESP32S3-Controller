// Relay-only commissioning through the production API. GPL-3.0.
#include "device_api.hpp"
#include "command_api.hpp"
#include "runtime_metrics.hpp"
#include <cassert>
#include <cstdio>
using namespace forkrot;
namespace forkrot {
bool Network::sendHud(const HudEndpoint &,const uint8_t *,size_t) {return false;}
void Network::resetHudInputs() {}
bool Network::hudPortValid(unsigned h,unsigned n,unsigned p,unsigned r) const {return portsValid(h,n,p,r,runtime_.loggerSettings.commandPort);}
void Network::setupLoggerUdp() {}
void Network::setupCommandUdp() {}
void Network::connectWifi(const String &ssid,const String &password) {credentials_.wifiSsid=ssid;credentials_.wifiPass=password;}
void Diagnostics::dbgPrintln(const String &line) {diagLog_.append({line.c_str(),line.length()},true);}
void Diagnostics::diagClear() {diagLog_.clear();}
void Diagnostics::writeLog(JsonWriter &out) const {out << "";}
bool Diagnostics::writeHistoryJson(JsonWriter &out,RssiPeriod) {out << "{\"samples\":[]}";return true;}
}
struct Actions : DeviceActions {
  void updatePosition() override {}
  void setCalibrationMode(CalibrationMode) override {}
  void setGsAzMode(uint8_t) override {}
  void captureGs232Cal(const String &) override {}
  bool saveCal() override {return true;}
  bool configureAzRange(uint8_t,float,float) override {return true;}
  bool editAzPoint(const char *,float) override {return true;}
  bool configureElRange(uint8_t,float,float) override {return true;}
  bool editElPoint(const char *,float) override {return true;}
  void loadPrefs() override {}
  void loadMotionSettings() override {}
  void loadProfilePresets() override {}
  void saveProfilePresets() override {}
  void saveForkSettings() override {}
  void setupWeb() override {}
  void invalidateDisplay() override {}
};
int main() {
  Preferences::database.clear();fakeHardware={};
  Runtime r;r.hud.config.role=DeviceRole::Controller;
  HardwareConfig c;c.pins.uartRx=c.pins.uartTx=-1;c.pins.azNegative=20;c.pins.azPositive=21;
  r.controller.configureHardware(c);
  r.hardwareRecord.good=r.hardwareRecord.candidate=c;r.hardwareRecord.hasConfirmed=true;
  r.setup.required=r.setup.fresh=r.setup.equipment=true;r.setup.generation=1;r.setup.step=SetupStep::Inputs;
  r.controller.setupRequired=true;r.setupStorageOk=true;
  r.setupLease=111;r.setupRenewed=1000;fakeHardware.now=1000;
  Calibration calibration(r.controller);Storage storage;Hardware hardware(r);Diagnostics diagnostics(storage);Actions actions;
  Network network(r,actions,calibration,hardware,diagnostics);network.credentials().network.choice=NetworkChoice::Ap;
  Ota ota;RestartDelay restart;DeviceApi api(r,actions,calibration,hardware,diagnostics,network,storage,ota,restart);
  auto request=[&](const char *method,const char *path,const std::string &fields="") {
    CommandForm form;assert(form.parse(fields));char data[8000];JsonWriter out(data,sizeof data);
    int status=200;const char *type="application/json";api.execute(method,path,form.view(),out,status,type,false);
    assert(out.good());return Response{status,{data,out.size()}};
  };
  r.setup.step=SetupStep::Tuning;r.controller.driveFault=true;
  assert(request("GET","/api/setup").body.find("\"ready\":false")!=std::string::npos);
  r.controller.driveFault=false;r.setup.step=SetupStep::Inputs;
  auto setup=request("GET","/api/setup");
  assert(setup.status==200 && setup.body.find("\"manualOnly\":true")!=setup.body.npos);
  assert(setup.body.find("\"ready\":true")!=setup.body.npos);
  assert(r.controller.jog(0,1,false,1000)==Result::Unavailable); // unfinished commissioning
  for(auto expected:{SetupStep::Programs,SetupStep::Review,SetupStep::Done}) {
    const auto response=request("POST","/api/setup","action="+std::string(expected==SetupStep::Done?"complete":"advance")+"&checked=1&generation="+std::to_string(r.setup.generation));
    assert(response.status==200 && r.setup.step==expected);
  }
  assert(!r.setup.required && !r.controller.setupRequired);
  assert(!r.controller.axes[0].calibrated && r.controller.jog(0,1,false,1000)==Result::Ok);
  r.controller.stopNow(-1,1001);
  const auto status=request("GET","/api/status");
  assert(status.status==200 && status.body.find("\"drive\":\"YAESU\"")!=status.body.npos);
  assert(r.controller.target({90,0,true,false},false,1001)==Result::NotCalibrated);
  std::puts("PASS: relay-only HTTP commissioning completes without fake calibration, preserves setup gates and reports local drive");
}
