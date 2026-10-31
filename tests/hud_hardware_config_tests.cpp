#include "motion_fixture.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include <cassert>
#include <cstdio>
using namespace forkrot;
namespace forkrot::crowpanel { bool beginBus() { return true; } }

int main() {
  for(bool savedWiring:{false,true}) {
    Preferences::database.clear();fakeHardware={};
    HardwareConfig config;config.simulation=false;config.bluetooth=false;
    for(const auto &d:pinDefinitions)
      if(d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl)
        config.pins.*d.member=savedWiring?1:-1;
    if(savedWiring) config.sensors={2,1,9000,2,1,9000}; // Irrelevant physical sensor configuration.
    assert(!board::validateHardware(config,false,true));
    for(const auto &d:pinDefinitions)
      if(d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl)
        assert(!board::pinActive(config,d,true));
    auto bad=config;bad.pins.uartRx=1;bad.pins.uartTx=-1;
    assert(board::validateHardware(bad,false,true));
    bad=config;bad.local[0]=LOCAL_INPUT_BUTTONS;
    bad.pins.localAzNegative=0;bad.pins.localAzPositive=-1;
    assert(board::validateHardware(bad,false,true));

    Storage storage;HudConfig hud;hud.role=DeviceRole::Hud;hud.uart=false;hud.simulationEnabled=false;
    assert(storage.saveHudConfig(hud));
    HardwareRecord record;record.hasConfirmed=true;record.good=record.candidate=config;
    const auto bytes=encodeHardware(record);
    Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
    MotionTestRuntime boot;storage.loadHud(boot,false);storage.loadHardware(boot);
    assert(!boot.recovery && boot.hud.isHud() && boot.configuredAsHud());
    assert(!boot.hud.config.simulationEnabled && !boot.controller.hardware.simulation);
    assert(!boot.hud.available() && boot.recoveryHistory.failures==0);
    Hardware hardware(boot);fakeHardware={};
    hardware.beginDirections();hardware.beginAdc();hardware.beginInputs();hardware.applyOutputs();hardware.allStop();
    assert(!boot.controller.driveFault && !fakeHardware.attachCalls && !fakeHardware.readsRaw && !fakeHardware.readsMv);
    for(const auto &event:fakeHardware.events)assert(!savedWiring || event.pin!=1);
    assert(storage.finishHardwareBoot(boot) && storage.stageHardware(boot,config));
    MotionTestRuntime trial;storage.loadHud(trial,false);storage.loadHardware(trial);
    assert(!trial.hardwareTrial && !trial.recovery && storage.finishHardwareBoot(trial));
    assert(storage.enterRecovery(trial,RecoveryReason::Requested));
    MotionTestRuntime recovery;storage.loadHud(recovery,false);storage.loadHardware(recovery);
    assert(recovery.recovery && recovery.hardwareHudRole && recovery.hardwareRecord.hasConfirmed);
    assert(recovery.hardwareRecord.good==config);
    recovery.hud.config.role=DeviceRole::Controller; // Firmware's temporary recovery role.
    assert(recovery.configuredAsHud() && storage.stageHardware(recovery,config));

    // Returning to Controller must restore physical validation before outputs can start.
    if(savedWiring) {
      hud.role=DeviceRole::Controller;assert(storage.saveHudConfig(hud));
      MotionTestRuntime controller;storage.loadHud(controller,false);storage.loadHardware(controller);
      assert(controller.recovery && controller.recoveryReason==RecoveryReason::Invalid);
      assert(controller.hardwareRecord.hasConfirmed && controller.hardwareRecord.good==config);
      assert(controller.hardwareRecord.generation==recovery.hardwareRecord.generation);
      Hardware physical(controller);fakeHardware={};physical.beginDirections();physical.beginAdc();
      assert(fakeHardware.events.empty() && !fakeHardware.readsRaw && !fakeHardware.readsMv);
      hud.role=DeviceRole::Hud;assert(storage.saveHudConfig(hud));
      MotionTestRuntime restored;storage.loadHud(restored,false);storage.loadHardware(restored);
      assert(restored.recovery && restored.hardwareRecord.hasConfirmed);
      assert(storage.stageHardware(restored,config));
      MotionTestRuntime repaired;storage.loadHud(repaired,false);storage.loadHardware(repaired);
      assert(!repaired.hardwareTrial && !repaired.recovery && storage.finishHardwareBoot(repaired));
      storage.loadHud(restored,false);storage.loadHardware(restored);
      assert(!restored.recovery && restored.hardwareRecord.hasConfirmed && restored.controller.hardware==config);
    }
  }
  // The catalogue keeps its record size and reads both legacy transports.
  Preferences::database.clear();Storage storage;
  MotionTestRuntime original;original.hud.config.role=DeviceRole::Hud;original.hud.begin(77,78);
  assert(storage.saveHudConfig(original.hud.config));
  assert(original.hud.add(0xc0e5525c3314ULL,HudEndpoint::mqttNode(0xc0e5525c3314ULL),"MQTT"));
  assert(original.hud.add(0x123400000000ULL,HudEndpoint::mqttNode(0x123400000000ULL),"Zero low ID"));
  original.hud.peer(0xc0e5525c3314ULL)->background=8;
  assert(storage.saveHudPeers(original.hud));
  MotionTestRuntime loaded;loaded.hud.begin(77,79);storage.loadHud(loaded,true);
  assert(!loaded.hud.catalogError && loaded.hud.count()==2);
  assert(loaded.hud.peer(0xc0e5525c3314ULL)->endpoint.mqtt && loaded.hud.peer(0xc0e5525c3314ULL)->background==8);
  assert(loaded.hud.peer(0x123400000000ULL)->endpoint.mqttId()==0x123400000000ULL);
  for(auto *p=original.hud.peers();p;p=p->next.get())p->endpoint={0xeb03a8c0,4534,false};
  assert(storage.saveHudPeers(original.hud));
  auto &bytes=std::get<Preferences::Bytes>(Preferences::database["hudcfg"]["peers"]);
  for(uint32_t version:{2u,1u}) {
    std::memcpy(bytes.data(),&version,4);const auto crc=hudCrc(bytes.data(),bytes.size()-4);
    std::memcpy(bytes.data()+bytes.size()-4,&crc,4);
    MotionTestRuntime legacy;legacy.hud.begin(77,80);storage.loadHud(legacy,true);
    assert(!legacy.hud.catalogError && legacy.hud.count()==2 && !legacy.hud.peers()->endpoint.mqtt);
    assert(legacy.hud.peers()->background==(version==1?0:8));
  }
  std::printf("PASS: board %d HUD boot/offline without simulation or local drive, recovery/role return and MQTT/legacy catalogue migration\n",SELECTED_BOARD);
}
