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
ACCESS(display_) ACCESS(menuOpen_) ACCESS(presentedLayout_)
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
  lv_display_delete(display.*member(display_Tag{}));
  std::puts("PASS: full GC9A01/CST816 path admits the recorded hold, preserves the strict 3s threshold, consumes release and closes on Back");
}
