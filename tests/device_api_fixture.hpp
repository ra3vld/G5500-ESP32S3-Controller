#pragma once
#include "device_api.hpp"
#include "hardware_form.hpp"
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
    CommandForm form;assert(form.parse(fields,!wire&&std::string(path).find("/api/hardware")==0?1536:COMMAND_INPUT_MAX));char data[8000];JsonWriter out(data,sizeof data);
    int status=200;const char *type="application/json";
    api.execute(method,path,form.view(),out,status,type,wire);assert(out.good());return {status,{data,out.size()}};
  }
};
