// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "storage.hpp"
#include "calibration_record.hpp"
#include "motion_json.hpp"
#include "board.hpp"
#include "project_version.hpp"
#include "reset_history_record.hpp"
#include <nvs.h>
#include <esp_err.h>
#include <time.h>
#include <cstdlib>
#include <cstring>
#include <cmath>
namespace forkrot {
bool Storage::saveSetup(const SetupState &state) {
  const auto bytes=encodeSetup(state);SetupBytes verify{};
  if(!prefs_.begin("setup",false)){prefs_.end();return false;}
  const bool ok=prefs_.putBytes("state1",bytes.data(),bytes.size())==bytes.size() && prefs_.getBytesLength("state1")==bytes.size() &&
    prefs_.getBytes("state1",verify.data(),verify.size())==verify.size() && bytes==verify;
  prefs_.end();return ok;
}
bool Storage::loadSetup(Runtime &r) {
  SetupState state;bool found=false,ok=true;
  if(prefs_.begin("setup",true)) {
    found=prefs_.isKey("state1");
    if(found){SetupBytes bytes{};ok=prefs_.getBytesLength("state1")==bytes.size() && prefs_.getBytes("state1",bytes.data(),bytes.size())==bytes.size() && decodeSetup(bytes,state);}
  }
  prefs_.end();
  if(!ok){
    // A damaged progress record must not strand an otherwise repairable board.
    // Restart commissioning and retain all real settings.
    state.required=WEB_INTERFACE_ENABLED;state.step=state.required?SetupStep::Network:SetupStep::Done;state.generation=1;
    r.setup=state;r.controller.setupRequired=state.required;return saveSetup(state);
  }
  if(!found) {
    // Detect legacy use before loadHardware()/loadPrefs() create default records.
    bool legacy=false;
    const char *spaces[]={"hardware","g5500","forkcfg","hudcfg"};
    const char *keys[]={"config1","ssid","model","cfg1"};
    for(unsigned i=0;i<4;++i){if(prefs_.begin(spaces[i],true)) {
      legacy=legacy||prefs_.isKey(keys[i]);
      if(i==1)legacy=legacy||prefs_.isKey("appass")||prefs_.isKey("calfmt")||prefs_.isKey("calmode");
      if(i==2)legacy=legacy||prefs_.isKey("rotorName");
      if(i==3)legacy=legacy||prefs_.isKey("config");
    }prefs_.end();}
    state.required=WEB_INTERFACE_ENABLED && !legacy;state.step=state.required?SetupStep::Network:SetupStep::Done;
    state.equipment=legacy;state.fresh=state.required;state.generation=1;
    if(!saveSetup(state)){r.setup=state;return false;}
  }
  r.setup=state;
#if !WEB_INTERFACE_ENABLED
  // Preserve unfinished web progress on flash, but never gate a console-only
  // image behind a browser workflow it cannot serve.
  r.setup.required=false;r.setup.paused=false;r.setup.step=SetupStep::Done;
#endif
  r.controller.setupRequired=r.setup.required;return true;
}
bool Storage::saveNetwork(const NetworkConfig &config) {
  if(config.wifi.validate()||config.ethernet.validate())return false;
  const auto bytes=encodeNetwork(config);NetworkBytes verify{};
  if(!prefs_.begin("netcfg",false)){prefs_.end();return false;}
  const bool ok=prefs_.putBytes("ipv4v1",bytes.data(),bytes.size())==bytes.size() && prefs_.getBytesLength("ipv4v1")==bytes.size() &&
    prefs_.getBytes("ipv4v1",verify.data(),verify.size())==verify.size() && bytes==verify;
  prefs_.end();return ok;
}
bool Storage::loadNetwork(NetworkConfig &config) {
  if(!prefs_.begin("netcfg",true)){prefs_.end();return true;}
  const bool found=prefs_.isKey("ipv4v1");NetworkBytes bytes{};
  const bool ok=!found || (prefs_.getBytesLength("ipv4v1")==bytes.size() && prefs_.getBytes("ipv4v1",bytes.data(),bytes.size())==bytes.size() && decodeNetwork(bytes,config));
  prefs_.end();return ok;
}
bool Storage::stageNetwork(const NetworkConfig &previous, const NetworkConfig &next) {
  if(next.wifi.validate() || next.ethernet.validate())return false;
  if(!prefs_.begin("netcfg",false)){prefs_.end();return false;}
  const auto bytes=encodeNetwork(previous);NetworkBytes verify{};
  const bool exists=prefs_.isKey("trial1");
  const bool ok=(exists || prefs_.putBytes("trial1",bytes.data(),bytes.size())==bytes.size()) &&
    prefs_.getBytesLength("trial1")==verify.size() && prefs_.getBytes("trial1",verify.data(),verify.size())==verify.size();
  NetworkConfig old;const bool valid=ok && decodeNetwork(verify,old) && (exists || bytes==verify);
  prefs_.end();return valid && saveNetwork(next);
}
bool Storage::loadNetworkTrial(Runtime &runtime) {
  if(!prefs_.begin("netcfg",true)){prefs_.end();return true;}
  runtime.networkTrial=prefs_.isKey("trial1");NetworkBytes bytes{};NetworkConfig old;
  const bool ok=!runtime.networkTrial || (prefs_.getBytesLength("trial1")==bytes.size() &&
    prefs_.getBytes("trial1",bytes.data(),bytes.size())==bytes.size() && decodeNetwork(bytes,old));
  prefs_.end();runtime.networkTrialBooted=runtime.networkTrial;
  runtime.networkTrialUntil=millis()+Runtime::NetworkTrialMs;return ok;
}
bool Storage::confirmNetwork() {
  if(!prefs_.begin("netcfg",false)){prefs_.end();return false;}
  const bool ok=!prefs_.isKey("trial1") || (prefs_.remove("trial1") && !prefs_.isKey("trial1"));
  prefs_.end();return ok;
}
bool Storage::rollbackNetwork(NetworkConfig &config) {
  if(!prefs_.begin("netcfg",true)){prefs_.end();return false;}
  const bool exists=prefs_.isKey("trial1");NetworkBytes bytes{};NetworkConfig old;
  const bool ok=exists && prefs_.getBytesLength("trial1")==bytes.size() &&
    prefs_.getBytes("trial1",bytes.data(),bytes.size())==bytes.size() && decodeNetwork(bytes,old);
  prefs_.end();
  if(!exists)return loadNetwork(config);
  if(!ok || !saveNetwork(old))return false;
  // The caller also restores HUD topology before deleting the backup. A reset
  // between those writes must leave the service AP and rollback available.
  config=old;return true;
}
bool Storage::beginResetSequence(bool externalReset, bool &requested) {
  requested=false;
  if(!prefs_.begin("bootkeys",false)){prefs_.end();return false;}
  const uint8_t count=externalReset?std::min<unsigned>(prefs_.getUChar("reset",0)+1,3):0;
  const bool ok=prefs_.putUChar("reset",count)==1 && prefs_.getUChar("reset",255)==count;
  prefs_.end();requested=ok && count==3;return ok;
}
bool Storage::clearResetSequence() {
  if(!prefs_.begin("bootkeys",false)){prefs_.end();return false;}
  const bool ok=prefs_.putUChar("reset",0)==1 && prefs_.getUChar("reset",255)==0;
  prefs_.end();return ok;
}

using namespace board;
namespace {
const char *displayKey() {
  return DISPLAY_KIND==1?"pose1":DISPLAY_KIND==3?"pose3":DISPLAY_KIND==4?"pose4":DISPLAY_KIND==6?"pose6":"pose5";
}
}
bool Storage::loadDisplay(Runtime &runtime) {
  runtime.displaySettings={};
  runtime.displaySettings.visible=defaultDisplayFields(DISPLAY_KIND);
  if(!GRAPHICAL_DISPLAY)return runtime.displayStorageOk=true;
  if(!prefs_.begin("panel",false)){prefs_.end();return runtime.displayStorageOk=false;}
  DisplayBytes b{};
  const bool ok=!prefs_.isKey(displayKey()) ||
      (prefs_.getBytesLength(displayKey())==b.size() && prefs_.getBytes(displayKey(),b.data(),b.size())==b.size() && decodeDisplay(b,runtime.displaySettings,defaultDisplayFields(DISPLAY_KIND)));
  prefs_.end();return runtime.displayStorageOk=ok;
}
bool Storage::saveDisplay(const DisplaySettings &settings) {
  if(!GRAPHICAL_DISPLAY || !settings.valid() || !prefs_.begin("panel",false)){prefs_.end();return false;}
  const auto bytes=encodeDisplay(settings);DisplayBytes check{};
  const bool ok=prefs_.putBytes(displayKey(),bytes.data(),bytes.size())==bytes.size() &&
      prefs_.getBytesLength(displayKey())==bytes.size() && prefs_.getBytes(displayKey(),check.data(),check.size())==check.size() && check==bytes;
  prefs_.end();return ok;
}
namespace {
using RuntimeSpeedRecord = std::array<uint8_t, RuntimeSpeeds::Count * sizeof(float) + 4>;
uint32_t runtimeSpeedChecksum(const RuntimeSpeedRecord &record) {
  uint32_t hash = 2166136261u;
  for (size_t i=0; i<record.size()-4; ++i) hash=(hash^record[i])*16777619u;
  return hash;
}
RuntimeSpeedRecord encodeRuntimeSpeeds(const RuntimeSpeeds &speeds) {
  RuntimeSpeedRecord record{};
  std::memcpy(record.data(),speeds.values.data(),sizeof(speeds.values));
  const uint32_t checksum=runtimeSpeedChecksum(record);
  std::memcpy(record.data()+record.size()-4,&checksum,4);
  return record;
}
bool decodeRuntimeSpeeds(const RuntimeSpeedRecord &record, RuntimeSpeeds &speeds) {
  uint32_t checksum;
  std::memcpy(&checksum,record.data()+record.size()-4,4);
  if(checksum!=runtimeSpeedChecksum(record)) return false;
  RuntimeSpeeds next;
  std::memcpy(next.values.data(),record.data(),sizeof(next.values));
  for(unsigned i=0;i<next.values.size();++i) {
    const float v=next.values[i];
    if(!std::isfinite(v) || v<0 || (i%2==0 && v>100)) return false;
  }
  speeds=next;
  return true;
}
// The readiness flag and both regulator/simple settings share one verified NVS
// value. An interrupted legacy-key write cannot arm a mixed configuration.
using DriveRecord=std::array<uint8_t,MotionRecordSize+36>;
uint32_t driveChecksum(const DriveRecord &b) {
  uint32_t h=2166136261u;for(size_t i=0;i<b.size()-4;++i)h=(h^b[i])*16777619u;return h;
}
DriveRecord encodeDrive(const MotionSettings &m,const SpeedSettings &s) {
  DriveRecord b{};const auto motion=encodeMotionSettings(m);std::copy(motion.begin(),motion.end(),b.begin());
  size_t at=motion.size();auto put=[&](uint32_t n){for(unsigned i=0;i<4;++i)b[at++]=uint8_t(n>>(8*i));};
  for(unsigned n:{s.speedPercent,s.startSpeedPercent,s.approachSpeedPercent,s.accelerationMs,s.decelerationMs,s.slowdownDegrees})put(n);
  for(float n:{s.coastDegrees,s.decelerationFactor}){uint32_t bits;std::memcpy(&bits,&n,4);put(bits);}
  put(driveChecksum(b));return b;
}
bool decodeDrive(const DriveRecord &b,MotionSettings &m,SpeedSettings &s) {
  MotionRecord motion{};std::copy_n(b.begin(),motion.size(),motion.begin());
  MotionSettings next;SpeedSettings speed;
  if(!decodeMotionSettings(motion,next))return false;
  size_t at=motion.size();auto get=[&](){uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(b[at++])<<(8*i);return n;};
  speed.speedPercent=get();speed.startSpeedPercent=get();speed.approachSpeedPercent=get();
  speed.accelerationMs=get();speed.decelerationMs=get();speed.slowdownDegrees=get();
  auto real=[&](){const uint32_t n=get();float f;std::memcpy(&f,&n,4);return f;};
  speed.coastDegrees=real();speed.decelerationFactor=real();
  if(get()!=driveChecksum(b) || speed.speedPercent<1 || speed.speedPercent>100 ||
     speed.startSpeedPercent<1 || speed.startSpeedPercent>100 || speed.approachSpeedPercent<1 || speed.approachSpeedPercent>100 ||
     speed.accelerationMs<100 || speed.accelerationMs>10000 || speed.decelerationMs<100 || speed.decelerationMs>10000 ||
     speed.slowdownDegrees<1 || speed.slowdownDegrees>90 || !std::isfinite(speed.coastDegrees) || speed.coastDegrees<0 || speed.coastDegrees>90 ||
     !std::isfinite(speed.decelerationFactor) || speed.decelerationFactor<.1f || speed.decelerationFactor>10) return false;
  m=next;s=speed;return true;
}
}
bool Storage::loadModbus(Runtime &runtime) {
  runtime.modbus={};
  bool exists=false,legacy=false,ok=true;
  if(prefs_.begin("modbus",true)) {
    exists=prefs_.isKey("config1");
    if(exists) {
      const auto size=prefs_.getBytesLength("config1");
      if(size==LegacyModbusBytes{}.size()) {
        LegacyModbusBytes bytes{};legacy=true;
        ok=prefs_.getBytes("config1",bytes.data(),bytes.size())==bytes.size()&&decodeModbus(bytes,runtime.modbus);
      } else {
        ModbusBytes bytes{};
        ok=size==bytes.size()&&prefs_.getBytes("config1",bytes.data(),bytes.size())==bytes.size()&&decodeModbus(bytes,runtime.modbus);
      }
    }
  }
  prefs_.end();
  if(!ok)return false; // Never replace a damaged record with defaults.
  if(!exists) {
    if(prefs_.begin("hudcfg",true))legacy=prefs_.isKey("config");
    prefs_.end();
    if(legacy)runtime.modbus.uart1.baud=115200;
  }
  if(legacy) {
    if(runtime.hud.config.uart&&!runtime.controller.hardware.hudRs485()&&!runtime.modbus.pc)
      runtime.modbus.uart0.baud=runtime.hud.config.link.baud;
    return saveModbus(runtime.modbus);
  }
  return true;
}
bool Storage::saveModbus(const ModbusConfig &config) {
  if(config.validate())return false;
  const auto bytes=encodeModbus(config);ModbusBytes readback{};
  if(!prefs_.begin("modbus",false)){prefs_.end();return false;}
  const bool ok=prefs_.putBytes("config1",bytes.data(),bytes.size())==bytes.size() &&
      prefs_.getBytesLength("config1")==bytes.size() && prefs_.getBytes("config1",readback.data(),readback.size())==readback.size() && bytes==readback;
  prefs_.end();return ok;
}
bool Storage::writeHardware(const HardwareRecord &record) {
  const auto bytes = encodeHardware(record);
  return writeHardwareBlob("config1", bytes.data(), bytes.size());
}
bool Storage::writeRecovery(const RecoveryHistory &history) {
  const auto bytes=encodeRecovery(history);
  return writeHardwareBlob("recovery1", bytes.data(), bytes.size());
}
bool Storage::hardwareStorageFailure(const char *operation, const char *key, int32_t error,
                                    size_t expected, size_t actual) {
  hardwareStorageFailure_.operation=operation;hardwareStorageFailure_.key=key;
  hardwareStorageFailure_.error=error;hardwareStorageFailure_.expected=expected;
  hardwareStorageFailure_.actual=actual;++hardwareStorageFailure_.count;
  return false;
}
bool Storage::writeHardwareBlob(const char *key, const uint8_t *bytes, size_t size) {
  nvs_handle_t handle;
  auto error=nvs_open("hardware",NVS_READWRITE,&handle);
  if(error!=ESP_OK)return hardwareStorageFailure("open_write",key,error,size);
  error=nvs_set_blob(handle,key,bytes,size);
  if(error!=ESP_OK){nvs_close(handle);return hardwareStorageFailure("set_blob",key,error,size);}
  error=nvs_commit(handle);nvs_close(handle);
  if(error!=ESP_OK)return hardwareStorageFailure("commit",key,error,size);
  // Verify through a new read-only handle, including a successful commit.
  // Keep the last error even if the fallback recovery write succeeds.
  error=nvs_open("hardware",NVS_READONLY,&handle);
  if(error!=ESP_OK)return hardwareStorageFailure("open_verify",key,error,size);
  HardwareBytes verified{};size_t actual=0;
  error=nvs_get_blob(handle,key,nullptr,&actual);
  if(error!=ESP_OK){nvs_close(handle);return hardwareStorageFailure("get_length",key,error,size,actual);}
  if(actual!=size){nvs_close(handle);return hardwareStorageFailure("length",key,ESP_ERR_INVALID_SIZE,size,actual);}
  error=nvs_get_blob(handle,key,verified.data(),&actual);nvs_close(handle);
  if(error!=ESP_OK)return hardwareStorageFailure("get_blob",key,error,size,actual);
  if(actual!=size)return hardwareStorageFailure("length",key,ESP_ERR_INVALID_SIZE,size,actual);
  if(std::memcmp(bytes,verified.data(),size))return hardwareStorageFailure("compare",key,ESP_ERR_INVALID_CRC,size,actual);
  return true;
}
void Storage::writeHardwareStorageJson(JsonWriter &j, std::string_view view) {
  // These reads must neither create a namespace nor replace malformed records.
  if(view!="records") {
    nvs_stats_t stats{};const auto error=nvs_get_stats(nullptr,&stats);
    const auto &failure=hardwareStorageFailure_;
    j << "{\"namespace\":\"hardware\",\"firmware\":" << jsonQuoted(firmware::VERSION)
      << ",\"stats\":{\"error\":" << int(error) << ",\"name\":" << jsonQuoted(esp_err_to_name(error))
      << ",\"used\":" << unsigned(stats.used_entries) << ",\"free\":" << unsigned(stats.free_entries)
      << ",\"available\":" << unsigned(stats.available_entries) << ",\"total\":" << unsigned(stats.total_entries)
      << ",\"namespaces\":" << unsigned(stats.namespace_count) << ",\"settingsReserve\":" << unsigned(SettingsReserveEntries)
      << "},\"lastFailure\":{\"count\":" << failure.count
      << ",\"operation\":" << jsonQuoted(failure.operation) << ",\"key\":" << jsonQuoted(failure.key)
      << ",\"error\":" << failure.error << ",\"name\":" << jsonQuoted(esp_err_to_name(failure.error))
      << ",\"expected\":" << unsigned(failure.expected) << ",\"actual\":" << unsigned(failure.actual) << "}}";
    return;
  }
  nvs_handle_t handle;const auto error=nvs_open("hardware",NVS_READONLY,&handle);
  j << "{\"openError\":" << int(error) << ",\"openName\":" << jsonQuoted(esp_err_to_name(error));
  for(const auto *key:{"config1","recovery1"}) {
    HardwareBytes bytes{};size_t size=0;
    auto read=error==ESP_OK?nvs_get_blob(handle,key,nullptr,&size):error;
    if(read==ESP_OK) {
      if(size>bytes.size())read=ESP_ERR_NVS_INVALID_LENGTH;
      else read=nvs_get_blob(handle,key,bytes.data(),&size);
    }
    j << ',' << jsonQuoted(key) << ":{\"error\":" << int(read) << ",\"name\":" << jsonQuoted(esp_err_to_name(read))
      << ",\"bytes\":" << unsigned(size) << ",\"expectedBytes\":"
      << unsigned(key[0]=='c'?HardwareBytes{}.size():RecoveryBytes{}.size());
    if(read==ESP_OK) {
      bool valid=false;
      if(key[0]=='c') {
        HardwareRecord record;
        auto decode=[&](auto raw){if(size!=raw.size())return false;std::copy_n(bytes.begin(),raw.size(),raw.begin());return decodeHardware(raw,record);};
        valid=decode(LegacyHardwareBytes{}) || decode(PinHardwareBytes{}) || decode(LocalHardwareBytes{}) ||
          decode(UartHardwareBytes{}) || decode(SensorHardwareBytes{}) || decode(RadioHardwareBytes{}) ||
          decode(InvertedSensorHardwareBytes{}) || decode(EnableHardwareBytes{}) || decode(HardwareBytes{});
        j << ",\"version\":" << unsigned(bytes[2]);
        if(valid)j << ",\"generation\":" << record.generation << ",\"phase\":" << unsigned(record.phase)
          << ",\"reason\":" << jsonQuoted(recoveryReasonName(record.reason)) << ",\"hasConfirmed\":" << (record.hasConfirmed?"true":"false");
      } else if(size==RecoveryBytes{}.size()) {
        RecoveryBytes raw{};std::copy_n(bytes.begin(),raw.size(),raw.begin());RecoveryHistory history;
        valid=decodeRecovery(raw,history);
        if(valid)j << ",\"failures\":" << unsigned(history.failures) << ",\"pending\":" << (history.pending?"true":"false");
      }
      j << ",\"valid\":" << (valid?"true":"false");
    }
    j << '}';
  }
  if(error==ESP_OK)nvs_close(handle);
  j << '}';
}
bool Storage::diagnosticStorageRoom(size_t entries) {
  nvs_stats_t stats{};
  return nvs_get_stats(nullptr,&stats)==ESP_OK && stats.available_entries>=SettingsReserveEntries+entries;
}
bool Storage::compactResetHistory() {
  // Do not create an unused namespace. A scalar update needs just one entry,
  // so legacy history can be migrated even when a larger settings blob cannot.
  if(!prefs_.begin("g5500rst",true)){prefs_.end();return true;}
  prefs_.end();
  if(!prefs_.begin("g5500rst",false)){prefs_.end();return false;}
  for(unsigned i=0;i<RESET_HISTORY_MAX;++i) {
    char kt[8],kr[8],kc[8];
    std::snprintf(kt,sizeof kt,"t%02u",i);std::snprintf(kr,sizeof kr,"r%02u",i);std::snprintf(kc,sizeof kc,"c%02u",i);
    if(!prefs_.isKey(kt))continue;
    const auto stored=prefs_.getULong64(kt,UINT64_MAX);uint64_t epoch=0;int code=0;
    if(!unpackResetHistory(stored,epoch,code)) {
      char reason[32]{};uint64_t packed;
      if(!prefs_.isKey(kc) || !prefs_.getString(kr,reason,sizeof reason) ||
          !packResetHistory(stored,prefs_.getInt(kc,INT32_MIN),reason,packed))continue;
      if(prefs_.putULong64(kt,packed)!=sizeof packed || prefs_.getULong64(kt,0)!=packed) {prefs_.end();return false;}
    }
    // Once tNN contains the whole event, it is authoritative. Deleting the
    // redundant keys is restartable, including a reset between the two erases.
    for(const auto *key:{kr,kc})if(prefs_.isKey(key) && (!prefs_.remove(key) || prefs_.isKey(key))) {prefs_.end();return false;}
  }
  prefs_.end();return true;
}
void Storage::loadHardware(Runtime &runtime, bool forceRecovery, bool requestedRecovery) {
  compactResetHistory(); // Reclaim redundant diagnostics before a mandatory migration.
  runtime.hardwareInteraction = false;
  runtime.controller.targetMotionStarted = false;
  runtime.hardwareProbe = false;
  runtime.hardwareHudUart = runtime.hud.config.uart;
  runtime.hardwareHudRole = runtime.hud.isHud();
  HardwareRecord record;
  RecoveryHistory history;
  bool readable = prefs_.begin("hardware", false);
  bool valid = true, historyValid = true;
  const bool fresh = readable && !prefs_.isKey("config1");
  if (readable && !fresh) {
    HardwareBytes bytes{};
    if(prefs_.getBytesLength("config1")==LegacyHardwareBytes{}.size()) {
      LegacyHardwareBytes legacy{};
      valid=prefs_.getBytes("config1",legacy.data(),legacy.size())==legacy.size() && decodeHardware(legacy,record);
    } else if(prefs_.getBytesLength("config1")==PinHardwareBytes{}.size()) {
      PinHardwareBytes legacy{};
      valid=prefs_.getBytes("config1",legacy.data(),legacy.size())==legacy.size() && decodeHardware(legacy,record);
    } else if(prefs_.getBytesLength("config1")==LocalHardwareBytes{}.size()) {
      LocalHardwareBytes legacy{};
      valid=prefs_.getBytes("config1",legacy.data(),legacy.size())==legacy.size() && decodeHardware(legacy,record);
    } else if(prefs_.getBytesLength("config1")==UartHardwareBytes{}.size()) {
      UartHardwareBytes legacy{};
      valid=prefs_.getBytes("config1",legacy.data(),legacy.size())==legacy.size() && decodeHardware(legacy,record);
    } else if(prefs_.getBytesLength("config1")==EnableHardwareBytes{}.size()) {
      EnableHardwareBytes legacy{};
      valid=prefs_.getBytes("config1",legacy.data(),legacy.size())==legacy.size() && decodeHardware(legacy,record);
    } else if(prefs_.getBytesLength("config1")==InvertedSensorHardwareBytes{}.size()) {
      InvertedSensorHardwareBytes legacy{};
      valid=prefs_.getBytes("config1",legacy.data(),legacy.size())==legacy.size() && decodeHardware(legacy,record);
    } else if(prefs_.getBytesLength("config1")==RadioHardwareBytes{}.size()) {
      RadioHardwareBytes legacy{};
      valid=prefs_.getBytes("config1",legacy.data(),legacy.size())==legacy.size() && decodeHardware(legacy,record);
    } else if(prefs_.getBytesLength("config1")==SensorHardwareBytes{}.size()) {
      SensorHardwareBytes legacy{};
      valid=prefs_.getBytes("config1",legacy.data(),legacy.size())==legacy.size() && decodeHardware(legacy,record);
    } else valid = prefs_.getBytesLength("config1") == bytes.size() &&
        prefs_.getBytes("config1", bytes.data(), bytes.size()) == bytes.size() && decodeHardware(bytes, record);
  }
  if (readable && prefs_.isKey("recovery1")) {
    RecoveryBytes bytes{};
    historyValid=prefs_.getBytesLength("recovery1")==bytes.size() &&
        prefs_.getBytes("recovery1",bytes.data(),bytes.size())==bytes.size() && decodeRecovery(bytes,history);
  }
  prefs_.end();
  const auto savedHistory=history;
  // A capability removed by this image must not erase the last known drive
  // configuration. Validate its physical fields with the unavailable radio off.
  auto invalidStored = [&](HardwareConfig config) {
    if (!BLUETOOTH_SUPPORT) config.bluetooth = false;
    return validateHardware(config, runtime.hud.config.uart,runtime.hardwareHudRole);
  };
  // Pending starts the candidate; Testing on disk means an interrupted trial
  // and must fall back to good. An inactive map cannot veto the selected one.
  const bool candidateBoot=record.phase==ConfigPhase::Pending && !forceRecovery && !history.pending;
  const auto &bootConfig=candidateBoot?record.candidate:record.good;
  if (!readable || !valid) {
    record = {};
    record.phase = ConfigPhase::Recovery;
    record.reason = !readable ? RecoveryReason::Storage : RecoveryReason::Invalid;
  } else if (!historyValid) {
    record.phase=ConfigPhase::Recovery;record.reason=RecoveryReason::Storage;
  } else if (invalidStored(bootConfig)) {
    // The record passed its format/checksum checks. Preserve both maps, the
    // confirmation and generation even when this role/image cannot use it.
    record.phase = ConfigPhase::Recovery;
    record.reason = RecoveryReason::Invalid;
  } else if (fresh || (!record.hasConfirmed && record.phase == ConfigPhase::Recovery &&
                       record.reason == RecoveryReason::Initial)) {
    // A fresh board starts with this build's hardware defaults. Only later
    // hardware edits need an explicit trial/confirmation; faults stay latched.
    record = {};
    record.hasConfirmed = true; record.phase = ConfigPhase::Starting;
    record.reason = RecoveryReason::None;
    history={};
  } else if ((record.phase != ConfigPhase::Recovery || automaticRecoveryReason(record.reason)) &&
             (forceRecovery || history.pending || record.phase == ConfigPhase::Testing ||
              record.phase == ConfigPhase::Starting || record.phase == ConfigPhase::Recovery)) {
    // Retry only known-good hardware. A failed candidate is never enabled.
    // The pending marker prevents a detected fault and its reboot counting twice.
    if (!history.pending && !(record.phase==ConfigPhase::Recovery && history.due()))
      history.record(record.phase==ConfigPhase::Recovery?record.reason:RecoveryReason::Unconfirmed);
    history.pending=false;
    record.phase=history.due() || !record.hasConfirmed ? ConfigPhase::Recovery : ConfigPhase::Starting;
    record.reason=record.phase==ConfigPhase::Recovery?history.last:RecoveryReason::None;
  } else if (record.phase == ConfigPhase::Pending) {
    record.phase = ConfigPhase::Testing;
  } else if (record.phase == ConfigPhase::Confirmed) {
    record.phase = record.hasConfirmed ? ConfigPhase::Starting : ConfigPhase::Recovery;
    if (!record.hasConfirmed) record.reason = RecoveryReason::Initial;
  }
  if(requestedRecovery){record.phase=ConfigPhase::Recovery;record.reason=RecoveryReason::Requested;}
  record.good.releaseSimulationPins();
  record.candidate.releaseSimulationPins();
  // A failed read or undecodable record is not permission to overwrite NVS
  // with defaults. Only an explicit recovery edit may replace those bytes.
  if (readable && valid && (!writeHardware(record) || (!(history==savedHistory) && !writeRecovery(history)))) {
    record.phase = ConfigPhase::Recovery; record.reason = RecoveryReason::Storage;
    writeHardware(record);
  }
  runtime.hardwareRecord = record;
  runtime.recoveryHistory = history;
  runtime.recovery = record.phase == ConfigPhase::Recovery;
  runtime.recoveryReason = record.reason;
  runtime.hardwareTrial = record.phase == ConfigPhase::Testing;
  HardwareConfig effective = runtime.hardwareTrial ? record.candidate : record.good;
  if (runtime.recovery) {
    effective=recoveryHardware(record);
    // Retaining an incompatible confirmed map must not activate its UART pins.
    if(invalidStored(record.good))effective.pins.uartRx=effective.pins.uartTx=-1;
  }
  runtime.controller.configureHardware(effective);
  runtime.controller.configurationLocked = runtime.recovery;
}
bool Storage::stageHardware(Runtime &runtime, const HardwareConfig &config) {
  if (validateHardware(config, runtime.hardwareHudUart,runtime.configuredAsHud())) return false;
  auto next = runtime.hardwareRecord;
  next.candidate = config; next.candidate.releaseSimulationPins();
  // Retrying the exact confirmed map is already authorized. Only an untested
  // configuration needs a trial with automatic rollback until confirmed.
  next.phase = next.hasConfirmed && next.candidate == next.good ? ConfigPhase::Confirmed : ConfigPhase::Pending;
  next.reason = RecoveryReason::None;
  if (!++next.generation) ++next.generation;
  // An explicit retry from recovery starts a new series. Ordinary settings
  // edits and successful short boots do not erase accumulated runtime faults.
  if (runtime.recovery && !writeRecovery({})) return false;
  if (!writeHardware(next)) return false;
  if (runtime.recovery) runtime.recoveryHistory={};
  runtime.hardwareRecord = next; return true;
}
bool Storage::confirmHardware(Runtime &runtime) {
  if (!runtime.hardwareTrial || runtime.hardwareRecord.phase != ConfigPhase::Testing) return false;
  auto next = runtime.hardwareRecord;
  next.good = next.candidate; next.hasConfirmed = true;
  next.phase = ConfigPhase::Confirmed; next.reason = RecoveryReason::None;
  if (!writeHardware(next)) return false;
  runtime.hardwareRecord = next; runtime.hardwareTrial = false;
  // The candidate is already running; confirmation only makes it persistent.
  return true;
}
bool Storage::enterRecovery(Runtime &runtime, RecoveryReason reason) {
  auto next = runtime.hardwareRecord;
  auto history=runtime.recoveryHistory;
  const bool automatic=automaticRecoveryReason(reason) && next.hasConfirmed;
  if (automatic) history.record(reason);
  else history.pending=false;
  if (!(history==runtime.recoveryHistory) && !writeRecovery(history)) return false;
  next.phase=automatic && !history.due()?ConfigPhase::Confirmed:ConfigPhase::Recovery;
  next.reason=next.phase==ConfigPhase::Recovery?reason:RecoveryReason::None;
  // Keep last confirmed settings and the failed candidate for inspection/editing.
  runtime.recoveryReason = reason;
  if (!writeHardware(next)) return false;
  runtime.hardwareRecord = next; runtime.recoveryHistory=history; return true;
}
bool Storage::finishHardwareBoot(Runtime &runtime, uint32_t uptime) {
  if (runtime.hardwareRecord.phase == ConfigPhase::Starting) {
    auto next = runtime.hardwareRecord; next.phase = ConfigPhase::Confirmed;
    if (!writeHardware(next)) return false;
    runtime.hardwareRecord = next;
  }
  if (runtime.hardwareRecord.phase==ConfigPhase::Confirmed && runtime.recoveryHistory.failures &&
      uptime>=RecoveryHistory::StableMs) {
    if (!writeRecovery({})) return false;
    runtime.recoveryHistory={};
  }
  return true;
}
bool Storage::probeHardwareStorage(Runtime &runtime) {
  // Verify the same persistent record using a separate transaction, without
  // modifying motor/network settings or accepting a trial on elapsed time alone.
  auto next = runtime.hardwareRecord;
  ++next.probeCounter;
  if (!writeHardware(next)) return false;
  runtime.hardwareRecord = next; return true;
}
const char *Storage::calNamespace(const Runtime &runtime) {
  return runtime.controller.model==Model::G800SA?"g800sacal":runtime.controller.model!=Model::G5500?"g800cal":"g5500";
}

void Storage::loadPrefs(Runtime &runtime, Calibration &calibration, WifiCredentials &credentials, bool wifiOnly) {
  auto &calibrationState = calibration.state();
  if(!loadNetwork(credentials.network)) {
    runtime.controller.fault="network_storage_failed";
  }
  wifiCredentialsReadOk_ = prefs_.begin("g5500", true);
  credentials.wifiSsid = wifiCredentialsReadOk_ ? prefs_.getString("ssid", "") : String("");
  credentials.wifiPass = wifiCredentialsReadOk_ ? prefs_.getString("pass", "") : String("");
  credentials.apPass = wifiCredentialsReadOk_ ? prefs_.getString("appass", AP_PASS) : String(AP_PASS);

  // v1.6.23 format 4: independent P36/P45 + explicit two-point endpoint flags.
  prefs_.end();
  if (wifiOnly || runtime.hud.isHud())
    return;
  prefs_.begin(calNamespace(runtime), true);
  const size_t recordSize = prefs_.getBytesLength("azgeom1");
  if (prefs_.isKey("azgeom1")) {
    calibrationState = {};
    bool readOk = false;
    if (recordSize == CalibrationRecord{}.size()) {
      CalibrationRecord record{};
      readOk = prefs_.getBytes("azgeom1", record.data(), record.size()) == record.size() && readCalibrationRecord(record, calibrationState);
    } else if (recordSize == CalibrationRecordV1{}.size()) {
      CalibrationRecordV1 record{};
      readOk = prefs_.getBytes("azgeom1", record.data(), record.size()) == record.size() && readCalibrationRecord(record, calibrationState);
    }
    prefs_.end();
    if (!readOk) {
      calibrationState = {};
      calibrationState.storageFault = true;
      runtime.controller.fault = "calibration_storage_invalid";
    } else if (recordSize == CalibrationRecordV1{}.size() && !saveCal(runtime, calibration)) {
      runtime.controller.fault = "calibration_save_failed";
    }
    bindPositionSensors(calibrationState,runtime.controller.hardware);
    calibration.refreshMultiCalValidity();
    calibrationState.cal.valid = calibration.selectedCalibrationValid();
    runtime.controller.setRange(calibration.activeAzRange(), calibrationState.gsAzMode);
  runtime.controller.setElRange(calibration.activeElRange(), calibrationState.elProfile);
    return;
  }
  calibrationState = {};
  uint8_t calFmt = prefs_.getUChar("calfmt", 0);

  calibrationState.cal.az36_0 = 0;
  calibrationState.cal.az36Max = 3100;
  calibrationState.cal.az45_0 = 0;
  calibrationState.cal.az45Max = 3100;
  calibrationState.cal.el0 = 0;
  calibrationState.cal.elMax = 3100;

  calibrationState.az36OSet = calibrationState.az36FSet = calibrationState.az45OSet =
      calibrationState.az45FSet = false;
  calibrationState.elOSet = calibrationState.elFSet = false;
  calibrationState.az36LinearValid = calibrationState.az45LinearValid =
      calibrationState.elLinearValid = false;

  for (uint8_t i = 0; i < AZ36_CAL_N; i++) {
    calibrationState.az36CalMv[i] = -1;
  }
  for (uint8_t i = 0; i < AZ45_CAL_N; i++) {
    calibrationState.az45CalMv[i] = -1;
  }
  for (uint8_t i = 0; i < EL_CAL_N; i++) {
    calibrationState.elCalMv[i] = -1;
  }

  if (calFmt == 4) {
    calibrationState.cal.az36_0 = prefs_.getInt("a36z", 0);
    calibrationState.cal.az36Max = prefs_.getInt("a36f", 3100);
    calibrationState.cal.az45_0 = prefs_.getInt("a45z", 0);
    calibrationState.cal.az45Max = prefs_.getInt("a45f", 3100);
    calibrationState.cal.el0 = prefs_.getInt("el0mv", 0);
    calibrationState.cal.elMax = prefs_.getInt("elmaxmv", 3100);

    calibrationState.az36OSet = prefs_.getBool("a36os", false);
    calibrationState.az36FSet = prefs_.getBool("a36fs", false);
    calibrationState.az45OSet = prefs_.getBool("a45os", false);
    calibrationState.az45FSet = prefs_.getBool("a45fs", false);
    calibrationState.elOSet = prefs_.getBool("elos", false);
    calibrationState.elFSet = prefs_.getBool("elfs", false);

    calibrationState.az36CalMv[0] = prefs_.getInt("a36m0", -1);
    calibrationState.az36CalMv[1] = prefs_.getInt("a36m90", -1);
    calibrationState.az36CalMv[2] = prefs_.getInt("a36m180", -1);
    calibrationState.az36CalMv[3] = prefs_.getInt("a36m270", -1);
    calibrationState.az36CalMv[4] = prefs_.getInt("a36m360", -1);

    calibrationState.az45CalMv[0] = prefs_.getInt("a45m0", -1);
    calibrationState.az45CalMv[1] = prefs_.getInt("a45m90", -1);
    calibrationState.az45CalMv[2] = prefs_.getInt("a45m180", -1);
    calibrationState.az45CalMv[3] = prefs_.getInt("a45m270", -1);
    calibrationState.az45CalMv[4] = prefs_.getInt("a45m360", -1);
    calibrationState.az45CalMv[5] = prefs_.getInt("a45m450", -1);

    calibrationState.elCalMv[0] = prefs_.getInt("elm0", -1);
    calibrationState.elCalMv[1] = prefs_.getInt("elm45", -1);
    calibrationState.elCalMv[2] = prefs_.getInt("elm90", -1);
    calibrationState.elCalMv[3] = prefs_.getInt("elm135", -1);
    calibrationState.elCalMv[4] = prefs_.getInt("elm180", -1);

  } else if (calFmt == 3) {
    // Migration from 1.5.58. Its per-endpoint state was unreliable.
    // Preserve measured 2-point values but force endpoints to be captured again.
    calibrationState.cal.az36_0 = prefs_.getInt("a36z", 0);
    calibrationState.cal.az36Max = prefs_.getInt("a36f", 3100);
    calibrationState.cal.az45_0 = prefs_.getInt("a45z", 0);
    calibrationState.cal.az45Max = prefs_.getInt("a45f", 3100);
    calibrationState.cal.el0 = prefs_.getInt("el0mv", 0);
    calibrationState.cal.elMax = prefs_.getInt("elmaxmv", 3100);

    // AZ MULTI was independent in 1.5.58 and is safe to preserve.
    calibrationState.az36CalMv[0] = prefs_.getInt("a36m0", -1);
    calibrationState.az36CalMv[1] = prefs_.getInt("a36m90", -1);
    calibrationState.az36CalMv[2] = prefs_.getInt("a36m180", -1);
    calibrationState.az36CalMv[3] = prefs_.getInt("a36m270", -1);
    calibrationState.az36CalMv[4] = prefs_.getInt("a36m360", -1);

    calibrationState.az45CalMv[0] = prefs_.getInt("a45m0", -1);
    calibrationState.az45CalMv[1] = prefs_.getInt("a45m90", -1);
    calibrationState.az45CalMv[2] = prefs_.getInt("a45m180", -1);
    calibrationState.az45CalMv[3] = prefs_.getInt("a45m270", -1);
    calibrationState.az45CalMv[4] = prefs_.getInt("a45m360", -1);
    calibrationState.az45CalMv[5] = prefs_.getInt("a45m450", -1);

    // EL MULTI endpoints in 1.5.58 could be inherited from 2-point.
    // Keep only the unambiguous middle points; recapture 0° and 180°.
    calibrationState.elCalMv[0] = -1;
    calibrationState.elCalMv[1] = prefs_.getInt("elm45", -1);
    calibrationState.elCalMv[2] = prefs_.getInt("elm90", -1);
    calibrationState.elCalMv[3] = prefs_.getInt("elm135", -1);
    calibrationState.elCalMv[4] = -1;

  } else if (calFmt == 2) {
    // Migration from the earlier single-AZ format with explicit flags when available.
    int oldAz0 = prefs_.getInt("az0mv", 0);
    int oldAzMax = prefs_.getInt("azmaxmv", 3100);
    float oldMaxDeg = prefs_.getFloat("az2maxdeg", 450.0f);
    bool oldO = prefs_.getBool("az2oset", false);
    bool oldF = prefs_.getBool("az2fset", false);

    if (oldMaxDeg < 400.0f) {
      calibrationState.cal.az36_0 = oldAz0;
      calibrationState.cal.az36Max = oldAzMax;
      calibrationState.az36OSet = oldO;
      calibrationState.az36FSet = oldF;
    } else {
      calibrationState.cal.az45_0 = oldAz0;
      calibrationState.cal.az45Max = oldAzMax;
      calibrationState.az45OSet = oldO;
      calibrationState.az45FSet = oldF;
    }

    calibrationState.cal.el0 = prefs_.getInt("el0mv", 0);
    calibrationState.cal.elMax = prefs_.getInt("elmaxmv", 3100);
    calibrationState.elOSet = prefs_.getBool("el2oset", false);
    calibrationState.elFSet = prefs_.getBool("el2fset", false);

    int oldAz[6];
    oldAz[0] = prefs_.getInt("azm0", -1);
    oldAz[1] = prefs_.getInt("azm90", -1);
    oldAz[2] = prefs_.getInt("azm180", -1);
    oldAz[3] = prefs_.getInt("azm270", -1);
    oldAz[4] = prefs_.getInt("azm360", -1);
    oldAz[5] = prefs_.getInt("azm450", -1);
    for (uint8_t i = 0; i < AZ36_CAL_N; i++) {
      calibrationState.az36CalMv[i] = oldAz[i];
    }
    for (uint8_t i = 0; i < AZ45_CAL_N; i++) {
      calibrationState.az45CalMv[i] = oldAz[i];
    }

    calibrationState.elCalMv[0] = prefs_.getInt("elm0", -1);
    calibrationState.elCalMv[1] = prefs_.getInt("elm45", -1);
    calibrationState.elCalMv[2] = prefs_.getInt("elm90", -1);
    calibrationState.elCalMv[3] = prefs_.getInt("elm135", -1);
    calibrationState.elCalMv[4] = prefs_.getInt("elm180", -1);
  }

  uint8_t savedMode = prefs_.getUChar("calmode", 255);
  uint8_t savedAzMode = prefs_.getUChar("azmode", 45);
  prefs_.end();

  calibrationState.az36LinearValid =
      (calibrationState.az36OSet && calibrationState.az36FSet &&
       abs(calibrationState.cal.az36Max - calibrationState.cal.az36_0) > 100);
  calibrationState.az45LinearValid =
      (calibrationState.az45OSet && calibrationState.az45FSet &&
       abs(calibrationState.cal.az45Max - calibrationState.cal.az45_0) > 100);
  calibrationState.elLinearValid =
      (calibrationState.elOSet && calibrationState.elFSet &&
       abs(calibrationState.cal.elMax - calibrationState.cal.el0) > 100);

  calibration.refreshMultiCalValidity();
  calibrationState.gsAzMode = (savedAzMode == 36) ? 36 : 45;

  if (savedMode == CAL_GS232_2POINT || savedMode == CAL_MULTI_POINT) {
    calibrationState.calibrationMode = (CalibrationMode)savedMode;
  } else {
    bool anyMulti = (calibrationState.az36MultiValid || calibrationState.az45MultiValid) &&
                    calibrationState.elMultiValid;
    calibrationState.calibrationMode = anyMulti ? CAL_MULTI_POINT : CAL_GS232_2POINT;
  }

  calibrationState.cal.valid = calibration.selectedCalibrationValid();
  bindPositionSensors(calibrationState,runtime.controller.hardware);
  // Migrate once, including format 4, without changing the legacy keys' meaning.
  if (!saveCal(runtime, calibration)) runtime.controller.fault = "calibration_save_failed";
  runtime.controller.setRange(calibration.activeAzRange(), calibrationState.gsAzMode);
  runtime.controller.setElRange(calibration.activeElRange(), calibrationState.elProfile);
}

bool Storage::saveCal(const Runtime &runtime, Calibration &calibration) {
  if (runtime.hud.isHud() || calibration.state().storageFault)
    return false;
  auto &calibrationState = calibration.state();
  calibrationState.az36LinearValid =
      (calibrationState.az36OSet && calibrationState.az36FSet &&
       abs(calibrationState.cal.az36Max - calibrationState.cal.az36_0) > 100);
  calibrationState.az45LinearValid =
      (calibrationState.az45OSet && calibrationState.az45FSet &&
       abs(calibrationState.cal.az45Max - calibrationState.cal.az45_0) > 100);
  calibrationState.elLinearValid =
      (calibrationState.elOSet && calibrationState.elFSet &&
       abs(calibrationState.cal.elMax - calibrationState.cal.el0) > 100);

  calibration.refreshMultiCalValidity();
  calibrationState.cal.valid = calibration.selectedCalibrationValid();

  if (!prefs_.begin(calNamespace(runtime), false)) { prefs_.end(); return false; }
  const auto record = calibrationRecord(calibrationState);
  CalibrationRecord verified{};
  prefs_.putBytes("azgeom1", record.data(), record.size());
  const bool saved = prefs_.getBytesLength("azgeom1") == record.size() &&
      prefs_.getBytes("azgeom1", verified.data(), verified.size()) == verified.size() && verified == record;
  if (!saved) { prefs_.end(); return false; }
  prefs_.putUChar("calfmt", 4);
  prefs_.putUChar("calmode", (uint8_t)calibrationState.calibrationMode);
  if (calibrationState.gsAzMode != AZ_CUSTOM) prefs_.putUChar("azmode", calibrationState.gsAzMode);

  prefs_.putInt("a36z", calibrationState.cal.az36_0);
  prefs_.putInt("a36f", calibrationState.cal.az36Max);
  prefs_.putInt("a45z", calibrationState.cal.az45_0);
  prefs_.putInt("a45f", calibrationState.cal.az45Max);
  prefs_.putBool("a36os", calibrationState.az36OSet);
  prefs_.putBool("a36fs", calibrationState.az36FSet);
  prefs_.putBool("a45os", calibrationState.az45OSet);
  prefs_.putBool("a45fs", calibrationState.az45FSet);

  prefs_.putInt("el0mv", calibrationState.cal.el0);
  prefs_.putInt("elmaxmv", calibrationState.cal.elMax);
  prefs_.putBool("elos", calibrationState.elOSet);
  prefs_.putBool("elfs", calibrationState.elFSet);

  prefs_.putInt("a36m0", calibrationState.az36CalMv[0]);
  prefs_.putInt("a36m90", calibrationState.az36CalMv[1]);
  prefs_.putInt("a36m180", calibrationState.az36CalMv[2]);
  prefs_.putInt("a36m270", calibrationState.az36CalMv[3]);
  prefs_.putInt("a36m360", calibrationState.az36CalMv[4]);

  prefs_.putInt("a45m0", calibrationState.az45CalMv[0]);
  prefs_.putInt("a45m90", calibrationState.az45CalMv[1]);
  prefs_.putInt("a45m180", calibrationState.az45CalMv[2]);
  prefs_.putInt("a45m270", calibrationState.az45CalMv[3]);
  prefs_.putInt("a45m360", calibrationState.az45CalMv[4]);
  prefs_.putInt("a45m450", calibrationState.az45CalMv[5]);

  prefs_.putInt("elm0", calibrationState.elCalMv[0]);
  prefs_.putInt("elm45", calibrationState.elCalMv[1]);
  prefs_.putInt("elm90", calibrationState.elCalMv[2]);
  prefs_.putInt("elm135", calibrationState.elCalMv[3]);
  prefs_.putInt("elm180", calibrationState.elCalMv[4]);
  prefs_.end();
  return true;
}

WifiSaveResult Storage::saveWifi(const String &s, const String &p) {
  if (!s.length() || s.length() > 32) return WifiSaveResult::InvalidSsid;
  if (p.length() && (p.length() < 8 || p.length() > 64)) return WifiSaveResult::InvalidPassword;
  if (p.length() == 64) {
    for (size_t i = 0; i < p.length(); ++i) {
      char c = p.c_str()[i];
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        return WifiSaveResult::InvalidPassword;
    }
  }
  if (!prefs_.begin("g5500", false)) return WifiSaveResult::OpenFailed;
  const size_t ssidWritten = prefs_.putString("ssid", s);
  const size_t passwordWritten = prefs_.putString("pass", p);
  prefs_.end();
  if (ssidWritten != s.length() || passwordWritten != p.length()) return WifiSaveResult::WriteFailed;

  // Reopen NVS rather than trusting the request or an in-memory copy. An empty
  // password returns zero even on success, so read back both keys with a sentinel.
  if (!prefs_.begin("g5500", true)) return WifiSaveResult::VerifyFailed;
  const bool verified = prefs_.getString("ssid", "\x01") == s &&
                        prefs_.getString("pass", "\x01") == p;
  prefs_.end();
  return verified ? WifiSaveResult::Ok : WifiSaveResult::VerifyFailed;
}

bool Storage::saveApPassword(const String &p) {
  if(!prefs_.begin("g5500", false)){prefs_.end();return false;}
  const auto n=prefs_.putString("appass",p);const bool ok=n==p.length() && prefs_.isKey("appass") && prefs_.getString("appass")==p;
  prefs_.end();return ok;
}

void Storage::loadTouchCalibration(TouchCalibration &touchCalibration) {
  prefs_.begin("g5500", true);
  touchCalibration.touchCalValid = prefs_.getBool("tcalok", false);
  touchCalibration.touchSwapXY = prefs_.getBool("tswap", true);
  touchCalibration.touchX0Raw = prefs_.getInt("tx0", TOUCH_RAW_Y_MIN);
  touchCalibration.touchX1Raw = prefs_.getInt("tx1", TOUCH_RAW_Y_MAX);
  touchCalibration.touchY0Raw = prefs_.getInt("ty0", TOUCH_RAW_X_MAX);
  touchCalibration.touchY1Raw = prefs_.getInt("ty1", TOUCH_RAW_X_MIN);
  prefs_.end();
  if (abs(touchCalibration.touchX1Raw - touchCalibration.touchX0Raw) < 500 ||
      abs(touchCalibration.touchY1Raw - touchCalibration.touchY0Raw) < 500) {
    touchCalibration.touchCalValid = false;
  }
}

void Storage::saveTouchCalibration(const TouchCalibration &touchCalibration) {
  prefs_.begin("g5500", false);
  prefs_.putBool("tcalok", touchCalibration.touchCalValid);
  prefs_.putBool("tswap", touchCalibration.touchSwapXY);
  prefs_.putInt("tx0", touchCalibration.touchX0Raw);
  prefs_.putInt("tx1", touchCalibration.touchX1Raw);
  prefs_.putInt("ty0", touchCalibration.touchY0Raw);
  prefs_.putInt("ty1", touchCalibration.touchY1Raw);
  prefs_.end();
}

void Storage::loadForkSettings(Runtime &runtime) {
  if(!runtime.hud.isHud()) {
    prefs_.begin("forkmotion",true);
    const unsigned hz=prefs_.getUInt("controlhz",1000/MotionControlPeriodMs);
    runtime.controller.setControlFrequency(validMotionHz(hz)?hz:50,0);
    prefs_.end();
  }
  prefs_.begin("forkcfg",true);
  const auto savedModel=prefs_.getUChar("model",0);
  runtime.controller.model=savedModel<=uint8_t(Model::G800SA)?Model(savedModel):Model::G5500;
  if (!runtime.hud.isHud() && !runtime.controller.hardware.simulation && runtime.controller.hardware.singleAxis() && runtime.controller.model==Model::G5500) runtime.controller.model = Model::G800DXA;
  runtime.controller.restoreMode(prefs_.getUChar("mode", 0) == 1 ? forkrot::Mode::Auto
                                                                 : forkrot::Mode::Manual);
  if (runtime.controller.hardware.drive == DRIVE_YAESU) {
  runtime.controller.speedEnabled = prefs_.getBool("speedon", false);
  runtime.controller.speedPercent =
      constrain(prefs_.getUInt("speed", 50), 1u, 100u);
  runtime.controller.startSpeedPercent =
      constrain(prefs_.getUInt("startspd", 25), 1u, 100u);
  runtime.controller.approachSpeedPercent =
      constrain(prefs_.getUInt("approachspd", 25), 1u, 100u);
  runtime.controller.accelerationMs = constrain(prefs_.getUInt("accelms", 2000), 100u, 10000u);
  runtime.controller.decelerationMs = constrain(prefs_.getUInt("decelms", 1500), 100u, 10000u);
  runtime.controller.slowdownDegrees = constrain(prefs_.getUInt("slowdeg", 10), 1u, 90u);
  for (unsigned i = 0; i < 2; ++i) {
    auto &speed = runtime.controller.speeds[i];
    const String prefix = i ? "el" : "az";
    const float coast = prefs_.getFloat((prefix + "coast").c_str(), speed.coastDegrees);
    const float factor = prefs_.getFloat((prefix + "decelf").c_str(), 1);
    if (std::isfinite(coast) && coast >= 0 && coast <= 90) speed.coastDegrees = coast;
    if (std::isfinite(factor) && factor >= 0.1f && factor <= 10) speed.decelerationFactor = factor;
  }
  }
  const String savedName = prefs_.getString("name", "G5500");
  runtime.protocol.rotorName = (runtime.hud.isHud() ? prefs_.getString("hudname", savedName) : savedName).c_str();
  runtime.loggerSettings.n1mmPort = prefs_.getUShort("nport", 12040);
  runtime.loggerSettings.pstPort = prefs_.getUShort("pport", 1234);
  runtime.loggerSettings.reportPort = prefs_.getUShort("rport", 13010);
  runtime.loggerSettings.commandPort = prefs_.getUShort("cmdport", 4535);
  runtime.loggerSettings.reportHost = prefs_.getString("rhost", "").c_str();
  runtime.protocol.manualFollow.n1mm = prefs_.getBool("nmanual", false);
  runtime.protocol.manualFollow.pst = prefs_.getBool("pmanual", false);
  runtime.protocol.manualFollow.gs232 = prefs_.getBool("gmanual", false);
  runtime.protocol.manualFollow.rotctld = prefs_.getBool("rmanual", false);
  runtime.protocol.setExternalAzMode(prefs_.getBool("azmech", false)
                                        ? forkrot::ExternalAzMode::Mechanical
                                        : forkrot::ExternalAzMode::Shortest);
#if SELECTED_BOARD == BOARD_CROWPANEL_21
  runtime.loggerSettings.encoderInverted = prefs_.getBool("encinv", true);
#endif
  prefs_.end();
  if (runtime.controller.hardware.drive != DRIVE_YAESU || runtime.controller.hardware.simulation) {
  prefs_.begin("forkpwm", true);
  for (unsigned i=0; i<2; ++i) {
    auto &speed = runtime.controller.speeds[i];
    const String prefix = i ? "el" : "az";
    speed.speedPercent =
        constrain(prefs_.getUInt((prefix + "work").c_str(), speed.speedPercent), 1u, 100u);
    speed.startSpeedPercent =
        constrain(prefs_.getUInt((prefix + "start").c_str(), speed.startSpeedPercent), 1u, 100u);
    speed.approachSpeedPercent = constrain(
        prefs_.getUInt((prefix + "approach").c_str(), speed.approachSpeedPercent), 1u, 100u);
    speed.accelerationMs =
        constrain(prefs_.getUInt((prefix + "accel").c_str(), speed.accelerationMs), 100u, 10000u);
    speed.decelerationMs =
        constrain(prefs_.getUInt((prefix + "decel").c_str(), speed.decelerationMs), 100u, 10000u);
    speed.slowdownDegrees =
        constrain(prefs_.getUInt((prefix + "slow").c_str(), speed.slowdownDegrees), 1u, 90u);
    const float coast = prefs_.getFloat((prefix + "coast").c_str(), speed.coastDegrees);
    const float factor = prefs_.getFloat((prefix + "decelf").c_str(), 1);
    if (std::isfinite(coast) && coast >= 0 && coast <= 90) speed.coastDegrees = coast;
    if (std::isfinite(factor) && factor >= 0.1f && factor <= 10) speed.decelerationFactor = factor;
  }
  prefs_.end();
  }
  loadMotionSettings(runtime);
  runtime.controller.runtimeSpeeds = {};
  if (!runtime.hud.isHud() && prefs_.begin("forkspeed",true)) {
    RuntimeSpeedRecord record{};
    if (prefs_.getBytesLength("values1")==record.size() &&
        prefs_.getBytes("values1",record.data(),record.size())==record.size())
      decodeRuntimeSpeeds(record,runtime.controller.runtimeSpeeds);
  }
  prefs_.end();
}

bool Storage::serviceRuntimeSpeeds(Runtime &runtime, uint32_t now, bool force) {
  auto &speeds=runtime.controller.runtimeSpeeds;
  if(runtime.hud.isHud() || !speeds.dirty || (!force && !speeds.due(now))) return true;
  // Flash writes can suspend the control thread. In a fast loop, keep the
  // pending value until STOP; a controlled restart still explicitly flushes.
  if(!force && runtime.controller.controlPeriodMs()<20 && runtime.controller.moving()) return true;
  const auto record=encodeRuntimeSpeeds(speeds);
  bool valid=false;
  if(prefs_.begin("forkspeed",false)) {
    prefs_.putBytes("values1",record.data(),record.size());
    RuntimeSpeedRecord check{};
    valid=prefs_.getBytesLength("values1")==record.size() &&
        prefs_.getBytes("values1",check.data(),check.size())==check.size() && check==record;
  }
  prefs_.end();
  if(valid) speeds.dirty=false;
  else speeds.changedAt=now; // Retry after the same delay instead of hammering NVS.
  return valid;
}

void Storage::loadMotionSettings(Runtime &runtime) {
  if(runtime.hud.isHud()) return;
  runtime.controller.stop();
  runtime.controller.motionSettings = {};
  prefs_.begin("forkmotion",true);
  for(unsigned i=0;i<2;++i) {
    char key[12]; std::snprintf(key,sizeof key,"%c%u%u%u",runtime.controller.hardware.simulation==SimulationMechanics?'m':runtime.controller.hardware.simulation?'s':'r',
        unsigned(runtime.controller.hardware.drive),unsigned(runtime.controller.model),i);
    DriveRecord record{}; MotionSettings settings; SpeedSettings speed;
    const size_t storedSize=prefs_.getBytesLength(key);
    if(storedSize==record.size() && prefs_.getBytes(key,record.data(),record.size())==record.size() &&
       decodeDrive(record,settings,speed)) { runtime.controller.motionSettings[i]=settings;runtime.controller.speeds[i]=speed; }
    else if(storedSize) {
      // Missing settings use working defaults; damaged settings fail closed.
      runtime.controller.motionSettings[i].ready=false;
      runtime.controller.fault="motion_storage_failed";
    }
  }
  prefs_.end();

}

bool Storage::saveForkSettings(const Runtime &runtime) {
  const auto &savedAz=reportedSpeed(runtime.controller,0);
  bool valid=true;
  auto putUChar=[&](const char *key,uint8_t value) {
    valid=(prefs_.putUChar(key,value)==sizeof value && prefs_.getUChar(key)==value) && valid;
  };
  auto putUShort=[&](const char *key,uint16_t value) {
    valid=(prefs_.putUShort(key,value)==sizeof value && prefs_.getUShort(key)==value) && valid;
  };
  auto putUInt=[&](const char *key,uint32_t value) {
    valid=(prefs_.putUInt(key,value)==sizeof value && prefs_.getUInt(key)==value) && valid;
  };
  auto putBool=[&](const char *key,bool value) {
    valid=(prefs_.putBool(key,value)==sizeof value && prefs_.getBool(key)==value) && valid;
  };
  auto putFloat=[&](const char *key,float value) {
    valid=(prefs_.putFloat(key,value)==sizeof value && prefs_.getFloat(key)==value) && valid;
  };
  auto putString=[&](const char *key,const char *value) {
    const size_t size=std::strlen(value);
    // Empty strings return zero on success; existence and readback distinguish it from failure.
    valid=(prefs_.putString(key,value)==size && prefs_.isKey(key) && prefs_.getString(key)==value) && valid;
  };
  if(!prefs_.begin("forkcfg", false)){prefs_.end();return false;}
  if (!runtime.hud.isHud()) {
    putUChar("model", uint8_t(runtime.controller.model));
    putUChar("mode", runtime.controller.mode == forkrot::Mode::Auto ? 1 : 0);
  if (runtime.controller.hardware.drive == DRIVE_YAESU) {
  putBool("speedon", runtime.controller.speedEnabled);
  putUInt("speed", savedAz.speedPercent);
  putUInt("startspd", savedAz.startSpeedPercent);
  putUInt("approachspd", savedAz.approachSpeedPercent);
  putUInt("accelms", savedAz.accelerationMs);
  putUInt("decelms", savedAz.decelerationMs);
  putUInt("slowdeg", savedAz.slowdownDegrees);
  for (unsigned i = 0; i < 2; ++i) {
    const auto &speed = reportedSpeed(runtime.controller,i);
    const String prefix = i ? "el" : "az";
    putFloat((prefix + "coast").c_str(), speed.coastDegrees);
    putFloat((prefix + "decelf").c_str(), speed.decelerationFactor);
  }
  }
  putString("name", runtime.protocol.rotorName.c_str());
  } else
    putString("hudname", runtime.protocol.rotorName.c_str());
  putUShort("nport", runtime.loggerSettings.n1mmPort);
  putUShort("pport", runtime.loggerSettings.pstPort);
  putUShort("rport", runtime.loggerSettings.reportPort);
  putUShort("cmdport", runtime.loggerSettings.commandPort);
  putString("rhost", runtime.loggerSettings.reportHost.c_str());
  putBool("nmanual", runtime.protocol.manualFollow.n1mm);
  putBool("pmanual", runtime.protocol.manualFollow.pst);
  putBool("gmanual", runtime.protocol.manualFollow.gs232);
  putBool("rmanual", runtime.protocol.manualFollow.rotctld);
  putBool("azmech", runtime.protocol.externalAzMode == forkrot::ExternalAzMode::Mechanical);
#if SELECTED_BOARD == BOARD_CROWPANEL_21
  putBool("encinv", runtime.loggerSettings.encoderInverted);
#endif
  prefs_.end();
  if(!valid)return false;
  if (runtime.controller.hardware.drive != DRIVE_YAESU || runtime.controller.hardware.simulation) {
  if (runtime.hud.isHud())
    return true;
  if(!prefs_.begin("forkpwm", false)){prefs_.end();return false;}
  for (unsigned i=0; i<2; ++i) {
    const auto &speed = reportedSpeed(runtime.controller,i);
    const String prefix = i ? "el" : "az";
    putUInt((prefix + "work").c_str(), speed.speedPercent);
    putUInt((prefix + "start").c_str(), speed.startSpeedPercent);
    putUInt((prefix + "approach").c_str(), speed.approachSpeedPercent);
    putUInt((prefix + "accel").c_str(), speed.accelerationMs);
    putUInt((prefix + "decel").c_str(), speed.decelerationMs);
    putUInt((prefix + "slow").c_str(), speed.slowdownDegrees);
    putFloat((prefix + "coast").c_str(), speed.coastDegrees);
    putFloat((prefix + "decelf").c_str(), speed.decelerationFactor);
  }
  prefs_.end();
  if(!valid)return false;
  }
  if(runtime.hud.isHud()) return true;
  const bool opened=prefs_.begin("forkmotion",false);
  if(!opened) { prefs_.end(); return false; }
  putUInt("controlhz",1000/runtime.controller.controlPeriodMs());

  for(unsigned i=0;i<2;++i) {
    char key[12]; std::snprintf(key,sizeof key,"%c%u%u%u",runtime.controller.hardware.simulation==SimulationMechanics?'m':runtime.controller.hardware.simulation?'s':'r',
        unsigned(runtime.controller.hardware.drive),unsigned(runtime.controller.model),i);
    const auto record=encodeDrive(reportedMotion(runtime.controller,i),reportedSpeed(runtime.controller,i));
    const auto written=prefs_.putBytes(key,record.data(),record.size()); DriveRecord check{};
    valid=written==record.size() && prefs_.getBytesLength(key)==record.size() && prefs_.getBytes(key,check.data(),check.size())==check.size() && check==record && valid;
  }
  prefs_.end(); return valid;

}

const char *Storage::presetNamespace(const Runtime &runtime) {
  return runtime.controller.model==Model::G800SA?"fork800sa":runtime.controller.model!=Model::G5500?"fork800":"fork5500";
}

void Storage::loadProfilePresets(Runtime &runtime) {
  if (runtime.hud.isHud())
    return;
  prefs_.begin(presetNamespace(runtime), true);
  for (unsigned i = 0; i < 4; ++i) {
    String base = "p" + String(i);
    runtime.controller.presets[i].label =
        prefs_.getString((base + "n").c_str(), String(i * 90) + "°").c_str();
    runtime.controller.presets[i].target = {prefs_.getFloat((base + "a").c_str(), i * 90),
                                            prefs_.getFloat((base + "e").c_str(), 0), true,
                                            runtime.controller.model == forkrot::Model::G5500};
  }
  prefs_.end();
}

void Storage::saveProfilePresets(const Runtime &runtime) {
  if (runtime.hud.isHud())
    return;
  prefs_.begin(presetNamespace(runtime), false);
  for (unsigned i = 0; i < 4; ++i) {
    String base = "p" + String(i);
    prefs_.putString((base + "n").c_str(), runtime.controller.presets[i].label.c_str());
    prefs_.putFloat((base + "a").c_str(), runtime.controller.presets[i].target.az);
    prefs_.putFloat((base + "e").c_str(), runtime.controller.presets[i].target.el);
  }
  prefs_.end();
}

bool Storage::addResetHistoryEntry(int bootResetReason, const String &bootResetReasonText) {
#if RESET_HISTORY_SUPPORT

  resetHistorySlot_=-1;
  if(!compactResetHistory() || !diagnosticStorageRoom(7))return false;
  if(!prefs_.begin("g5500rst", false)) { prefs_.end(); return false; }

  uint8_t head = prefs_.getUChar("head", 0);
  uint8_t count = prefs_.getUChar("count", 0);
  if (head >= RESET_HISTORY_MAX) {
    head = 0;
  }
  if (count > RESET_HISTORY_MAX) {
    count = RESET_HISTORY_MAX;
  }

  time_t now = time(nullptr);
  uint64_t epoch = (now > 1700000000) ? (uint64_t)now : 0ULL;

  char kt[8], kr[8], kc[8];
  snprintf(kt, sizeof(kt), "t%02u", head);
  snprintf(kr, sizeof(kr), "r%02u", head);
  snprintf(kc, sizeof(kc), "c%02u", head);

  char reason[32];
  const auto length=std::min(bootResetReasonText.length(),sizeof reason-1);
  std::memcpy(reason,bootResetReasonText.c_str(),length);reason[length]=0;
  uint64_t packed;
  if(packResetHistory(epoch,bootResetReason,reason,packed)) {
    if(prefs_.putULong64(kt,packed)!=sizeof packed || prefs_.getULong64(kt,0)!=packed) {prefs_.end();return false;}
    for(const auto *key:{kr,kc})if(prefs_.isKey(key) && (!prefs_.remove(key) || prefs_.isKey(key))) {prefs_.end();return false;}
  } else {
    // Retain custom/unknown historical text without narrowing its code/time.
    const bool timeSaved=prefs_.putULong64(kt,epoch)==sizeof epoch;
    const auto written=prefs_.putString(kr,reason);
    const bool codeSaved=prefs_.putInt(kc,bootResetReason)==sizeof(int32_t);
    char verified[32]{};
    if(!timeSaved || !codeSaved || written!=length || !prefs_.getString(kr,verified,sizeof verified) ||
        std::strcmp(reason,verified) || prefs_.getULong64(kt,UINT64_MAX)!=epoch ||
        prefs_.getInt(kc,INT32_MIN)!=bootResetReason) { prefs_.end();return false; }
  }
  const auto slot=head;
  head = (head + 1) % RESET_HISTORY_MAX;
  if (count < RESET_HISTORY_MAX) {
    count++;
  }

  prefs_.putUChar("head", head);
  prefs_.putUChar("count", count);
  const bool saved=prefs_.getUChar("head",255)==head && prefs_.getUChar("count",255)==count;
  prefs_.end();
  if(saved)resetHistorySlot_=int8_t(slot);
  return saved;

#else
(void)bootResetReason;(void)bootResetReasonText;return true;
#endif
}

bool Storage::updateResetHistoryTime() {
#if RESET_HISTORY_SUPPORT

  const time_t now=time(nullptr);
  if(resetHistorySlot_<0 || now<=1700000000)return false;
  if(!diagnosticStorageRoom(1))return false;
  if(!prefs_.begin("g5500rst",false)) { prefs_.end();return false; }
  char key[8];snprintf(key,sizeof key,"t%02u",unsigned(resetHistorySlot_));
  uint64_t epoch=0,value=uint64_t(now);int code=0;
  if(unpackResetHistory(prefs_.getULong64(key,0),epoch,code) &&
      !packResetHistory(value,code,resetHistoryReasonName(unsigned(code)),value)) {prefs_.end();return false;}
  const bool saved=prefs_.putULong64(key,value)==sizeof value && prefs_.getULong64(key,0)==value;
  prefs_.end();return saved;

#else
return true;
#endif
}

bool Storage::writeResetHistoryJson(JsonWriter &j) {
#if RESET_HISTORY_SUPPORT

  if(!prefs_.begin("g5500rst", true)) {
    prefs_.end();j << "{\"available\":false,\"count\":0,\"entries\":[]}";return j.good();
  }
  uint8_t head = prefs_.getUChar("head", 0);
  uint8_t count = prefs_.getUChar("count", 0);
  if (head >= RESET_HISTORY_MAX) {
    head = 0;
  }
  if (count > RESET_HISTORY_MAX) {
    count = RESET_HISTORY_MAX;
  }

  j << "{\"available\":true,\"count\":" << count << ",\"entries\":[";
  for (uint8_t n = 0; n < count; n++) {
    int idx = (int)head - 1 - (int)n;
    while (idx < 0) {
      idx += RESET_HISTORY_MAX;
    }

    char kt[8], kr[8], kc[8];
    snprintf(kt, sizeof(kt), "t%02d", idx);
    snprintf(kr, sizeof(kr), "r%02d", idx);
    snprintf(kc, sizeof(kc), "c%02d", idx);

    uint64_t epoch = prefs_.getULong64(kt, 0ULL);
    char reason[32]{};
    int code=0;
    if(unpackResetHistory(epoch,epoch,code))std::strcpy(reason,resetHistoryReasonName(unsigned(code)));
    else {
      if(!prefs_.getString(kr,reason,sizeof reason))std::strcpy(reason,"UNKNOWN");
      code=prefs_.getInt(kc,0);
    }

    if (n) {
      j << ',';
    }
    j << "{\"epoch\":" << epoch << ",\"reason\":" << jsonQuoted(reason) << ",\"code\":" << code << '}';
  }
  j << "]}";
  prefs_.end();
  return j.good();

#else
j << "{\"available\":false,\"count\":0,\"entries\":[]}";return true;
#endif
}

void Storage::clearResetHistory() {
#if RESET_HISTORY_SUPPORT

  resetHistorySlot_=-1;
  if(!prefs_.begin("g5500rst", false)) {prefs_.end();return;}
  prefs_.clear();
  prefs_.end();

#else

#endif
}
void Storage::loadRssiYear(RssiHistory &history) {
  prefs_.begin("g5500rssi", true);
  size_t n = prefs_.getBytesLength("year");
  if (n == sizeof(history.rssiYear)) {
    prefs_.getBytes("year", history.rssiYear, sizeof(history.rssiYear));
  }
  history.rssiYearPos = prefs_.getUShort("ypos", 0);
  history.rssiYearCount = prefs_.getUShort("ycnt", 0);
  prefs_.end();
}
void Storage::persistRssiYear(const RssiHistory &history) {
  if(!diagnosticStorageRoom(17))return;
  if(!prefs_.begin("g5500rssi",false)){prefs_.end();return;}
  prefs_.putBytes("year", history.rssiYear, sizeof(history.rssiYear));
  prefs_.putUShort("ypos", history.rssiYearPos);
  prefs_.putUShort("ycnt", history.rssiYearCount);
  prefs_.end();
}

} // namespace forkrot

