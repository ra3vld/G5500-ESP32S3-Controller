#include "motion_fixture.hpp"
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
  MotionTestRuntime normal;
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
  config.simulationEnabled = true;
  config.uart = true;
  config.selected = 123;
  config.link.baud=9600; config.link.intervalMs=1000;
  config.link.readyHigh=false;
  assert(storage.saveHudConfig(config));
  MotionTestRuntime hud;
  storage.loadHud(hud);
  assert(hud.hud.isHud() && hud.controller.remote && hud.hud.config.uart && hud.hud.config.selected == 123);
  assert(hud.hud.config.link.baud==9600 && hud.hud.config.link.intervalMs==1000);
  assert(!hud.hud.config.link.readyHigh && hud.hud.config.simulationEnabled);
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
  MotionTestRuntime reboot;
  storage.loadHud(reboot);
  storage.loadForkSettings(reboot);
  assert(reboot.protocol.rotorName == "HUD-N1MM");
  // Catalogue size is data-driven and stable across saves and boots.
  for (unsigned i = 0; i < 80; ++i) {
    auto *peer=hud.hud.add(100 + i, {0x0100007f + i, uint16_t(4534 + i), false}, "Station");
    assert(peer);peer->background=i%hudBackgroundCount;
  }
  assert(storage.saveHudPeers(hud.hud));
  MotionTestRuntime restored;
  storage.loadHud(restored);
  assert(restored.hud.count() == 80);
  auto *p = restored.hud.peers();
  for (unsigned i = 0; i < 80; ++i) {
    assert(p && p->id == 100 + i);
    assert(p->background == i%hudBackgroundCount);
    p = p->next.get();
  }
  {
    const auto saved=std::get<Preferences::Bytes>(Preferences::database["hudcfg"]["peers"]);
    const auto load=[&](Preferences::Bytes bytes) {
      const auto crc=hudCrc(bytes.data(),bytes.size()-4);
      std::memcpy(bytes.data()+bytes.size()-4,&crc,4);
      Preferences::database["hudcfg"]["peers"]=bytes;
    };
    auto legacy=saved;legacy[0]=1;load(legacy);
    MotionTestRuntime migrated;storage.loadHud(migrated);
    assert(!migrated.hud.catalogError && migrated.hud.count()==80);
    for(auto *peer=migrated.hud.peers();peer;peer=peer->next.get())assert(!peer->background);
    assert(storage.saveHudPeers(migrated.hud));
    MotionTestRuntime roundtrip;storage.loadHud(roundtrip);
    assert(!roundtrip.hud.catalogError && roundtrip.hud.count()==80);
    auto bad=saved;bad[4+15]=255;load(bad);
    MotionTestRuntime invalid;storage.loadHud(invalid);
    assert(invalid.hud.catalogError && !invalid.hud.peer(100));
    bad=saved;bad[0]=3;load(bad);
    MotionTestRuntime future;storage.loadHud(future);
    assert(future.hud.catalogError && !future.hud.count());
    bad=saved;bad.back()^=1;
    Preferences::database["hudcfg"]["peers"]=bad;
    MotionTestRuntime corruptPeers;storage.loadHud(corruptPeers);
    assert(corruptPeers.hud.catalogError && !corruptPeers.hud.count());
    Preferences::database["hudcfg"]["peers"]=saved;
  }
  // Only real peers occupy NVS records; a saved virtual selection is recreated on boot.
  assert(hud.hud.select(HUD_SIMULATION_ID));
  assert(storage.saveHudConfig(hud.hud.config) && storage.saveHudPeers(hud.hud));
  MotionTestRuntime simulated;
  storage.loadHud(simulated);
  assert(simulated.hud.count() == 80 && simulated.hud.config.selected == HUD_SIMULATION_ID);
  simulated.hud.begin(10, 21);
  assert(simulated.hud.count() == 81 && simulated.hud.available());
  assert(simulated.controller.axes[0].current == 0 && simulated.controller.axes[1].current == 0);
  fakeHardware = FakeHardware{};
  Hardware virtualHardware(simulated);
  virtualHardware.beginDirections(); virtualHardware.beginAdc(); virtualHardware.applyOutputs();
  assert(simulated.controller.target({90, 45, true, true}, false, 100) == Result::Deferred);
  for (unsigned now = 100; now <= 2100; now += 20) {
    simulated.hud.service(now); virtualHardware.controlRotator();
  }
  assert(simulated.controller.axes[0].current > 0 && simulated.controller.axes[1].current > 0);
  assert(!fakeHardware.readsRaw && !fakeHardware.readsMv && fakeHardware.events.empty());
  assert(Preferences::database["fork800"] == originalPresets);
  {
    auto saved = std::get<Preferences::Bytes>(Preferences::database["hudcfg"]["config"]);
    auto legacy = saved;
    assert(legacy.size() == 36);
    legacy[0] = 2; legacy[31] = 0;
    auto crc = hudCrc(legacy.data(), legacy.size()-4);
    std::memcpy(legacy.data()+legacy.size()-4, &crc, 4);
    Preferences::database["hudcfg"]["config"] = legacy;
    MotionTestRuntime v2; storage.loadHud(v2);
    assert(v2.hud.isHud() && !v2.hud.catalogError && !v2.hud.config.simulationEnabled);
    assert(!v2.hud.config.selected && v2.hud.config.link.baud == 9600);
    auto invalid = saved; invalid[31] = 2;
    crc = hudCrc(invalid.data(), invalid.size()-4);
    std::memcpy(invalid.data()+invalid.size()-4, &crc, 4);
    Preferences::database["hudcfg"]["config"] = invalid;
    MotionTestRuntime badFlag;
    badFlag.hud.config.simulationEnabled = true; badFlag.hud.config.selected = HUD_SIMULATION_ID;
    storage.loadHud(badFlag);
    assert(badFlag.hud.catalogError && !badFlag.hud.config.simulationEnabled);
    assert(!badFlag.hud.config.selected);
    Preferences::database["hudcfg"]["config"] = saved;
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
  MotionTestRuntime corrupt;
  storage.loadHud(corrupt);
  assert(corrupt.hud.isHud() && corrupt.hud.catalogError && corrupt.controller.remote);
  Preferences::database.clear();
  // Existing version-1 records retain role, selection and the cable defaults.
  uint32_t oldConfig[6]={1,0x00010001,4534,123,0,0};
  oldConfig[5]=hudCrc(reinterpret_cast<const uint8_t *>(oldConfig),sizeof oldConfig-4);
  const auto *oldBytes=reinterpret_cast<const uint8_t *>(oldConfig);
  Preferences::database["hudcfg"]["config"]=Preferences::Bytes(oldBytes,oldBytes+sizeof oldConfig);
  MotionTestRuntime migrated; storage.loadHud(migrated);
  assert(migrated.hud.isHud() && migrated.hud.config.uart && migrated.hud.config.selected==123 && !migrated.hud.config.simulationEnabled);
  assert(!migrated.hud.catalogError && migrated.hud.config.link.baud==115200 && migrated.hud.config.link.intervalMs==100);
  Preferences::database.clear();
  MotionTestRuntime defaults;
  storage.loadHud(defaults);
  assert(!defaults.hud.isHud() && !defaults.controller.remote && !defaults.hud.config.simulationEnabled);
  std::cout << "HUD adapters: no GPIO/PWM/ADC, calibration/preset preservation, 80-entry NVS roundtrip and "
               "corrupt-role fail-closed PASS\n";
}
