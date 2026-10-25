#include "motion_fixture.hpp"
#include "storage.hpp"
#include "hardware.hpp"
#include "hardware_form.hpp"
#include <cassert>
#include <iostream>
#include <vector>
using namespace forkrot;
static void recoveryPolicy() {
  Storage s;Preferences::database.clear();Preferences::failedBytesKey.clear();
  MotionTestRuntime boot;s.loadHardware(boot);assert(s.finishHardwareBoot(boot));
  const auto good=boot.hardwareRecord.good;
  // A detected failure plus its restart is one event, not two.
  for(unsigned count=1;count<=RecoveryHistory::Limit;++count) {
    assert(s.enterRecovery(boot,RecoveryReason::Allocation));
    s.loadHardware(boot,true);
    assert(boot.recoveryHistory.failures==count && !boot.recoveryHistory.pending);
    assert(boot.recovery==(count==RecoveryHistory::Limit));
    assert(boot.hardwareRecord.good==good && !boot.controller.moving());
    if(!boot.recovery)assert(s.finishHardwareBoot(boot));
  }
  MotionTestRuntime latched;s.loadHardware(latched);assert(latched.recovery && latched.recoveryHistory.failures==3);
  assert(s.finishHardwareBoot(latched,RecoveryHistory::StableMs));assert(latched.recoveryHistory.failures==3);
  // Explicit retry starts a new series; ordinary short successful boots do not.
  assert(s.stageHardware(latched,good));assert(!latched.recoveryHistory.failures);
  MotionTestRuntime trial;s.loadHardware(trial);assert(trial.hardwareTrial && s.confirmHardware(trial));
  s.loadHardware(boot);assert(s.finishHardwareBoot(boot));
  assert(s.enterRecovery(boot,RecoveryReason::LowMemory));s.loadHardware(boot);
  assert(boot.recoveryHistory.failures==1 && !boot.recovery);
  assert(s.finishHardwareBoot(boot,RecoveryHistory::StableMs-1));assert(boot.recoveryHistory.failures==1);
  MotionTestRuntime powerCycle;s.loadHardware(powerCycle);assert(powerCycle.recoveryHistory.failures==1);
  assert(s.finishHardwareBoot(powerCycle,RecoveryHistory::StableMs-1));assert(powerCycle.recoveryHistory.failures==1);
  const uint32_t started=UINT32_MAX-1234,now=started+RecoveryHistory::StableMs;
  assert(s.finishHardwareBoot(powerCycle,uint32_t(now-started)));assert(!powerCycle.recoveryHistory.failures);
  const auto transactions=Preferences::opened.size();
  for(unsigned n=0;n<100;++n)assert(s.finishHardwareBoot(powerCycle,RecoveryHistory::StableMs+n));
  assert(Preferences::opened.size()==transactions); // no periodic flash wear
  // Pending history survives failure of the second NVS write.
  s.loadHardware(powerCycle);assert(powerCycle.hardwareRecord.phase==ConfigPhase::Starting);
  Preferences::failedBytesKey="config1";
  assert(!s.enterRecovery(powerCycle,RecoveryReason::Radio));Preferences::failedBytesKey.clear();
  s.loadHardware(boot,true);assert(boot.recoveryHistory.failures==1 && !boot.recovery);
  assert(s.finishHardwareBoot(boot));
  Preferences::failedBytesKey="recovery1";
  assert(!s.finishHardwareBoot(boot,RecoveryHistory::StableMs));assert(boot.recoveryHistory.failures==1);
  Preferences::failedBytesKey.clear();
  // Requested recovery stays immediate; returning from it remains explicit.
  assert(s.enterRecovery(boot,RecoveryReason::Requested));s.loadHardware(boot,true);
  assert(boot.recovery && boot.recoveryReason==RecoveryReason::Requested && boot.recoveryHistory.failures==1);
  // Legacy automatic recovery with no counter is the first fault, not a lockout.
  auto old=boot.hardwareRecord;old.phase=ConfigPhase::Recovery;old.reason=RecoveryReason::Unconfirmed;
  auto bytes=encodeHardware(old);Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
  Preferences::database["hardware"].erase("recovery1");s.loadHardware(boot);
  assert(!boot.recovery && boot.recoveryHistory.failures==1 && boot.hardwareRecord.good==good);
  assert(s.finishHardwareBoot(boot));
  auto history=encodeRecovery(boot.recoveryHistory);RecoveryHistory decoded;
  assert(decodeRecovery(history,decoded) && decoded==boot.recoveryHistory);
  for(unsigned i=0;i<history.size();++i){auto bad=history;bad[i]^=0x80;assert(!decodeRecovery(bad,decoded));}
  history[8]^=1;Preferences::database["hardware"]["recovery1"]=Preferences::Bytes(history.begin(),history.end());
  s.loadHardware(boot);assert(boot.recovery && boot.recoveryReason==RecoveryReason::Storage && boot.hardwareRecord.good==good);
  // Repeated interrupted starts also use the three-event threshold.
  Preferences::database.clear();s.loadHardware(boot);
  for(unsigned count=1;count<=3;++count) {
    MotionTestRuntime next;s.loadHardware(next);assert(next.recoveryHistory.failures==count);
    assert(next.recovery==(count==3));assert(!next.controller.moving());
  }
  Preferences::database.clear();
}
static void confirmedRecoveryPreservesRecord() {
  Storage storage;
  HardwareConfig good;good.simulation=true;good.bluetooth=false;good.buttonGpio=-1;
  good.releaseSimulationPins();assert(!board::validateHardware(good));
  auto invalid=good;invalid.buttonGpio=100;assert(board::validateHardware(invalid));
  auto save=[&](const HardwareRecord &record) {
    Preferences::database.clear();const auto bytes=encodeHardware(record);
    Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
  };
  auto stored=[&]() {
    const auto &raw=std::get<Preferences::Bytes>(Preferences::database["hardware"]["config1"]);
    HardwareBytes bytes{};assert(raw.size()==bytes.size());std::copy(raw.begin(),raw.end(),bytes.begin());
    HardwareRecord record;assert(decodeHardware(bytes,record));return record;
  };
  HardwareRecord record;record.hasConfirmed=true;record.generation=42;record.probeCounter=17;
  record.good=good;record.candidate=invalid;
  // An obsolete candidate cannot invalidate the active confirmed map.
  save(record);MotionTestRuntime normal;storage.loadHardware(normal);
  assert(!normal.recovery && normal.hardwareRecord.hasConfirmed);
  assert(normal.controller.hardware==good && stored().candidate==invalid);

  // An invalid pending edit must preserve the prior confirmation and its identity.
  record.phase=ConfigPhase::Pending;save(record);
  MotionTestRuntime recovery;storage.loadHardware(recovery);
  assert(recovery.recovery && recovery.recoveryReason==RecoveryReason::Invalid);
  assert(recovery.hardwareRecord.hasConfirmed && recovery.hardwareRecord.generation==42);
  assert(recovery.hardwareRecord.probeCounter==17 && recovery.hardwareRecord.good==good);
  assert(stored().hasConfirmed && stored().candidate==invalid);
  storage.loadHardware(recovery);assert(recovery.hardwareRecord.hasConfirmed);
  assert(storage.stageHardware(recovery,good));
  MotionTestRuntime retry;storage.loadHardware(retry);
  assert(retry.hardwareTrial && retry.hardwareRecord.hasConfirmed && storage.confirmHardware(retry));

  // A previously confirmed map may become incompatible after a role/image change.
  // Keep it for inspection, allow a corrected candidate, and reject an unsafe fallback.
  record.phase=ConfigPhase::Confirmed;record.good=invalid;record.candidate=good;save(record);
  storage.loadHardware(recovery);
  assert(recovery.recovery && recovery.hardwareRecord.hasConfirmed);
  assert(recovery.hardwareRecord.good==invalid && recovery.hardwareRecord.generation==42);
  assert(recovery.controller.hardware.pins.uartRx==-1 && recovery.controller.hardware.pins.uartTx==-1);
  assert(storage.stageHardware(recovery,good));
  MotionTestRuntime forced;storage.loadHardware(forced,true);
  assert(forced.recovery && forced.recoveryReason==RecoveryReason::Invalid);
  assert(forced.hardwareRecord.hasConfirmed && forced.controller.configurationLocked);
  assert(storage.stageHardware(forced,good));storage.loadHardware(retry);
  assert(retry.hardwareTrial && !retry.recovery && retry.hardwareRecord.hasConfirmed);
  MotionTestRuntime interrupted;storage.loadHardware(interrupted);
  assert(interrupted.recovery && interrupted.recoveryReason==RecoveryReason::Invalid);
  assert(interrupted.hardwareRecord.hasConfirmed && interrupted.controller.configurationLocked);
  assert(interrupted.hardwareRecord.good==invalid && interrupted.hardwareRecord.candidate==good);
  assert(storage.stageHardware(interrupted,good));storage.loadHardware(retry);
  assert(retry.hardwareTrial && storage.confirmHardware(retry));
  storage.loadHardware(normal);assert(!normal.recovery && normal.hardwareRecord.hasConfirmed);
  assert(normal.controller.hardware==good && !normal.controller.configurationLocked);

  // Already erased/corrupt records acquire a real confirmation only after a successful trial.
  record.hasConfirmed=false;record.phase=ConfigPhase::Recovery;record.reason=RecoveryReason::Unconfirmed;
  record.good=record.candidate=good;save(record);storage.loadHardware(recovery);
  assert(recovery.recovery && !recovery.hardwareRecord.hasConfirmed);
  assert(storage.stageHardware(recovery,good));storage.loadHardware(retry);
  assert(retry.hardwareTrial && !retry.hardwareRecord.hasConfirmed);
  assert(storage.confirmHardware(retry));storage.loadHardware(normal);
  assert(normal.hardwareRecord.hasConfirmed && !normal.recovery && !normal.controller.configurationLocked);

  record.hasConfirmed=true;record.phase=ConfigPhase::Confirmed;record.reason=RecoveryReason::None;
  save(record);auto damaged=std::get<Preferences::Bytes>(Preferences::database["hardware"]["config1"]);
  damaged.back()^=1;Preferences::database["hardware"]["config1"]=damaged;
  storage.loadHardware(recovery);
  assert(recovery.recovery && !recovery.hardwareRecord.hasConfirmed);
  assert(std::get<Preferences::Bytes>(Preferences::database["hardware"]["config1"])==damaged);
  save(record);Preferences::beginSucceeds=false;storage.loadHardware(recovery);
  assert(recovery.recovery && recovery.recoveryReason==RecoveryReason::Storage);
  Preferences::beginSucceeds=true;storage.loadHardware(normal);
  assert(normal.hardwareRecord.hasConfirmed && normal.hardwareRecord.generation==42);
  Preferences::database.clear();
}
static void optionalPwmEnable() {
  for(unsigned driver:{DRIVE_SEPARATE_PWM,DRIVE_BTS7960,DRIVE_DIR_PWM}) for(unsigned mask=0;mask<4;++mask) {
    HardwareConfig cfg;cfg.drive=driver;cfg.simulation=false;cfg.bluetooth=false;
    if(driver==DRIVE_BTS7960) {cfg.pins.azSpeedEnable=mask&1?21:-1;cfg.pins.elSpeedEnable=mask&2?47:-1;cfg.pwmInverted=true;}
    else {cfg.pins.azEnable=mask&1?38:-1;cfg.pins.elEnable=mask&2?39:-1;}
    assert(!board::validateHardware(cfg));
    auto conflict=cfg;
    if(driver==DRIVE_BTS7960) conflict.pins.azSpeedEnable=cfg.pins.azPositive;
    else conflict.pins.azEnable=cfg.pins.azPositive;
    assert(std::string(board::validateHardware(conflict))=="gpio_conflict");
    MotionTestRuntime r;r.controller.configureHardware(cfg);r.controller.feedback(90,45,true,true);
    Hardware h(r);fakeHardware={};h.beginDirections();h.beginAdc();
    const auto disabled=[&](){for(unsigned axis=0;axis<2;++axis)if(cfg.enablePin(axis)>=0)assert(fakeHardware.levels[cfg.enablePin(axis)]==LOW);};
    const auto noInvalid=[&](){for(const auto &e:fakeHardware.events)assert(e.pin>=0 && e.operation!="write_unowned");};
    disabled();noInvalid();
    const auto drive=[&](int az,int el){r.controller.axes[0].direction=az;r.controller.axes[1].direction=el;
      r.controller.axes[0].speedOutputPercent=50;r.controller.axes[1].speedOutputPercent=40;h.applyOutputs();};
    drive(1,-1);
    for(unsigned axis=0;axis<2;++axis)if(cfg.enablePin(axis)>=0)assert(fakeHardware.levels[cfg.enablePin(axis)]==HIGH);
    const auto reversedAt=fakeHardware.events.size();drive(-1,1);
    // An assigned ENABLE is off before PWM/direction changes and enabled afterwards.
    for(unsigned axis=0;axis<2;++axis)if(cfg.enablePin(axis)>=0) {
      bool off=false,on=false,changed=false;
      for(size_t i=reversedAt;i<fakeHardware.events.size();++i) {
        const auto &e=fakeHardware.events[i];
        if(e.pin==cfg.enablePin(axis)&&e.operation=="write") {if(!e.value)off=true;else{assert(off&&changed);on=true;}}
        const bool pwm=driver==DRIVE_BTS7960?(e.pin==(axis?cfg.pins.elNegative:cfg.pins.azNegative)||e.pin==(axis?cfg.pins.elPositive:cfg.pins.azPositive)):e.pin==(axis?cfg.pins.elSpeedEnable:cfg.pins.azSpeedEnable);
        if(pwm&&e.operation=="pwm") {assert(off&&!on);changed=true;}
      }
      assert(on);
    }
    drive(0,1);
    if(cfg.enablePin(0)>=0)assert(fakeHardware.levels[cfg.enablePin(0)]==LOW);
    if(cfg.enablePin(1)>=0)assert(fakeHardware.levels[cfg.enablePin(1)]==HIGH);
    h.allStop();disabled();drive(1,1);r.controller.driveFault=true;h.applyOutputs();disabled();
    const auto idle=cfg.pwmInverted?1023u:0u;
    std::map<int,uint32_t> last;
    for(const auto &e:fakeHardware.events)if(e.operation=="pwm")last[e.pin]=e.value;
    for(const auto &[pin,duty]:last) { (void)pin;assert(duty==idle); }
    noInvalid();
    HardwareRecord record;record.good=record.candidate=cfg;HardwareRecord restored;
    assert(decodeHardware(encodeHardware(record),restored)&&restored.good==cfg);
    // No enable signal may make an otherwise disconnected EL axis appear.
    cfg.pins.elAdc=cfg.pins.elNegative=cfg.pins.elPositive=-1;
    if(driver!=DRIVE_BTS7960)cfg.pins.elSpeedEnable=-1;
    assert(cfg.singleAxis()&&!board::validateHardware(cfg));
  }
  for(unsigned driver:{DRIVE_SEPARATE_PWM,DRIVE_BTS7960,DRIVE_DIR_PWM}) for(bool enable:{false,true}) {
    HardwareConfig cfg;cfg.drive=driver;cfg.simulation=false;cfg.bluetooth=false;
    if(driver==DRIVE_BTS7960)cfg.pins.azSpeedEnable=cfg.pins.elSpeedEnable=enable?38:-1;
    else cfg.pins.azEnable=cfg.pins.elEnable=enable?38:-1;
    if(enable) {if(driver==DRIVE_BTS7960)cfg.pins.elSpeedEnable=39;else cfg.pins.elEnable=39;}
    assert(!board::validateHardware(cfg));
    const auto count=driver==DRIVE_BTS7960?4:2;
    for(int fail=0;fail<count;++fail) {
      MotionTestRuntime r;r.controller.configureHardware(cfg);Hardware h(r);fakeHardware={};fakeHardware.failAttachAt=fail;
      h.beginDirections();r.controller.axes[0].direction=1;r.controller.axes[0].speedOutputPercent=80;h.applyOutputs();
      assert(r.controller.driveFault);
      for(unsigned axis=0;axis<2;++axis)if(cfg.enablePin(axis)>=0)assert(fakeHardware.levels[cfg.enablePin(axis)]==LOW);
      for(const auto &e:fakeHardware.events)assert(e.pin>=0 && e.operation!="write_unowned");
    }
    for(bool hud:{false,true}) {
      MotionTestRuntime r;auto isolated=cfg;isolated.simulation=!hud;r.controller.configureHardware(isolated);
      if(hud)r.hud.config.role=DeviceRole::Hud;
      Hardware h(r);fakeHardware={};h.beginDirections();h.applyOutputs();h.allStop();
      for(const auto &e:fakeHardware.events)for(unsigned axis=0;axis<2;++axis)assert(e.pin!=cfg.enablePin(axis));
    }
  }
  Preferences::database.clear();Storage storage;
  HardwareRecord old;old.hasConfirmed=true;old.good.drive=old.candidate.drive=DRIVE_BTS7960;
  old.good.sensors[6]=old.candidate.sensors[6]=1;
  const auto legacy=encodeHardwareRecord<204>(old);
  Preferences::database["hardware"]["config1"]=Preferences::Bytes(legacy.begin(),legacy.end());
  MotionTestRuntime boot;storage.loadHardware(boot);
  assert(!boot.recovery && boot.controller.hardware==old.good);
  assert(boot.controller.hardware.enablePin(0)==old.good.pins.azSpeedEnable && boot.controller.hardware.pins.azEnable==-1);
  assert(storage.finishHardwareBoot(boot));
}
static void directionPwm() {
  HardwareConfig cfg;cfg.drive=DRIVE_DIR_PWM;cfg.simulation=false;cfg.bluetooth=false;
  cfg.pins.azNegative=cfg.pins.uartRx;cfg.pins.elNegative=cfg.pins.uartTx;
  assert(!board::validateHardware(cfg));
  MotionTestRuntime r;r.controller.configureHardware(cfg);r.controller.feedback(90,45,true,true);
  Hardware h(r);fakeHardware={};h.beginDirections();h.beginAdc();
  const auto drive=[&](int az,int el){r.controller.axes[0].direction=az;r.controller.axes[1].direction=el;
    r.controller.axes[0].speedOutputPercent=50;r.controller.axes[1].speedOutputPercent=40;h.applyOutputs();};
  drive(1,-1);
  assert(fakeHardware.levels[cfg.pins.azPositive]==HIGH && fakeHardware.levels[cfg.pins.elPositive]==LOW);
  drive(-1,1);
  assert(fakeHardware.levels[cfg.pins.azPositive]==LOW && fakeHardware.levels[cfg.pins.elPositive]==HIGH);
  for(size_t i=0;i<fakeHardware.events.size();++i){const auto &e=fakeHardware.events[i];
    assert(e.pin!=cfg.pins.azNegative && e.pin!=cfg.pins.elNegative); // Unused pins can belong to UART.
    if(e.operation=="write"&&(e.pin==cfg.pins.azPositive||e.pin==cfg.pins.elPositive)) {
      assert(i>0);const auto &before=fakeHardware.events[i-1];
      assert(before.operation=="pwm" && before.value==0 && before.pin==(e.pin==cfg.pins.azPositive?cfg.pins.azSpeedEnable:cfg.pins.elSpeedEnable));
    }
  }
  r.controller.stopNow(-1,100);h.applyOutputs();
  for(int pin:{cfg.pins.azSpeedEnable,cfg.pins.elSpeedEnable}){
    bool found=false;for(auto i=fakeHardware.events.rbegin();i!=fakeHardware.events.rend();++i)if(i->pin==pin&&i->operation=="pwm"){assert(i->value==0);found=true;break;}assert(found);
  }
  r.controller.configurationLocked=true;h.applyOutputs();
  drive(1,1); // Locked output remains off.
  for(int pin:{cfg.pins.azSpeedEnable,cfg.pins.elSpeedEnable})for(auto i=fakeHardware.events.rbegin();i!=fakeHardware.events.rend();++i)if(i->pin==pin&&i->operation=="pwm"){assert(i->value==0);break;}
  cfg.pins.elAdc=cfg.pins.elPositive=cfg.pins.elSpeedEnable=-1;
  assert(cfg.singleAxis() && !board::validateHardware(cfg)); // Saved inactive negative does not create EL.
  HardwareRecord record;record.good=record.candidate=cfg;HardwareRecord restored;
  assert(decodeHardware(encodeHardware(record),restored)&&restored.good==cfg);
  Preferences::database.clear();Storage storage;MotionTestRuntime boot;storage.loadHardware(boot);assert(storage.finishHardwareBoot(boot));
  assert(storage.stageHardware(boot,cfg));MotionTestRuntime trial;storage.loadHardware(trial);
  assert(trial.hardwareTrial&&trial.controller.hardware.drive==DRIVE_DIR_PWM&&trial.controller.hardware.singleAxis());
  assert(storage.confirmHardware(trial));storage.loadHardware(boot);assert(boot.controller.hardware==cfg&&!boot.recovery);
}
int main() {
  confirmedRecoveryPreservesRecord();recoveryPolicy();directionPwm();optionalPwmEnable();
  // Each runtime driver/simulation combination is exercised in this one binary.
  for (unsigned drive=0; drive<4; ++drive) for (bool simulation : {false,true}) {
    HardwareConfig cfg; cfg.drive=drive; cfg.simulation=simulation;
    cfg.pwmInverted=drive==2; cfg.bluetooth=false;
    assert(!board::validateHardware(cfg));
    MotionTestRuntime r; r.controller.configureHardware(cfg);
    auto effective=cfg;effective.releaseSimulationPins();assert(r.controller.hardware==effective);
    r.controller.feedback(0,0,true,true);
    Hardware h(r); fakeHardware={};
    h.beginDirections(); h.beginAdc();
    if (simulation) {
      for (const auto &event : fakeHardware.events)
        assert(event.pin!=board::PIN_LEFT && event.pin!=board::PIN_AZ_ADC);
    }
    r.controller.configurationLocked=true;
    assert(r.controller.target({90,0,true,false},false,0)==Result::Unavailable);
    assert(r.controller.jog(0,1,false,0)==Result::Unavailable);
    assert(r.controller.execute(0)==Result::Unavailable);
    r.controller.tick(0); h.applyOutputs(); assert(!r.controller.moving());
    if (!simulation && drive==2) {
      assert(fakeHardware.levels[board::PIN_SPEED]==LOW);
      assert(fakeHardware.levels[board::PIN_EL_SPEED]==LOW);
      assert(fakeHardware.levels[board::PIN_LEFT]==HIGH);
    }
  }
  HardwareConfig cfg; cfg.buttonGpio=board::PIN_LEFT;
  assert(board::validateHardware(cfg));
  cfg={}; cfg.bluetooth=true; cfg.bluetoothMode=BT_ESP32_SPP;
  assert(board::validateHardware(cfg)); // default native test board is S3
  cfg={}; cfg.buttonGpio=6; cfg.bluetooth=true; cfg.bluetoothMode=BT_HC05;
  assert(board::validateHardware(cfg));
  cfg.bluetooth=false; assert(!board::validateHardware(cfg));

  Storage s; Preferences::database.clear(); Preferences::failedBytesKey.clear();
  MotionTestRuntime first; s.loadHardware(first);
  assert(!first.recovery && first.recoveryReason==RecoveryReason::None);
  assert(first.hardwareRecord.hasConfirmed && !first.controller.configurationLocked);
  assert(s.finishHardwareBoot(first));
  assert(s.stageHardware(first,first.controller.hardware));
  MotionTestRuntime initialTrial; s.loadHardware(initialTrial);
  assert(initialTrial.hardwareTrial && s.confirmHardware(initialTrial));
  s.loadHardware(first); assert(s.finishHardwareBoot(first));
  HardwareConfig candidate=first.controller.hardware;
  candidate.drive=DRIVE_BTS7960; candidate.pwmInverted=true;
  candidate.simulation=true; candidate.releaseSimulationPins(); candidate.bluetooth=true; candidate.bluetoothMode=BT_HC05;
  const auto good=first.hardwareRecord.good;
  assert(s.stageHardware(first,candidate));
  MotionTestRuntime trial; s.loadHardware(trial);
  assert(trial.hardwareTrial && !trial.recovery && trial.controller.configurationLocked);
  assert(trial.controller.hardware==candidate && trial.hardwareRecord.good==good);
  // Writing identical bytes is insufficient proof that NVS still accepts writes.
  Preferences::failedBytesKey="config1";
  assert(!s.probeHardwareStorage(trial));
  Preferences::failedBytesKey.clear();
  assert(s.probeHardwareStorage(trial) && trial.hardwareRecord.probeCounter==1);
  // A failed candidate falls back to known-good hardware, never to that candidate.
  MotionTestRuntime rolledBack; s.loadHardware(rolledBack);
  assert(!rolledBack.recovery && !rolledBack.hardwareTrial && !rolledBack.controller.configurationLocked);
  assert(rolledBack.hardwareRecord.good==good && rolledBack.controller.hardware==good);
  assert(rolledBack.recoveryHistory.failures==1);
  MotionTestRuntime secondFailure;s.loadHardware(secondFailure);assert(!secondFailure.recovery && secondFailure.recoveryHistory.failures==2);
  MotionTestRuntime persistentRecovery; s.loadHardware(persistentRecovery);
  assert(persistentRecovery.recovery && persistentRecovery.recoveryHistory.failures==3);
  assert(s.stageHardware(persistentRecovery,candidate));
  MotionTestRuntime secondTrial; s.loadHardware(secondTrial);
  assert(s.confirmHardware(secondTrial));
  assert(secondTrial.controller.configurationLocked); // queues are cleared only by reboot
  MotionTestRuntime confirmed; s.loadHardware(confirmed);
  assert(!confirmed.recovery && !confirmed.hardwareTrial && confirmed.controller.hardware==candidate);
  assert(s.finishHardwareBoot(confirmed));
  assert(s.enterRecovery(confirmed,RecoveryReason::LowMemory));
  MotionTestRuntime lowMemory; s.loadHardware(lowMemory);
  assert(!lowMemory.recovery && lowMemory.recoveryHistory.failures==1);
  assert(lowMemory.hardwareRecord.good==candidate && lowMemory.controller.hardware==candidate);
  const auto oldGeneration=lowMemory.hardwareRecord.generation;
  Preferences::failedBytesKey="config1";
  assert(!s.stageHardware(lowMemory,good));
  assert(lowMemory.hardwareRecord.generation==oldGeneration);
  Preferences::failedBytesKey.clear();
  // Corruption, interrupted boot and the RTC fallback do not start motor outputs.
  auto bytes=encodeHardware(lowMemory.hardwareRecord); HardwareRecord decoded;
  assert(decodeHardware(bytes,decoded)); bytes[12]^=1; assert(!decodeHardware(bytes,decoded));
  Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
  MotionTestRuntime corrupt; s.loadHardware(corrupt); assert(corrupt.recovery);
  assert(s.stageHardware(corrupt,good));
  MotionTestRuntime rtc; s.loadHardware(rtc,true); assert(rtc.recovery && rtc.controller.configurationLocked);

  MotionTestRuntime catalog; catalog.hud.config.role=DeviceRole::Hud;
  assert(catalog.hud.add(42,{0x01020304,4534,false},"remote"));
  assert(s.saveHudConfig(catalog.hud.config) && s.saveHudPeers(catalog.hud));
  MotionTestRuntime configOnly; s.loadHud(configOnly,false);
  assert(configOnly.hud.isHud() && configOnly.hud.count()==0);
  s.loadHud(configOnly,true); assert(configOnly.hud.count()==1);
  Calibration untouched(configOnly.controller); untouched.state().storageFault=true;
  WifiCredentials credentials;
  s.loadPrefs(configOnly,untouched,credentials,true);
  assert(untouched.state().storageFault); // recovery never loads/resets calibration tables

  MemoryGuard guard; guard.begin(UINT32_MAX-5000,0);
  assert(guard.sample(UINT32_MAX-5000,40000,16000,0,true)==RecoveryReason::None);
  assert(!guard.ready(1000) && guard.ready(6000)); // wrap-safe healthy interval
  assert(guard.sample(6001,40000,2048,0,true)==RecoveryReason::None);
  assert(!guard.ready(6002));
  assert(guard.sample(8001,40000,2048,0,true)==RecoveryReason::LowMemory);
  guard.begin(0,0);
  assert(guard.sample(1,50000,20000,1,false)==RecoveryReason::Allocation);
  guard.begin(0,0);
  assert(guard.sample(MemoryGuard::TrialMs,50000,20000,0,true)==RecoveryReason::Unconfirmed);
  assert(guard.sample(MemoryGuard::TrialMs,50000,20000,0,false)==RecoveryReason::None);

  const std::string body="token=123&generation=2&driveInterface=2&simulation=1&bluetoothEnabled=0&"
      "bluetoothMode=3&forceApSta=0&buttonGpio=-1&buttonActiveLow=1&buttonPullup=0&pwmInverted=1";
  HardwareForm form;
  assert(parseHardwareForm(body,form,true));
  assert(form.token==123 && form.config.buttonGpio==-1 && form.config.drive==2);
  assert(!parseHardwareForm(body+"&token=123",form,true));
  assert(!parseHardwareForm("token=0&generation=1",form,false));
  assert(!parseHardwareForm("token=4294967296&generation=1",form,false));
  assert(!parseHardwareForm("token=1&generation=1&simulation=1",form,false));
  assert(!parseHardwareForm(body+"&unknown=1",form,true));
  assert(!parseHardwareForm("token=1",form,false));
  assert(parseHardwareForm("token=1&generation=1&pin.azAdc=1",form,true,candidate));
  assert(form.config.pins.azAdc==-1 && form.config.drive==candidate.drive);
  assert(!parseHardwareForm("token=1&generation=1",form,true));
  assert(!parseHardwareForm("token=1&generation=1&pin.azAdc=1&pin.azAdc=2",form,true));
  std::vector<FormArgument> arguments;
  std::string_view remaining=body;
  for(;;) {
    const auto end=remaining.find('&'),equal=remaining.find('=');
    arguments.emplace_back(remaining.substr(0,equal),remaining.substr(equal+1,end==remaining.npos?end:end-equal-1));
    if(end==remaining.npos)break;
    remaining.remove_prefix(end+1);
  }
  HardwareForm decodedForm,raw;
  assert(parseHardwareArguments(arguments,decodedForm,true,candidate));
  assert(parseHardwareForm(body,raw,true,candidate));
  assert(decodedForm.config==raw.config && decodedForm.token==raw.token && decodedForm.generation==raw.generation);
  arguments.emplace_back("token","123");
  assert(!parseHardwareArguments(arguments,decodedForm,true,candidate));
  assert(decodedForm.config==raw.config); // failed input never partly replaces the result
  arguments={{"token","1"},{"generation","2"}};
  assert(parseHardwareArguments(arguments,decodedForm,false));
  assert(!parseHardwareArguments(arguments,decodedForm,true));
  arguments[1].second="2&simulation=1";
  assert(!parseHardwareArguments(arguments,decodedForm,false));
  arguments[1].second="2=1";
  assert(!parseHardwareArguments(arguments,decodedForm,false));
  arguments={{"token","0"},{"generation","2"}};
  assert(!parseHardwareArguments(arguments,decodedForm,false));
  std::cout<<"PASS: runtime driver selection, startup lock, verified NVS, trial rollback, recovery, memory guard and strict forms\n";
}
