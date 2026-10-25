#include "motion_fixture.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include "hardware_form.hpp"
#include <cassert>
#include <cstdio>
using namespace forkrot;
namespace forkrot::crowpanel { bool beginBus() { return true; } }

int main() {
  for(unsigned drive=0;drive<=DRIVE_DIR_PWM;++drive) {
    HardwareConfig config;config.simulation=true;config.bluetooth=false;config.drive=drive;
    // Saved physical wiring may be unavailable, occupied, or share an I2C address.
    for(const auto &d:pinDefinitions)
      if(d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl)
        config.pins.*d.member=1;
    config.pins.azEnable=config.pins.elEnable=-1; // Not present in the v7 NVS record.
    config.sensors={2,54,0,2,54,0};
    assert(!board::validateHardware(config));
    auto physical=config;physical.simulation=false;
    assert(board::validateHardware(physical));
    auto broken=config;broken.pins.azAdc=49;
    assert(board::validateHardware(broken)); // Malformed records still fail closed.
    broken=config;broken.pins.uartRx=1;broken.pins.uartTx=-1;
    assert(board::validateHardware(broken)); // Simulation still validates active devices.

    auto released=config;released.releaseSimulationPins();
    for(const auto &d:pinDefinitions)
      if(d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl)
        assert(released.pins.*d.member==-1);
    HardwareForm form;
    assert(parseHardwareForm("token=1&generation=1&simulation=1&pin.azAdc=1",form,true,config));
    assert(form.config==released);
    MotionTestRuntime direct;direct.controller.configureHardware(config);
    assert(direct.controller.hardware==released);
    auto reused=config;const int button=config.buttonGpio>=0?config.buttonGpio:0;
    reused.pins.azPositive=button;reused.buttonGpio=button;
    reused.releaseSimulationPins();
    assert(reused.pins.azPositive==-1 && !board::validateHardware(reused));

    Preferences::database.clear();
    HardwareRecord record;record.hasConfirmed=true;record.good=record.candidate=config;
    const auto bytes=encodeHardwareRecord<204>(record);
    Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
    Storage storage;MotionTestRuntime runtime;runtime.hud.config.role=DeviceRole::Controller;storage.loadHardware(runtime);
    assert(!runtime.recovery && runtime.controller.hardware==released);
    assert(runtime.hardwareRecord.good==released && runtime.hardwareRecord.candidate==released);
    HardwareRecord stored;
    const auto &saved=std::get<Preferences::Bytes>(Preferences::database["hardware"]["config1"]);
    HardwareBytes current{};std::copy(saved.begin(),saved.end(),current.begin());
    assert(decodeHardware(current,stored) && stored.good==released);
    assert(storage.finishHardwareBoot(runtime));
    assert(!storage.stageHardware(runtime,physical));
    assert(storage.stageHardware(runtime,config));
    assert(runtime.hardwareRecord.candidate==released);
    MotionTestRuntime trial;trial.hud.config.role=DeviceRole::Controller;storage.loadHardware(trial);
    assert(!trial.hardwareTrial && !trial.recovery && storage.finishHardwareBoot(trial));
    MotionTestRuntime confirmed;confirmed.hud.config.role=DeviceRole::Controller;storage.loadHardware(confirmed);
    assert(!confirmed.recovery && confirmed.controller.hardware==released);
    Hardware hw(confirmed);fakeHardware={};
    hw.beginDirections();hw.beginAdc();hw.applyOutputs();hw.allStop();
    assert(fakeHardware.events.empty() && !fakeHardware.readsRaw && !fakeHardware.readsMv);
  }
  std::printf("PASS: board %d simulation releases drive/sensor GPIO in runtime, API and migrated NVS\n",SELECTED_BOARD);
}
