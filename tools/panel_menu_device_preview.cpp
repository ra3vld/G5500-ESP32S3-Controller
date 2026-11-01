// Exercise the production display menu, persistent storage and HUD peer views.
#include "device_api_fixture.hpp"
#include "display.hpp"
#include <cstring>
namespace forkrot {
PanelApResult Network::panelApAllowed(bool) const {return PanelApResult::Idle;}
bool Network::requestPanelAp(bool) {return true;}
}
template<class Tag,auto Member> struct MenuAccess {friend auto member(Tag){return Member;}};
#define ACCESS(name) struct name##Tag {friend auto member(name##Tag);};template struct MenuAccess<name##Tag,&Display::name>;
ACCESS(serviceMenu) ACCESS(updateMenuView) ACCESS(applyBrightness) ACCESS(menuOpen_)
#undef ACCESS
int main() {
  Fixture f;assert(f.storage.loadDisplay(f.runtime));
  f.runtime.controller.hardware.pins.tftBacklight=7;
  Display display(f.runtime,f.actions,f.calibration,f.hardware,f.diagnostics,f.network,f.storage);
  (display.*member(applyBrightnessTag{}))();
  auto action=[&](panel::TouchAction action){(display.*member(serviceMenuTag{}))(action,millis());};
  auto view=[&](){PanelMenuView v;(display.*member(updateMenuViewTag{}))(v,millis());return v;};
  action(panel::TouchAction::MenuOpen);assert(view().open&&view().local&&view().brightnessAvailable);
  action(panel::TouchAction::MenuDim);action(panel::TouchAction::MenuDim);
  assert(f.runtime.displaySettings.brightness==80&&f.runtime.displaySettings.revision==0);
  Preferences::failedBytesKey="pose1";
  action(panel::TouchAction::MenuBack);assert(view().open&&std::strstr(view().notice,"сохранить"));
  assert(f.runtime.displaySettings.revision==0);
  Preferences::failedBytesKey.clear();action(panel::TouchAction::MenuBack);
  assert(!view().open&&f.runtime.displaySettings.revision==1);
  Runtime restored;assert(f.storage.loadDisplay(restored)&&restored.displaySettings.brightness==80);
  action(panel::TouchAction::MenuOpen);action(panel::TouchAction::MenuDim);
  f.runtime.controller.axes[0].direction=1;f.runtime.controller.axes[0].wanted=1;
  action(panel::TouchAction::MenuBack);assert(view().open&&f.runtime.displaySettings.revision==1);
  f.runtime.controller.stopNow(-1,millis());action(panel::TouchAction::MenuBack);
  assert(!view().open&&f.runtime.displaySettings.brightness==70);
  f.runtime.hud.config.role=DeviceRole::Hud;
  auto *first=f.runtime.hud.add(12,{123,4534,false},"First",true);
  auto *second=f.runtime.hud.add(34,HudEndpoint::mqttNode(34),"Second",true);
  assert(first&&second);f.runtime.hud.select(12);
  const auto selected=f.runtime.hud.config.selected;
  const auto epoch=f.runtime.hud.selectionEpoch;
  second->received=true;second->receivedAt=10000;second->remoteTick=20000;f.runtime.hud.now=10000;
  action(panel::TouchAction::MenuOpen);action(panel::TouchAction::MenuPrevious);
  auto remote=view();assert(remote.open&&!remote.local&&std::strstr(remote.info,"MQTT"));
  assert(std::strstr(remote.info,"00:00:20"));
  f.runtime.hud.now=14000;remote=view();assert(std::strstr(remote.info,"00:00:20")&&std::strstr(remote.info,"Последние"));
  action(panel::TouchAction::MenuDim);assert(f.runtime.displaySettings.brightness==70);
  action(panel::TouchAction::MenuConnections);assert(std::strstr(view().info,"HUD/"));
  action(panel::TouchAction::MenuNext);assert(view().local);
  action(panel::TouchAction::MenuNext);assert(!view().local&&std::strstr(view().info,"Нет данных"));
  assert(f.runtime.hud.config.selected==selected&&f.runtime.hud.selectionEpoch==epoch);
  {
    Fixture failure;failure.runtime.controller.hardware.pins.tftBacklight=7;fakeHardware.attachSucceeds=false;
    Display fixed(failure.runtime,failure.actions,failure.calibration,failure.hardware,failure.diagnostics,failure.network,failure.storage);
    (fixed.*member(applyBrightnessTag{}))();
    (fixed.*member(serviceMenuTag{}))(panel::TouchAction::MenuOpen,millis());
    PanelMenuView v;(fixed.*member(updateMenuViewTag{}))(v,millis());
    assert(!v.brightnessAvailable&&fakeHardware.levels[7]==HIGH);
  }
  std::puts("PASS: production menu brightness applies/persists, failed saves stay open, motion guards writes, HUD views preserve control selection and stale uptime");
}
