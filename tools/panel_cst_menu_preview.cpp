// Run the ESP32-2424S012C input path with real LVGL and CST register samples.
#include "device_api_fixture.hpp"
#include "display.hpp"
#include <Wire.h>
namespace forkrot {
PanelApResult Network::panelApAllowed(bool) const {return PanelApResult::Idle;}
bool Network::requestPanelAp(bool) {return true;}
void Network::serviceHud(bool) {}
void Network::formatAddresses(char *text,size_t capacity,const char *,bool,bool) const {
  std::snprintf(text,capacity,"192.0.2.1");
}
}
template<class Tag,auto Member> struct CstAccess {friend auto member(Tag){return Member;}};
#define ACCESS(name) struct name##Tag {friend auto member(name##Tag);};template struct CstAccess<name##Tag,&Display::name>;
ACCESS(display_) ACCESS(menuOpen_) ACCESS(presentedLayout_) ACCESS(commandContext)
#undef ACCESS
int main() {
  Fixture f;
  // The user's live HUD state: online, no target yet, custom AZ/EL ranges.
  auto *peer=f.runtime.hud.add(12,{123,4534,false},"g5500",true);
  assert(peer);peer->received=true;peer->receivedAt=millis();
  peer->state.minimum=-50;peer->state.maximum=190;peer->state.rangeRevision=3;
  peer->state.elRange={-45,190};
  peer->state.axes[0].current=190;peer->state.axes[1].current=-10.92f;
  peer->state.axes[0].calibrated=peer->state.axes[1].calibrated=true;
  f.runtime.hud.now=millis();assert(f.runtime.hud.select(12));
  Display display(f.runtime,f.actions,f.calibration,f.hardware,f.diagnostics,f.network,f.storage);
  display.initLocalDisplay();display.startAfterCalibrationLoad();
  auto tick=[&](bool down,unsigned x,unsigned y,unsigned elapsed=10) {
    fakeHardware.now+=elapsed;
    f.runtime.hud.now=peer->receivedAt=millis();
    Wire.registers[2]=down?1:0;Wire.registers[3]=down?0x80:0x40;
    Wire.registers[4]=x;Wire.registers[5]=0;Wire.registers[6]=y;
    display.serviceTouch();display.updateLocalDisplay();
  };
  for(unsigned i=0;i<20;++i)tick(false,0,0);
  const auto layout=display.*member(presentedLayout_Tag{});
  std::printf("CST target: %u,%u %ux%u access=%d enabled=%d; current: %u,%u %ux%u\n",layout.target.x,layout.target.y,layout.target.width,layout.target.height,layout.menuAccess,layout.enabled,layout.current.x,layout.current.y,layout.current.width,layout.current.height);
  assert(layout.menuAccess&&layout.target.width&&layout.target.height);
  // Recorded on the user's C3: a sustained hold at 116,155, in the free
  // gap immediately above the goal row. It must reach the goal hold timer.
  const unsigned x=116,y=155;
  assert(!layout.target.contains(x,y)&&layout.menuTargetHit(x,y));
  tick(true,x,y);tick(false,x,y); // Padding is not a short confirming tap.
  assert(!(display.*member(menuOpen_Tag{})));
  for(unsigned i=0;i<301;++i) {
    tick(true,x,y);assert(!(display.*member(menuOpen_Tag{})));
  }
  tick(true,x,y);
  assert(display.*member(menuOpen_Tag{}));
  tick(false,x,y);
  for(unsigned i=0;i<10;++i)tick(false,x,y);
  const auto menu=display.*member(presentedLayout_Tag{});assert(menu.menuOpen);
  // The opening contact is consumed; a fresh Back tap closes the menu.
  const auto back=menu.menuButtons[0];
  tick(true,back.x+back.width/2,back.y+back.height/2);
  tick(false,back.x+back.width/2,back.y+back.height/2);
  assert(!(display.*member(menuOpen_Tag{})));
  const auto axis=[&](){return (display.*member(commandContextTag{}))().axis;};
  for(unsigned selected:{1u,0u}) {
    for(unsigned i=0;i<10;++i)tick(false,0,0);
    auto l=display.*member(presentedLayout_Tag{});auto other=l.secondaryCurrent;
    assert(other.width);const unsigned ox=other.x+other.width/2,oy=other.y+other.height/2;
    tick(true,ox,oy);assert(axis()!=selected);
    tick(false,ox,oy);assert(axis()==selected);
    for(unsigned i=0;i<10;++i)tick(false,0,0);
    l=display.*member(presentedLayout_Tag{});other=l.secondaryCurrent;
    const unsigned sx=other.x+other.width/2,sy=other.y+other.height/2;
    tick(true,sx,sy);tick(true,sx+30,sy-20);tick(true,sx,sy);
    tick(false,sx,sy);assert(axis()==selected);
  }
  for(unsigned i=0;i<10;++i)tick(false,0,0);
  auto l=display.*member(presentedLayout_Tag{});
  const unsigned cx=l.current.x+l.current.width/2,cy=l.current.y+l.current.height/2;
  for(unsigned i=0;i<302;++i)tick(true,cx,cy);
  assert(display.*member(menuOpen_Tag{}));
  tick(false,cx,cy);assert(axis()==0);
  for(unsigned i=0;i<10;++i)tick(false,0,0);
  auto mb=(display.*member(presentedLayout_Tag{})).menuButtons[0];
  tick(true,mb.x+mb.width/2,mb.y+mb.height/2);tick(false,0,0);
  for(unsigned i=0;i<10;++i)tick(false,0,0);
  auto *second=f.runtime.hud.add(34,{124,4534,false},"Second",true);assert(second);
  second->received=true;second->receivedAt=millis();second->state=peer->state;
  const auto epoch=f.runtime.hud.selectionEpoch;
  // The address/header edge was outside the old name-only hitbox on 240x240.
  assert((display.*member(presentedLayout_Tag{})).name.contains(50,38));
  tick(true,50,38);assert(f.runtime.hud.config.selected==34);
  for(unsigned i=0;i<20;++i)tick(true,120,120);
  assert(f.runtime.hud.config.selected==34 && !f.runtime.controller.moving());
  tick(false,0,0);for(unsigned i=0;i<10;++i)tick(false,0,0);
  tick(true,50,38);assert(f.runtime.hud.config.selected==12);
  tick(false,0,0);assert(f.runtime.hud.selectionEpoch==epoch+2);
  Runtime restored;f.storage.loadHud(restored,false);assert(restored.hud.config.selected==12);
  lv_display_delete(display.*member(display_Tag{}));
  std::puts("PASS: production touch path opens settings, selects axes on release, preserves swipes and switches peers from the expanded header without repeat or movement");
}
