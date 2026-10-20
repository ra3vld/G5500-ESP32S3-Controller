#include "motion_fixture.hpp"
#include "storage.hpp"
#include "hardware.hpp"
#include "hardware_form.hpp"
#include <cassert>
#include <iostream>
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
int main() {
  recoveryPolicy();
  // Each runtime driver/simulation combination is exercised in this one binary.
  for (unsigned drive=0; drive<3; ++drive) for (bool simulation : {false,true}) {
    HardwareConfig cfg; cfg.drive=drive; cfg.simulation=simulation;
    cfg.pwmInverted=drive==2; cfg.bluetooth=false;
    assert(!board::validateHardware(cfg));
    MotionTestRuntime r; r.controller.configureHardware(cfg);
    assert(r.controller.hardware==cfg);
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
  candidate.simulation=true; candidate.bluetooth=true; candidate.bluetoothMode=BT_HC05;
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
  assert(form.config.pins.azAdc==1 && form.config.drive==candidate.drive);
  assert(!parseHardwareForm("token=1&generation=1",form,true));
  assert(!parseHardwareForm("token=1&generation=1&pin.azAdc=1&pin.azAdc=2",form,true));
  std::cout<<"PASS: runtime driver selection, startup lock, verified NVS, trial rollback, recovery, memory guard and strict forms\n";
}
