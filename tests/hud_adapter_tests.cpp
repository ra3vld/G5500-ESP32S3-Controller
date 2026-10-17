#include "storage.hpp"
#include "hardware.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
#if SELECTED_BOARD == BOARD_CROWPANEL_21
namespace forkrot::crowpanel {
bool beginBus() { return true; }
} // namespace forkrot::crowpanel
#endif
int main() {
  Preferences::database.clear();
  Storage storage;
  Runtime normal;
  normal.protocol.rotorName="Original";
  normal.controller.model = Model::G800DXA;
  normal.controller.speeds[0].speedPercent = 65;
  storage.saveForkSettings(normal);
  normal.controller.presets[0] = {"Parking", {170, 0, true, false}};
  storage.saveProfilePresets(normal);
  const auto originalSettings = Preferences::database["forkcfg"];
  const auto originalPresets = Preferences::database["fork800"];
  HudConfig config;
  config.role = DeviceRole::Hud;
  config.uart = true;
  config.selected = 123;
  assert(storage.saveHudConfig(config));
  Runtime hud;
  storage.loadHud(hud);
  assert(hud.hud.isHud() && hud.controller.remote && hud.hud.config.uart && hud.hud.config.selected == 123);
  storage.loadForkSettings(hud);assert(hud.protocol.rotorName=="Original");
  hud.hud.begin(10, 20);
  // Exact low-level guard: no direction, PWM or ADC API is reached on HUD boot or later.
  fakeHardware = FakeHardware{};
  Hardware hw(hud);
  hw.beginDirections();
  hw.beginAdc();
  hw.applyOutputs();
  hw.controlRotator();
  hw.allStop();
  hw.moveDir('R');
  hud.controller.axes[0].current = 100;
  hud.controller.axes[0].direction = 1;
  hud.controller.axes[0].wanted = 1;
  hud.controller.tick(100);
  hud.controller.tick(10100);
  assert(hud.controller.axes[0].current == 100);
  assert(hw.adcAvgMv(board::PIN_AZ_ADC) == 0 && hw.filteredMv(0) == 0);
  int raw = 99, mv = 99;
  hw.adcDiagRead(board::PIN_EL_ADC, raw, mv);
  assert(!raw && !mv && !fakeHardware.readsMv && !fakeHardware.readsRaw);
  for (const auto &e : fakeHardware.events)
    for (const int pin : {board::PIN_LEFT, board::PIN_RIGHT, board::PIN_UP, board::PIN_DOWN, board::PIN_SPEED,
                          board::PIN_EL_SPEED, board::PIN_AZ_ADC, board::PIN_EL_ADC})
      if (pin >= 0)
        assert(e.pin != pin);
  Calibration cal(hud.controller);
  WifiCredentials wifi;
  storage.loadPrefs(hud, cal, wifi);
  storage.saveCal(hud, cal);
  storage.saveProfilePresets(hud);
  hud.controller.model = Model::G5500;
  hud.controller.speedPercent = 99;
  hud.loggerSettings.n1mmPort = 15000;
  hud.protocol.rotorName = "HUD-N1MM";
  storage.saveForkSettings(hud);
  assert(Preferences::database["forkcfg"].at("model") == originalSettings.at("model"));
#if DRIVE_INTERFACE == DRIVE_YAESU
  assert(Preferences::database["forkcfg"].at("speed") == originalSettings.at("speed"));
#endif
  assert(Preferences::database["fork800"] == originalPresets);
  assert(Preferences::database["forkcfg"].at("name") == originalSettings.at("name"));
  Runtime reboot;
  storage.loadHud(reboot);
  storage.loadForkSettings(reboot);
  assert(reboot.protocol.rotorName == "HUD-N1MM");
  // Catalogue size is data-driven and stable across saves and boots.
  for (unsigned i = 0; i < 80; ++i)
    assert(hud.hud.add(100 + i, {0x0100007f + i, uint16_t(4534 + i), false}, "Station"));
  assert(storage.saveHudPeers(hud.hud));
  Runtime restored;
  storage.loadHud(restored);
  assert(restored.hud.count() == 80);
  auto *p = restored.hud.peers();
  for (unsigned i = 0; i < 80; ++i) {
    assert(p && p->id == 100 + i);
    p = p->next.get();
  }
  Preferences::failedBytesKey = "config";
  auto changed = config;
  changed.selected = 999;
  assert(!storage.saveHudConfig(changed));
  Preferences::failedBytesKey = "peers";
  assert(hud.hud.add(999, {0x0100007f, 4534, false}, "Extra"));
  assert(!storage.saveHudPeers(hud.hud));
  Preferences::failedBytesKey.clear();
  // Bad role/config storage cannot accidentally enable motor pins.
  auto &bytes = std::get<Preferences::Bytes>(Preferences::database["hudcfg"]["config"]);
  bytes[0] ^= 1;
  Runtime corrupt;
  storage.loadHud(corrupt);
  assert(corrupt.hud.isHud() && corrupt.hud.catalogError && corrupt.controller.remote);
  Preferences::database.clear();
  Runtime defaults;
  storage.loadHud(defaults);
  assert(!defaults.hud.isHud() && !defaults.controller.remote);
  std::cout << "HUD adapters: no GPIO/PWM/ADC, calibration/preset preservation, 80-entry NVS roundtrip and "
               "corrupt-role fail-closed PASS\n";
}
