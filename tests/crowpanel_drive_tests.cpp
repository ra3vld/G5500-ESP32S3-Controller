#include "motion_fixture.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include <cassert>
#include <iostream>
namespace forkrot::crowpanel { bool beginBus() { return true; } }
using namespace forkrot;
using namespace forkrot::board;
static void reset() { fakeHardware={}; }
static uint32_t moduleDuty(uint32_t duty) { return BTS7960_PWM_INVERTED ? 1023-duty : duty; }
static std::map<int,uint32_t> duties() {
  std::map<int,uint32_t> values;
  for(const auto &e:fakeHardware.events) if(e.operation=="pwm") values[e.pin]=e.value;
  return values;
}
static bool controlOff() {
  if(DRIVE_INTERFACE==DRIVE_YAESU && !fakeHardware.gpioOwned[PIN_SPEED]) return duties()[PIN_SPEED]==1023;
  return fakeHardware.levels[PIN_SPEED]==HIGH;
}
static bool nativeStopped() {
  if(!controlOff()) return false;
  if(DRIVE_INTERFACE==DRIVE_BTS7960 && !fakeHardware.gpioOwned[PIN_LEFT]) {
    const auto values=duties();
    return moduleDuty(values.at(PIN_LEFT))==0 && moduleDuty(values.at(PIN_RIGHT))==0;
  }
  const int idle=DRIVE_INTERFACE==DRIVE_BTS7960 && BTS7960_PWM_INVERTED ? HIGH : LOW;
  return fakeHardware.levels[PIN_LEFT]==idle && fakeHardware.levels[PIN_RIGHT]==idle;
}
static void noInvalidGpio() {
  for(const auto &e:fakeHardware.events) {
    assert(e.pin==PIN_LEFT || e.pin==PIN_RIGHT || e.pin==PIN_SPEED);
    assert(e.operation!="write_unowned");
    if(e.operation=="mode" && e.pin==PIN_SPEED) assert(e.value==OUTPUT_OPEN_DRAIN);
    if(e.operation=="attach") {
      if(DRIVE_INTERFACE==DRIVE_YAESU) assert(e.pin==PIN_SPEED && e.value==20000);
      else assert(e.pin!=PIN_SPEED && e.value==20000);
    }
  }
}
static void run(MotionTestRuntime &r, Hardware &h, uint32_t start=0) {
  r.controller.feedback(20,0,true,false);r.controller.speedEnabled=true;
  assert(r.controller.jog(0,1,false,start)==Result::Ok);
  for(uint32_t at=start;at<=start+1000;at+=20) {
    fakeHardware.now=at;
    r.controller.feedbackAt(0,r.controller.axes[0].current+r.controller.axes[0].direction*0.15f,true,at);
    r.controller.tick(at);h.applyOutputs();
  }
  assert(r.controller.axes[0].direction==1 && !controlOff());
}
int main() {
  static_assert(PIN_AZ_ADC==6 && PIN_EL_ADC<0 && PIN_BACKLIGHT<0 && PIN_SPEED==0);
  static_assert(PIN_UART_RX<0 && PIN_UART_TX<0 && PIN_LEFT==44 && PIN_RIGHT==43);
  HardwareConfig cfg;assert(!validateHardware(cfg));
  cfg.drive=DRIVE_SEPARATE_PWM;assert(validateHardware(cfg));
  cfg={};cfg.bluetooth=true;assert(validateHardware(cfg));
  cfg={};assert(validateHardware(cfg,true));
  reset();MotionTestRuntime r;Hardware h(r);
  assert(r.controller.model==Model::G800DXA);
  h.beginDirections();assert(!r.controller.driveFault && nativeStopped());
  assert(fakeHardware.events.front().operation=="preload" && fakeHardware.events.front().pin==0 && fakeHardware.events.front().value==HIGH);
  assert(fakeHardware.attachCalls==(DRIVE_INTERFACE==DRIVE_YAESU ? 1u : 2u));
  h.beginAdc();assert(fakeHardware.readsRaw==1);
  fakeHardware.millivolts[6]=1500;assert(h.filteredMv(0)==1500 && fakeHardware.readsMv==25);
  h.filteredMv(1);assert(fakeHardware.readsMv==25);
  int raw=-1,mv=-1;h.adcDiagRead(PIN_EL_ADC,raw,mv);assert(raw==0 && mv==0);
  r.controller.setModel(Model::G5500);assert(r.controller.model==Model::G800DXA);
  assert(r.controller.jog(1,1,false,0)==Result::Invalid);
  assert(api(r.controller,r.protocol,r.loggerSettings,"/api/settings",{{"model","G5500"}},0).status==400);
  const auto json=stateJson(r.controller,r.protocol,r.loggerSettings,0);
  assert(json.find("\"singleAxis\":true")!=std::string::npos);
  assert(json.find("\"azSpeedEnable\":0")!=std::string::npos);
  assert(json.find(DRIVE_INTERFACE==DRIVE_YAESU ? "\"drive\":\"YAESU\"" : "\"drive\":\"BTS7960\"")!=std::string::npos);
  run(r,h);
  if(DRIVE_INTERFACE==DRIVE_BTS7960) {
    std::map<int,uint32_t> values;
    for(const auto &e:fakeHardware.events) if(e.operation=="pwm") {
      values[e.pin]=moduleDuty(e.value);assert(!(values[PIN_LEFT] && values[PIN_RIGHT]));
    }
  }
  const auto before=fakeHardware.events.size();h.allStop();
  assert(!r.controller.moving() && nativeStopped());
  assert(fakeHardware.events[before].pin==0);
  assert(fakeHardware.events[before].value==(DRIVE_INTERFACE==DRIVE_YAESU ? 1023u : unsigned(HIGH)));
  noInvalidGpio();
  // Reversal keeps BOOT at its OFF value throughout the mechanical dead-time.
  run(r,h,1100);assert(r.controller.jog(0,-1,false,2100)==Result::Ok);
  uint32_t stopped=0,reversed=0;
  for(uint32_t at=2120;at<=6500;at+=20) {
    fakeHardware.now=at;
    r.controller.feedbackAt(0,r.controller.axes[0].current+r.controller.axes[0].direction*0.15f,true,at);
    r.controller.tick(at);h.applyOutputs();
    if(!stopped && !r.controller.axes[0].direction) stopped=at;
    if(stopped && !r.controller.axes[0].direction) assert(controlOff());
    const auto pwmDuty=lroundf(r.controller.axes[0].speedOutputPercent*1023.f/100.f);
    if(DRIVE_INTERFACE==DRIVE_BTS7960 && pwmDuty==0) assert(controlOff());
    if(r.controller.axes[0].direction==-1 && pwmDuty>0) {reversed=at;break;}
  }
  assert(stopped && reversed>=stopped+300 && !controlOff());h.allStop();
  run(r,h,7000);r.controller.configurationLocked=true;h.applyOutputs();assert(nativeStopped());
  // Every failed LEDC acquisition restores BOOT HIGH and idle direction pins.
  for(int failure=0;failure<(DRIVE_INTERFACE==DRIVE_YAESU ? 1 : 2);++failure) {
    reset();fakeHardware.failAttachAt=failure;MotionTestRuntime broken;Hardware hw(broken);hw.beginDirections();
    assert(broken.controller.driveFault && broken.controller.fault=="crowpanel_drive_failed" && nativeStopped());
    assert(broken.controller.jog(0,1,false,0)==Result::Invalid);noInvalidGpio();
  }
  // Open-drain never drives against the BOOT button. A held LOW at startup
  // blocks commands; physical motor power must be disconnected in the ROM loader.
  reset();fakeHardware.inputOverrides[0]=LOW;MotionTestRuntime held;Hardware hh(held);hh.beginDirections();
  assert(held.controller.driveFault && fakeHardware.attachCalls==0 && nativeStopped());noInvalidGpio();
  if(DRIVE_INTERFACE==DRIVE_BTS7960) {
    reset();MotionTestRuntime pressed;Hardware ph(pressed);ph.beginDirections();run(pressed,ph);
    fakeHardware.inputOverrides[0]=LOW;ph.allStop();
    assert(pressed.controller.driveFault && nativeStopped());
    // The button can force EN physically active, but both PWM channels remain zero.
    assert(digitalRead(0)==LOW);
  } else {
    // Electronic 25/100% speed maps to inverse BOOT duty; disabled speed is zero.
    reset();CrowPanelDrive drive;assert(drive.begin(HardwareConfig{}));
    assert(drive.apply(1,256,true) && duties()[0]==767);
    assert(drive.apply(1,1023,true) && duties()[0]==0);
    assert(drive.apply(1,512,false) && duties()[0]==1023);
    assert(drive.stop() && nativeStopped());
    bool openDrainAfterAttach=false;
    for(const auto &e:fakeHardware.events) if(e.operation=="direction" && e.pin==0 && e.value==GPIO_MODE_INPUT_OUTPUT_OD) openDrainAfterAttach=true;
    assert(openDrainAfterAttach);noInvalidGpio();
  }
  Preferences::database.clear();Storage storage;Preferences::database["forkcfg"]["model"]=uint8_t(0);
  MotionTestRuntime loaded;storage.loadForkSettings(loaded);assert(loaded.controller.model==Model::G800DXA);
  for(bool hud:{false,true}) {
    reset();MotionTestRuntime isolated;
    if(hud) isolated.hud.config.role=DeviceRole::Hud;
    else {auto c=isolated.controller.hardware;c.simulation=true;isolated.controller.configureHardware(c);}
    Hardware ih(isolated);ih.beginDirections();ih.beginAdc();ih.applyOutputs();ih.allStop();ih.filteredMv(0);
    assert(fakeHardware.events.empty() && fakeHardware.readsMv==0 && fakeHardware.readsRaw==0);
  }
  std::cout<<"PASS: CrowPanel GPIO0 driver="<<DRIVE_INTERFACE<<" inverted="<<BTS7960_PWM_INVERTED
    <<" ADC6, open-drain BOOT, PWM/EN polarity, STOP/reverse, attach/held-BOOT faults, model, storage, HUD and simulation\n";
}
