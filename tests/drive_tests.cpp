#include "hardware.hpp"
#include "storage.hpp"
#include "input.hpp"
#include <iostream>
#include <stdexcept>
#include <map>
using namespace forkrot;
using namespace forkrot::board;
unsigned checks = 0;
// Interpret measured GPIO duty after the inverting optical PWM channel.
uint32_t moduleDuty(uint32_t gpioDuty) {
#if DRIVE_INTERFACE == DRIVE_BTS7960 && BTS7960_PWM_INVERTED
  return 1023u - gpioDuty;
#else
  return gpioDuty;
#endif
}
constexpr int pwmIdleLevel = DRIVE_INTERFACE == DRIVE_BTS7960 && BTS7960_PWM_INVERTED ? HIGH : LOW;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x); } while (0)
Controller ready() { Controller c; c.feedback(20,30,true,true); c.setMode(Mode::Auto,0); return c; }
void onePass() {
  auto c = ready();
  CHECK(c.target({90,80,true,true},true,0)==Result::Ok); c.tick(0);
  c.feedback(96,40,true,true); c.tick(20); // crosses completely past stop zone
  CHECK(c.axes[0].completed && !c.axes[0].direction);
  for (int noise : {91,99,94,100,92}) {
    c.feedback(float(noise),45,true,true);
    CHECK(c.target({90,80,true,true},true,40)==Result::Ok); c.tick(40);
    CHECK(c.axes[0].completed && !c.axes[0].direction);
  }
  c.feedback(100,45,true,true); c.stop(0);
  c.target({90,80,true,true},true,50); c.tick(50);
  CHECK(!c.axes[0].direction && !c.axes[0].wanted && !c.axes[0].tracking && c.axes[0].acceptedTarget==90);
  CHECK(c.axes[1].tracking);
  CHECK(c.target({95,90,true,true},true,60)==Result::Ok); c.tick(60);
  CHECK(c.axes[0].target==90 && c.axes[0].acceptedTarget==90);
  CHECK(c.axes[1].target==90);
  CHECK(c.target({96,90,true,true},true,80)==Result::Ok);
  CHECK(c.axes[0].acceptedTarget==96); // accumulated against 90, not ignored 95
  c.feedback(110,45,true,true); c.tick(80);
  CHECK(c.target({96,90,true,true},false,100)==Result::Ok); c.tick(400);
  CHECK(c.axes[0].direction==-1); // explicit local command may re-arm
  c.feedback(91,45,true,true); c.tick(420);
  CHECK(c.axes[0].completed && !c.axes[0].direction);
  c.feedback(98,45,true,true); c.tick(440); CHECK(!c.axes[0].direction);
  c.target({102,90,true,true},true,460); c.tick(760);
  CHECK(c.axes[0].acceptedTarget==102); CHECK(c.axes[0].completed); // short rotation already inside 5
  c.stop(); CHECK(c.mode==Mode::Auto);
  c.target({180,120,true,true},false,800); c.tick(800);
  CHECK(c.axes[0].tracking && c.axes[1].tracking);
  c.setMaximum(360); c.tick(820);
  CHECK(!c.axes[0].direction && c.axes[1].tracking && c.axes[1].direction==1);
  c.setMaximum(360); c.tick(840); CHECK(c.axes[1].direction==1);
  c.feedback(100,50,true,false); c.tick(860);
  CHECK(!c.axes[1].tracking && !c.axes[1].direction);
}
void limitReversal() {
  auto c=ready(); c.jog(0,1,false,0); c.tick(0);
  c.feedback(450,30,true,true);
  c.target({100,0,true,false},true,10); c.tick(10);
  CHECK(c.axes[0].direction==0 && c.axes[0].wanted==-1 && c.axes[0].tracking);
  c.tick(309); CHECK(c.axes[0].direction==0);
  c.tick(310); CHECK(c.axes[0].direction==-1);
  c.stop(); c.feedback(0,30,true,true); c.jog(0,-1,false,600); c.tick(600);
  c.target({100,0,true,false},true,610); c.tick(610);
  CHECK(c.axes[0].wanted==1); c.tick(910); CHECK(c.axes[0].direction==1);
}