namespace forkrot {
namespace {
struct HudDiskConfig {
  uint32_t version = 1;
  uint8_t role = 0, wifi = 0, uart = 0, reserved = 0;
  uint32_t port = 4534;
  uint32_t selectedLo = 0, selectedHi = 0;
  uint32_t crc = 0;
};
struct HudDiskConfigV2 {
  HudDiskConfig base;
  uint32_t baud = 115200;
  uint16_t intervalMs = 100;
  uint8_t readyHigh = 1, reserved = 0;
  uint32_t crc = 0;
};
struct HudDiskConfigV3 {
  HudDiskConfig base;
  uint32_t baud = 115200;
  uint16_t intervalMs = 100;
  uint8_t readyHigh = 1, simulationEnabled = 0;
  uint32_t crc = 0;
};
static_assert(sizeof(HudDiskConfigV2) == sizeof(HudDiskConfigV3));
struct HudDiskPeer {
  uint32_t idLo = 0, idHi = 0, address = 0;
  uint16_t port = 4534;
  uint8_t uart = 0, background = 0;
  char label[49]{};
  uint8_t padding[3]{};
};
static_assert(sizeof(HudDiskPeer) == 68);
} // namespace
bool Storage::saveHudConfig(const HudConfig &c) {
  if (!c.link.valid()) return false;
  HudDiskConfigV3 d;
  d.base.version = 3;
  d.base.role = uint8_t(c.role);
  d.base.wifi = uint8_t(c.wifi);
  d.base.uart = c.uart;
  d.base.port = c.port;
  d.base.selectedLo = uint32_t(c.selected);
  d.base.selectedHi = uint32_t(c.selected >> 32);
  d.baud = c.link.baud; d.intervalMs = c.link.intervalMs;
  d.readyHigh = c.link.readyHigh;
  d.simulationEnabled = c.simulationEnabled;
  d.crc = hudCrc(reinterpret_cast<const uint8_t *>(&d), sizeof d - 4);
  if (!prefs_.begin("hudcfg", false)) {
    prefs_.end();
    return false;
  }
  prefs_.putBytes("config", &d, sizeof d);
  prefs_.end();
  HudDiskConfigV3 check;
  const bool opened = prefs_.begin("hudcfg", true);
  const bool ok = opened && prefs_.getBytes("config", &check, sizeof check) == sizeof check &&
                  std::memcmp(&d, &check, sizeof d) == 0;
  prefs_.end();
  return ok;
}
bool Storage::saveHudPeers(const Hud &hud) {
  size_t count = 0;
  for (auto *p = hud.peers(); p; p = p->next.get())
    if (p->saved && !p->simulated()) {
      if (!validHudBackground(p->background)) return false;
      ++count;
    }
  if (count > (SIZE_MAX - 8) / sizeof(HudDiskPeer))
    return false;
  const size_t size = 8 + count * sizeof(HudDiskPeer);
  auto data = std::unique_ptr<uint8_t, decltype(&std::free)>(static_cast<uint8_t *>(std::calloc(1, size)),
                                                             &std::free);
  if (!data)
    return false;
  const uint32_t version = 3;
  std::memcpy(data.get(), &version, 4);
  size_t offset = 4;
  for (auto *p = hud.peers(); p; p = p->next.get())
    if (p->saved && !p->simulated()) {
      HudDiskPeer d;
      d.idLo = uint32_t(p->id);
      d.idHi = uint32_t(p->id >> 32);
      d.address = p->endpoint.address;
      d.port = p->endpoint.port;
      d.uart = p->endpoint.mqtt ? 2 : uint8_t(p->endpoint.uart);
      d.background = p->background;
      std::memcpy(d.label, p->label, sizeof d.label);
      std::memcpy(data.get() + offset, &d, sizeof d);
      offset += sizeof d;
    }
  const uint32_t crc = hudCrc(data.get(), size - 4);
  std::memcpy(data.get() + size - 4, &crc, 4);
  if (!prefs_.begin("hudcfg", false)) {
    prefs_.end();
    return false;
  }
  prefs_.putBytes("peers", data.get(), size);
  prefs_.end();
  if (!prefs_.begin("hudcfg", true)) {
    prefs_.end();
    return false;
  }
  const bool ok = prefs_.getBytesLength("peers") == size &&
                  prefs_.getBytes("peers", data.get(), size) == size && hudCrc(data.get(), size - 4) == crc;
  prefs_.end();
  return ok;
}
void Storage::loadHud(Runtime &runtime, bool peers) {
  auto &hud = runtime.hud;
  const bool opened = prefs_.begin("hudcfg", true);
  if (opened && prefs_.getBytesLength("config")) {
    HudDiskConfig d;
    HudLinkConfig link;
    bool simulationEnabled = false;
    bool valid = false;
    if (prefs_.getBytesLength("config") == sizeof d) {
      valid = prefs_.getBytes("config", &d, sizeof d) == sizeof d && d.version == 1 &&
          hudCrc(reinterpret_cast<const uint8_t *>(&d), sizeof d - 4) == d.crc;
    } else if (prefs_.getBytesLength("config") == sizeof(HudDiskConfigV2)) {
      HudDiskConfigV3 next;
      valid = prefs_.getBytes("config", &next, sizeof next) == sizeof next &&
          (next.base.version == 2 || next.base.version == 3) &&
          hudCrc(reinterpret_cast<const uint8_t *>(&next), sizeof next - 4) == next.crc &&
          next.readyHigh <= 1 && next.simulationEnabled <= (next.base.version == 3 ? 1 : 0);
      simulationEnabled = next.base.version == 3 && next.simulationEnabled;
      d = next.base; d.version = 1;
      link.baud=next.baud; link.intervalMs=next.intervalMs;
      link.readyHigh=next.readyHigh;
    }
    if (!valid || !link.valid() ||
        d.version != 1 || d.role > 1 || d.wifi > 2 || d.uart > 1 || !d.port || d.port > 65535 ||
        d.reserved) {
      hud.config.role = DeviceRole::Hud;
      hud.config.simulationEnabled = false;
      if (hud.config.selected == HUD_SIMULATION_ID) hud.config.selected = 0;
      hud.catalogError = true;
    } else {
      hud.config.role = DeviceRole(d.role);
      hud.config.wifi = HudWifi(d.wifi);
      hud.config.uart = d.uart;
      hud.config.port = uint16_t(d.port);
      hud.config.selected = uint64_t(d.selectedLo) | (uint64_t(d.selectedHi) << 32);
      hud.config.link = link;
      hud.config.simulationEnabled = simulationEnabled;
      if (!simulationEnabled && hud.config.selected == HUD_SIMULATION_ID) hud.config.selected = 0;
    }
  }
  const size_t size = peers && opened && hud.isHud() ? prefs_.getBytesLength("peers") : 0;
  if (size >= 8 && (size - 8) % sizeof(HudDiskPeer) == 0) {
    auto data =
        std::unique_ptr<uint8_t, decltype(&std::free)>(static_cast<uint8_t *>(std::malloc(size)), &std::free);
    if (!data || prefs_.getBytes("peers", data.get(), size) != size)
      hud.catalogError = true;
    else {
      uint32_t version, crc;
      std::memcpy(&version, data.get(), 4);
      std::memcpy(&crc, data.get() + size - 4, 4);
      if ((version < 1 || version > 3) || crc != hudCrc(data.get(), size - 4))
        hud.catalogError = true;
      else
        for (size_t at = 4; at < size - 4; at += sizeof(HudDiskPeer)) {
          HudDiskPeer d;
          std::memcpy(&d, data.get() + at, sizeof d);
          const auto id = uint64_t(d.idLo) | (uint64_t(d.idHi) << 32);
          const unsigned background = version == 1 ? 0 : d.background;
          auto *peer = d.uart <= (version >= 3 ? 2 : 1) && !d.label[48] && validHudBackground(background)
              ? hud.add(id, {d.address, d.port, d.uart==1, d.uart==2}, d.label) : nullptr;
          if (!peer)
            hud.catalogError = true;
          else
            peer->background = uint8_t(background);
        }
    }
  } else if (size)
    hud.catalogError = true;
  prefs_.end();
  // Even an invalid saved role fails closed before any motor/ADC setup.
  runtime.controller.remote = hud.isHud() ? &hud : nullptr;
}
} // namespace forkrot

#if MQTT_SUPPORT
namespace forkrot {
bool Storage::loadMqtt(MqttConfig &config) {
  if(!prefs_.begin("mqtt",false)){prefs_.end();return false;}
  if(!prefs_.isKey("config1")){prefs_.end();config=MqttConfig{};return true;}
  MqttRecord bytes{};
  const bool read=prefs_.getBytesLength("config1")==bytes.size() && prefs_.getBytes("config1",bytes.data(),bytes.size())==bytes.size();
  prefs_.end();return read && decodeMqtt(bytes,config);
}
bool Storage::saveMqtt(const MqttConfig &config) {
  if(config.validate())return false;
  const auto bytes=encodeMqtt(config);MqttRecord verify{};
  if(!prefs_.begin("mqtt",false)){prefs_.end();return false;}
  const bool ok=prefs_.putBytes("config1",bytes.data(),bytes.size())==bytes.size() &&
      prefs_.getBytesLength("config1")==verify.size() && prefs_.getBytes("config1",verify.data(),verify.size())==verify.size() && bytes==verify;
  prefs_.end();return ok;
}
}
#endif