void filters() {
  PositionFilter f;
  uint32_t tick=0;
  for (int mv : {1000,1010,990,1005,995}) { f.sample(mv,tick); tick+=20; }
  const uint32_t now=10000;
  CHECK(f.sample(3000,now)==1005); CHECK(f.sample(0,now+1)==1005);
  CHECK(f.sample(1000,now+20)==1000);
  PositionFilter wrap; wrap.sample(10,0xfffffff0u);
  CHECK(!wrap.due(3)); CHECK(wrap.due(4));
  fakeHardware = {}; Runtime r; Hardware h(r);
  fakeHardware.millivolts[PIN_AZ_ADC]=1200;
  CHECK(h.filteredMv(0)==1200); CHECK(fakeHardware.readsMv==25);
  fakeHardware.millivolts[PIN_AZ_ADC]=3000;
  fakeHardware.now=19; CHECK(h.filteredMv(0)==1200); CHECK(fakeHardware.readsMv==25);
  int raw=0,mv=0; h.adcDiagRead(PIN_AZ_ADC,raw,mv); CHECK(raw==3000 && mv==3000); CHECK(fakeHardware.readsRaw==25 && fakeHardware.readsMv==50);
  fakeHardware.now=20; h.filteredMv(0);
  for (unsigned t : {40u,60u,80u}) { fakeHardware.now=t; fakeHardware.millivolts[PIN_AZ_ADC]=1200; h.filteredMv(0); }
  fakeHardware.now=100; fakeHardware.millivolts[PIN_AZ_ADC]=3000; CHECK(h.filteredMv(0)==1200);
}
void settings() {
  auto c=ready(); Protocol p(c); Settings cfg;
  CHECK(api(c,p,cfg,"/api/settings",{{"azSpeedPercent","75"},{"speedPercent","75"},{"elSpeedPercent","12"},{"elSlowdownDegrees","2"}},0).status==200);
  CHECK(c.speedPercent==75 && c.speeds[1].speedPercent==12);
  const auto before=stateJson(c,p,cfg,0);
  for (const Form &form : {Form{{"azSpeedPercent","60"},{"speedPercent","70"}}, Form{{"driveInterface","2"},{"model","G800DXA"}}, Form{{"elSpeedPercent","101"},{"speedPercent","90"}}}) {
    CHECK(api(c,p,cfg,"/api/settings",form,0).status==400); CHECK(stateJson(c,p,cfg,0)==before);
  }
  p.line("X4",0); CHECK(c.speedPercent==100 && c.speeds[1].speedPercent==12);
  p.line("M 1 90 180",0); CHECK(p.line("N",0).reply=="N=0/2\r\n");
  p.line("T",0); CHECK(p.line("N",0).reply=="N=1/2\r\n");
  c.tick(1000); CHECK(p.line("N",1000).reply=="N=2/2\r\n");
  p.line("S",1001); CHECK(p.line("N",1001).reply=="N=0/0\r\n");
#if DRIVE_INTERFACE != DRIVE_YAESU
  CHECK(api(c,p,cfg,"/api/settings",{{"azSpeedPercent","1"},{"elSpeedPercent","1"}},0).status==200);
  Preferences::database.clear(); Storage storage; Runtime r;
  Preferences::database["forkcfg"]["speed"] = uint32_t(77);
  Preferences::database["forkcfg"]["startspd"] = uint32_t(31);
  storage.loadForkSettings(r); CHECK(r.controller.speedPercent==100);
  r.controller.speedPercent=1; r.controller.speeds[1].speedPercent=33;
  r.controller.target({90,60,true,true},true,0); storage.saveForkSettings(r);
  CHECK(std::get<uint32_t>(Preferences::database["forkcfg"]["speed"])==77);
  Runtime loaded; storage.loadForkSettings(loaded);
  CHECK(loaded.controller.speedPercent==1 && loaded.controller.speeds[1].speedPercent==33);
  CHECK(!loaded.controller.targetKnown && !loaded.controller.pendingValid && !loaded.controller.moving());
#endif
}
void outputs() {
  fakeHardware={}; Runtime r; Hardware h(r); h.beginDirections(); h.beginInputs();
  CHECK(fakeHardware.levels[PIN_SPEED]==LOW);
  if (PIN_EL_SPEED>=0) CHECK(fakeHardware.levels[PIN_EL_SPEED]==LOW);
#if DRIVE_INTERFACE == DRIVE_BTS7960
  for (int pin : {PIN_LEFT, PIN_RIGHT, PIN_UP, PIN_DOWN}) CHECK(fakeHardware.levels[pin]==pwmIdleLevel);
#endif
  r.controller.feedback(20,30,true,true);
  r.controller.decelerationMs=1200;
#if DRIVE_INTERFACE == DRIVE_YAESU
  r.controller.model=Model::G800DXA; r.controller.speedEnabled=true;
#endif
  CHECK(r.controller.target({100,120,true,DRIVE_INTERFACE != DRIVE_YAESU},false,0)==Result::Ok);
  r.controller.tick(0); h.applyOutputs();
  std::map<int,uint32_t> duties;
  for (const auto &e : fakeHardware.events) if (e.operation=="pwm") duties[e.pin]=moduleDuty(e.value);
#if DRIVE_INTERFACE == DRIVE_BTS7960
  CHECK(duties[PIN_RIGHT]>0 && duties[PIN_LEFT]==0);
  CHECK(duties[PIN_UP]>0 && duties[PIN_DOWN]==0);
  CHECK(fakeHardware.levels[PIN_SPEED]==HIGH && fakeHardware.levels[PIN_EL_SPEED]==HIGH);
#elif DRIVE_INTERFACE == DRIVE_SEPARATE_PWM
  CHECK(duties[PIN_SPEED]>0 && duties[PIN_EL_SPEED]>0);
#endif
  r.controller.tick(2000); fakeHardware.now=2000; h.applyOutputs();
  CHECK(r.controller.axes[0].speedOutputPercent>18);
  r.controller.jog(0,-1,false,2000); r.controller.tick(2000); h.applyOutputs();
  r.controller.tick(3200); fakeHardware.now=3200; h.applyOutputs();
  CHECK(r.controller.axes[0].direction==0);
  r.controller.tick(3499); CHECK(r.controller.axes[0].direction==0);
  r.controller.tick(3500); fakeHardware.now=3500; h.applyOutputs(); CHECK(r.controller.axes[0].direction==-1);
#if DRIVE_INTERFACE == DRIVE_BTS7960
  for (const auto &e : fakeHardware.events) if (e.operation=="pwm") {
    duties[e.pin]=moduleDuty(e.value);
    CHECK(!(duties[PIN_RIGHT] && duties[PIN_LEFT])); CHECK(!(duties[PIN_UP] && duties[PIN_DOWN]));
  }
#endif
  const auto stopEvents = fakeHardware.events.size();
  h.allStop();
#if DRIVE_INTERFACE == DRIVE_BTS7960
  // Hardware ENABLE must fall before the first PWM change during STOP.
  bool azDisabled = false, elDisabled = false;
  for (size_t j=stopEvents; j<fakeHardware.events.size(); ++j) {
    const auto &e = fakeHardware.events[j];
    if (e.operation=="write" && e.pin==PIN_SPEED && e.value==LOW) azDisabled=true;
    if (e.operation=="write" && e.pin==PIN_EL_SPEED && e.value==LOW) elDisabled=true;
    if (e.operation=="pwm") CHECK(azDisabled && elDisabled);
  }
  CHECK(azDisabled && elDisabled);
#else
  (void)stopEvents;
#endif
  CHECK(!r.controller.moving());
#if DRIVE_INTERFACE == DRIVE_BTS7960
  CHECK(fakeHardware.levels[PIN_SPEED]==LOW && fakeHardware.levels[PIN_EL_SPEED]==LOW);
#endif
  for (const auto &e : fakeHardware.events) if (e.operation=="pwm") duties[e.pin]=moduleDuty(e.value);
  for (const auto &e : duties) CHECK(e.second==0);
  fakeHardware={}; Runtime broken; Hardware fail(broken); fail.beginDirections();
#if DRIVE_INTERFACE == DRIVE_YAESU
  broken.controller.model=Model::G800DXA; broken.controller.speedEnabled=true;
#endif
  fakeHardware.attachSucceeds=false;
  broken.controller.feedback(10,20,true,true); broken.controller.jog(0,1,false,0); broken.controller.tick(0); fail.applyOutputs();
  CHECK(broken.controller.driveFault && !broken.controller.moving());
  CHECK(fakeHardware.levels[PIN_LEFT]==pwmIdleLevel && fakeHardware.levels[PIN_RIGHT]==pwmIdleLevel && fakeHardware.levels[PIN_SPEED]==LOW);
  CHECK(broken.controller.jog(0,1,false,100)==Result::Invalid);
  broken.controller.setModel(Model::G5500); CHECK(broken.controller.fault=="pwm_attach_failed" && broken.controller.driveFault);
  const int count=DRIVE_INTERFACE==DRIVE_BTS7960 ? 4 : DRIVE_INTERFACE==DRIVE_SEPARATE_PWM ? 2 : 1;
  for (int failure=0; failure<count; ++failure) {
    fakeHardware={}; fakeHardware.failAttachAt=failure;
    Runtime partiallyAttached; Hardware partial(partiallyAttached); partial.beginDirections();
#if DRIVE_INTERFACE == DRIVE_YAESU
    partiallyAttached.controller.model=Model::G800DXA; partiallyAttached.controller.speedEnabled=true;
#endif
    partiallyAttached.controller.jog(0,1,false,0); partiallyAttached.controller.tick(0); partial.applyOutputs();
    CHECK(partiallyAttached.controller.driveFault && !partiallyAttached.controller.moving());
    CHECK(fakeHardware.levels[PIN_SPEED]==LOW);
#if DRIVE_INTERFACE != DRIVE_YAESU
    CHECK(fakeHardware.levels[PIN_EL_SPEED]==LOW);
#endif
    for (const auto &event : fakeHardware.events) if (event.operation=="pwm") CHECK(moduleDuty(event.value)==0);
    const auto attempts=fakeHardware.attachCalls;
    fakeHardware.attachSucceeds=true; partial.applyOutputs();
    CHECK(fakeHardware.attachCalls==attempts);
    for (const auto &event : fakeHardware.events) CHECK(event.operation!="write_unowned");
  }

}
void button() {
  fakeHardware={}; Runtime r; Hardware h(r); h.beginInputs(); Button b;
  if (PIN_CONFIRM>=0) {
    fakeHardware.levels[PIN_CONFIRM]=USER_BUTTON_ACTIVE_LOW ? LOW : HIGH;
    CHECK(!b.sample(h.buttonReleased(),0)); CHECK(!b.sample(h.buttonReleased(),100));
    fakeHardware.levels[PIN_CONFIRM]=USER_BUTTON_ACTIVE_LOW ? HIGH : LOW;
    CHECK(!b.sample(h.buttonReleased(),101)); CHECK(!b.sample(h.buttonReleased(),141));
    fakeHardware.levels[PIN_CONFIRM]=USER_BUTTON_ACTIVE_LOW ? LOW : HIGH;
    CHECK(!b.sample(h.buttonReleased(),142)); CHECK(b.sample(h.buttonReleased(),182)); CHECK(!b.sample(h.buttonReleased(),300));
  } else CHECK(fakeHardware.events.empty());
}
int main() { try { onePass(); limitReversal(); filters(); settings(); outputs(); button(); std::cout<<"PASS drive "<<DRIVE_INTERFACE<<": "<<checks<<" motion/filter/API/NVS/PWM/button checks\n"; } catch (const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; } }
