// G5500 Fork; GPL-3.0.
#pragma once
#include "pin_map.hpp"
#include "position_sensor_config.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
namespace forkrot {
inline constexpr const char *localSettingNames[] = {"localInputMode", "localAzInvert", "localElInvert",
  "localAzMin", "localAzCenter", "localAzMax", "localElMin", "localElCenter", "localElMax", "localDeadzone", "localHysteresis"};
using LocalSettings = std::array<uint16_t,11>;
struct HardwareConfig {
  SensorSettings sensors{0,6,0,0,6,0};
  unsigned sensorType(unsigned axis) const { return sensors[axis*3]; }
  unsigned sensorAddress(unsigned axis) const { return sensors[axis*3+1]; }
  unsigned sensorZero(unsigned axis) const { return sensors[axis*3+2]; }
  bool sensorInverted(unsigned axis) const { return sensors[6+axis]!=0; }
  LocalSettings local{LOCAL_INPUT_MODE, LOCAL_AZ_INVERT, LOCAL_EL_INVERT,
    LOCAL_AZ_ADC_MIN, LOCAL_AZ_ADC_CENTER, LOCAL_AZ_ADC_MAX,
    LOCAL_EL_ADC_MIN, LOCAL_EL_ADC_CENTER, LOCAL_EL_ADC_MAX, LOCAL_ADC_DEADZONE, LOCAL_ADC_HYSTERESIS};
  uint8_t localMask() const {
    return ((local[0]&LOCAL_INPUT_BUTTONS) ? (pins.localAzNegative>=0?1:0)|(pins.localElNegative>=0?2:0) : 0) |
           ((local[0]&LOCAL_INPUT_JOYSTICK) ? (pins.localAzAdc>=0?1:0)|(pins.localElAdc>=0?2:0) : 0);
  }
  PinMap pins;
  uint8_t drive = DRIVE_INTERFACE;
  bool simulation = DRIVE_SIMULATION;
  bool bluetooth = BLUETOOTH_ENABLED;
  uint8_t bluetoothMode = BLUETOOTH_MODE;
  bool forceAp = ESP32_FORCE_AP_STA;
  int8_t buttonGpio = USER_BUTTON_GPIO;
  bool buttonLow = USER_BUTTON_ACTIVE_LOW, buttonPullup = USER_BUTTON_PULLUP;
  bool pwmInverted = BTS7960_PWM_INVERTED;
  void releaseSimulationPins() {
    if(!simulation) return;
    for(const auto &d:pinDefinitions)
      if(d.group==PinGroup::Drive || d.group==PinGroup::SensorAz || d.group==PinGroup::SensorEl)
        pins.*d.member=-1;
  }
  // BTS retains its persisted speed/enable role; other PWM modes use a separate optional pin.
  int enablePin(unsigned axis) const {
    return drive==DRIVE_BTS7960 ? (axis?pins.elSpeedEnable:pins.azSpeedEnable)
        : separatePwm() ? (axis?pins.elEnable:pins.azEnable) : -1;
  }
  bool localAxisConfigured(unsigned axis) const {
    return axis ? pins.elAdc>=0 || (drive!=DRIVE_DIR_PWM && pins.elNegative>=0) || pins.elPositive>=0 || (drive!=DRIVE_BTS7960 && pins.elSpeedEnable>=0) || sensorType(1)
                : pins.azAdc>=0 || (drive!=DRIVE_DIR_PWM && pins.azNegative>=0) || pins.azPositive>=0 || (drive!=DRIVE_BTS7960 && pins.azSpeedEnable>=0) || sensorType(0);
  }
  bool singleAxis() const {
    return pins.elAdc<0 && (drive==DRIVE_DIR_PWM || pins.elNegative<0) && pins.elPositive<0 && (drive==DRIVE_BTS7960 || pins.elSpeedEnable<0);
  }
  bool separatePwm() const { return drive==DRIVE_SEPARATE_PWM || drive==DRIVE_DIR_PWM; }
  bool hasDirections(unsigned axis) const {
    return axis ? (drive==DRIVE_DIR_PWM || pins.elNegative>=0) && pins.elPositive>=0
                : (drive==DRIVE_DIR_PWM || pins.azNegative>=0) && pins.azPositive>=0;
  }
  bool manualWithoutFeedback() const {
    return !simulation && drive==DRIVE_YAESU && hasDirections(0) && pins.azSpeedEnable<0 &&
        pins.azAdc<0 && !sensorType(0) && (singleAxis() || (hasDirections(1) && pins.elAdc<0 && !sensorType(1)));
  }
  bool hudRs485() const { return pins.hudRx >= 0 && pins.hudTx >= 0 && pins.hudDe >= 0; }
  bool rs485() const { return pins.uartDe >= 0; }
  uint8_t backend() const {
    return (!BLUETOOTH_SUPPORT || !bluetooth || (FORK_CLASSIC_ESP32 && bluetoothMode==BT_ESP32_SPP && !BLUETOOTH_SPP_SUPPORT)) ? BT_NONE : bluetoothMode == BT_AUTO
        ? ((FORK_CLASSIC_ESP32 && BLUETOOTH_SPP_SUPPORT) ? BT_ESP32_SPP : BT_HC05) : bluetoothMode;
  }
  const char *backendName() const {
    return backend() == BT_ESP32_SPP ? "ESP32_SPP" : backend() == BT_HC05 ? "HC05" : "NONE";
  }
  bool operator==(const HardwareConfig &b) const {
    return sensors == b.sensors && local == b.local && drive == b.drive && simulation == b.simulation && bluetooth == b.bluetooth &&
        bluetoothMode == b.bluetoothMode && forceAp == b.forceAp && buttonGpio == b.buttonGpio &&
        buttonLow == b.buttonLow && buttonPullup == b.buttonPullup && pwmInverted == b.pwmInverted && pins == b.pins;
  }
  bool operator!=(const HardwareConfig &b) const { return !(*this == b); }
};
// Exact calibration identity; changing sensor/wiring/zero cannot reuse ADC calibration.
inline uint64_t positionSensorIdentity(const HardwareConfig &c,unsigned axis) {
  if(!c.sensorType(axis)) return uint64_t(c.sensorInverted(axis))<<48; // preserve existing potentiometer records
  return uint64_t(c.sensorType(axis)) | (uint64_t(c.sensorAddress(axis))<<8) |
      (uint64_t(c.sensorZero(axis))<<16) |
      (uint64_t(uint8_t(axis?c.pins.elSensorSda:c.pins.azSensorSda))<<32) |
      (uint64_t(uint8_t(axis?c.pins.elSensorScl:c.pins.azSensorScl))<<40) |
      (uint64_t(c.sensorInverted(axis))<<48);
}
enum class ConfigPhase : uint8_t { Confirmed, Pending, Testing, Recovery, Starting };
enum class RecoveryReason : uint8_t { None, Unconfirmed, LowMemory, Allocation, Radio, Storage, Invalid, Requested, Initial };
inline const char *recoveryReasonName(RecoveryReason r) {
  switch (r) {
  case RecoveryReason::Unconfirmed: return "unconfirmed";
  case RecoveryReason::LowMemory: return "low_memory";
  case RecoveryReason::Allocation: return "allocation_failed";
  case RecoveryReason::Radio: return "radio_failed";
  case RecoveryReason::Storage: return "storage_failed";
  case RecoveryReason::Invalid: return "invalid_config";
  case RecoveryReason::Requested: return "requested";
  case RecoveryReason::Initial: return "initial_setup";
  default: return "none";
  }
}
inline bool automaticRecoveryReason(RecoveryReason r) {
  return r == RecoveryReason::Unconfirmed || r == RecoveryReason::LowMemory ||
      r == RecoveryReason::Allocation || r == RecoveryReason::Radio || r == RecoveryReason::Storage;
}
struct RecoveryHistory {
  static constexpr uint8_t Limit = 3;
  static constexpr uint32_t StableMs = 30u * 60u * 1000u;
  uint8_t failures = 0;
  bool pending = false; // this fault was already counted before restarting
  RecoveryReason last = RecoveryReason::None;
  void record(RecoveryReason reason) {
    if (failures < Limit) ++failures;
    pending = true; last = reason;
  }
  bool due() const { return failures >= Limit; }
  bool operator==(const RecoveryHistory &o) const {
    return failures == o.failures && pending == o.pending && last == o.last;
  }
};
// Separate from the hardware record so old hardware settings remain readable.
using RecoveryBytes = std::array<uint8_t, 12>;
inline RecoveryBytes encodeRecovery(const RecoveryHistory &r) {
  RecoveryBytes b{'R','F',1,r.failures,uint8_t(r.pending),uint8_t(r.last)};
  uint32_t crc = 2166136261u;
  for (unsigned i=0; i<8; ++i) crc = (crc ^ b[i]) * 16777619u;
  for (unsigned i=0; i<4; ++i) b[8+i] = uint8_t(crc >> (8*i));
  return b;
}
inline bool decodeRecovery(const RecoveryBytes &b, RecoveryHistory &r) {
  if (b[0]!='R' || b[1]!='F' || b[2]!=1 || b[3]>RecoveryHistory::Limit || b[4]>1 ||
      b[5]>uint8_t(RecoveryReason::Initial) || (!b[3] && (b[4] || b[5])) ||
      (b[3] && !automaticRecoveryReason(RecoveryReason(b[5])))) return false;
  RecoveryHistory next; next.failures=b[3];next.pending=b[4];next.last=RecoveryReason(b[5]);
  if (encodeRecovery(next)!=b) return false;
  r=next;return true;
}
struct HardwareRecord {
  HardwareConfig good{}, candidate{};
  uint32_t generation = 1;
  uint32_t probeCounter = 0;
  bool hasConfirmed = false;
  ConfigPhase phase = ConfigPhase::Confirmed;
  RecoveryReason reason = RecoveryReason::None;
};
// Explicit wire format: no structure padding, compiler ABI or unchecked enums in NVS.
using LegacyHardwareBytes = std::array<uint8_t,40>;
using PinHardwareBytes = std::array<uint8_t,98>; // version 2, 29 GPIO roles
using LocalHardwareBytes = std::array<uint8_t,154>; // version 3, 35 GPIO roles
using UartHardwareBytes = std::array<uint8_t,162>; // version 4, 39 GPIO roles
using SensorHardwareBytes = std::array<uint8_t,194>; // version 5, 43 GPIO roles
using RadioHardwareBytes = std::array<uint8_t,196>; // version 6, 44 GPIO roles
using InvertedSensorHardwareBytes = std::array<uint8_t,204>; // version 7, sensor inversion
using HardwareBytes = std::array<uint8_t,40 + pinDefinitions.size()*2 + LocalSettings{}.size()*4 + SensorSettings{}.size()*4>;
template<size_t N> inline std::array<uint8_t,N> encodeHardwareRecord(const HardwareRecord &r) {
  static_assert(N==40 || N==98 || N==154 || N==162 || N==194 || N==196 || N==204 || N==HardwareBytes{}.size(), "Unknown hardware format");
  std::array<uint8_t,N> b{};
  b[0]='H'; b[1]='W'; b[2]=N==40?1:N==98?2:N==154?3:N==162?4:N==194?5:N==196?6:N==204?7:8; b[3]=SELECTED_BOARD; b[4]=DISPLAY_KIND;
  b[5]=uint8_t(r.phase); b[6]=uint8_t(r.reason); b[7]=r.hasConfirmed;
  for(unsigned i=0;i<4;++i) b[8+i]=uint8_t(r.generation>>(8*i));
  auto put=[&](unsigned at,const HardwareConfig &c) {
    b[at]=c.drive; b[at+1]=c.simulation; b[at+2]=c.bluetooth; b[at+3]=c.bluetoothMode;
    b[at+4]=c.forceAp; b[at+5]=uint8_t(c.buttonGpio); b[at+6]=c.buttonLow;
    b[at+7]=c.buttonPullup; b[at+8]=c.pwmInverted;
  };
  put(12,r.good); put(21,r.candidate);
  for(unsigned i=0;i<4;++i) b[30+i]=uint8_t(r.probeCounter>>(8*i));
  constexpr size_t count=N==40?0:N==98?29:N==154?35:N==162?39:N==194?43:(N==196 || N==204)?44:pinDefinitions.size();
  if constexpr(N!=40) for(size_t i=0;i<count;++i) {
    b[36+i]=uint8_t(r.good.pins.*pinDefinitions[i].member);
    b[36+count+i]=uint8_t(r.candidate.pins.*pinDefinitions[i].member);
  }
  if constexpr(N!=40 && N!=98) for(unsigned c=0;c<2;++c) for(size_t i=0;i<LocalSettings{}.size();++i) {
    const auto value=(c?r.candidate:r.good).local[i];
    const auto at=36+count*2+(c*LocalSettings{}.size()+i)*2;
    b[at]=uint8_t(value); b[at+1]=uint8_t(value>>8);
  }
  if constexpr(N==194 || N==196 || N==204 || N==HardwareBytes{}.size()) for(unsigned c=0;c<2;++c) for(size_t i=0;i<(N==194 || N==196?6:SensorSettings{}.size());++i) {
    const auto value=(c?r.candidate:r.good).sensors[i];
    const auto at=36+count*2+LocalSettings{}.size()*4+(c*(N==194 || N==196?6:SensorSettings{}.size())+i)*2;
    b[at]=uint8_t(value);b[at+1]=uint8_t(value>>8);
  }
  uint32_t crc=2166136261u;
  for(size_t i=0;i<N-4;++i) crc=(crc^b[i])*16777619u;
  for(unsigned i=0;i<4;++i) b[N-4+i]=uint8_t(crc>>(8*i));
  return b;
}
inline HardwareBytes encodeHardware(const HardwareRecord &r) { return encodeHardwareRecord<HardwareBytes{}.size()>(r); }
template<size_t N> inline bool decodeHardware(const std::array<uint8_t,N> &b,HardwareRecord &r) {
  if(b[0]!='H' || b[1]!='W' || b[2]!=(N==40?1:N==98?2:N==154?3:N==162?4:N==194?5:N==196?6:N==204?7:8) || b[3]!=SELECTED_BOARD || b[4]!=DISPLAY_KIND ||
      b[5]>uint8_t(ConfigPhase::Starting) || b[6]>uint8_t(RecoveryReason::Initial) || b[7]>1) return false;
  HardwareRecord next;
  next.phase=ConfigPhase(b[5]); next.reason=RecoveryReason(b[6]); next.hasConfirmed=b[7];
  next.generation=next.probeCounter=0;
  for(unsigned i=0;i<4;++i) { next.generation|=uint32_t(b[8+i])<<(8*i); next.probeCounter|=uint32_t(b[30+i])<<(8*i); }
  if(!next.generation) return false;
  auto get=[&](unsigned at,HardwareConfig &c) {
    for(unsigned i:{1u,2u,4u,6u,7u,8u}) if(b[at+i]>1) return false;
    c.drive=b[at]; c.simulation=b[at+1]; c.bluetooth=b[at+2]; c.bluetoothMode=b[at+3];
    c.forceAp=b[at+4]; c.buttonGpio=int8_t(b[at+5]); c.buttonLow=b[at+6];
    c.buttonPullup=b[at+7]; c.pwmInverted=b[at+8]; return true;
  };
  if(!get(12,next.good) || !get(21,next.candidate)) return false;
  constexpr size_t count=N==40?0:N==98?29:N==154?35:N==162?39:N==194?43:(N==196 || N==204)?44:pinDefinitions.size();
  if constexpr(N!=40) for(size_t i=0;i<count;++i) {
    next.good.pins.*pinDefinitions[i].member=int8_t(b[36+i]);
    next.candidate.pins.*pinDefinitions[i].member=int8_t(b[36+count+i]);
  }
  if constexpr(N!=40 && N!=98) for(unsigned c=0;c<2;++c) for(size_t i=0;i<LocalSettings{}.size();++i) {
    const auto at=36+count*2+(c*LocalSettings{}.size()+i)*2;
    (c?next.candidate:next.good).local[i]=uint16_t(b[at])|(uint16_t(b[at+1])<<8);
  }
  if constexpr(N==194 || N==196 || N==204 || N==HardwareBytes{}.size()) for(unsigned c=0;c<2;++c) for(size_t i=0;i<(N==194 || N==196?6:SensorSettings{}.size());++i) {
    const auto at=36+count*2+LocalSettings{}.size()*4+(c*(N==194 || N==196?6:SensorSettings{}.size())+i)*2;
    (c?next.candidate:next.good).sensors[i]=uint16_t(b[at])|(uint16_t(b[at+1])<<8);
  }
  if(encodeHardwareRecord<N>(next)!=b) return false;
  if constexpr(N==40) {
    // Old CrowPanel firmware gave the external UART exclusively to HC-05.
    if(SELECTED_BOARD==BOARD_CROWPANEL_21) for(auto *c:{&next.good,&next.candidate}) {
      c->pins.uartRx=c->backend()==BT_HC05?-1:board::PIN_UART_RX;
      c->pins.uartTx=c->backend()==BT_HC05?-1:board::PIN_UART_TX;
    }
  }
  r=next; return true;
}
// Hysteresis preserves enough internal byte-addressable RAM for settings POSTs,
// NVS, TCP and the parser-free recovery page. Values are conservative guardrails,
// not a claim that every workload above them is physically validated.
struct MemoryGuard {
  static constexpr uint32_t HealthyFree = 32768, HealthyBlock = 12288;
  static constexpr uint32_t CriticalFree = 16384, CriticalBlock = 6144;
  static constexpr uint32_t TrialMs = 300000, HealthyMs = 10000, LowMs = 2000;
  uint32_t started = 0, healthyAt = 0, lowAt = 0, failures = 0;
  bool healthy = false, low = false;
  void begin(uint32_t now, uint32_t allocFailures) { *this = {}; started=now; failures=allocFailures; }
  RecoveryReason sample(uint32_t now, uint32_t free, uint32_t block, uint32_t allocFailures, bool trial) {
    if (allocFailures != failures) return RecoveryReason::Allocation;
    const bool enough = free >= HealthyFree && block >= HealthyBlock;
    if (!enough) healthy = false;
    else if (!healthy) { healthy = true; healthyAt = now; }
    if (free < CriticalFree || block < CriticalBlock) {
      if (!low) { low = true; lowAt = now; }
      if (uint32_t(now-lowAt) >= LowMs) return RecoveryReason::LowMemory;
    } else low = false;
    return trial && uint32_t(now-started) >= TrialMs ? RecoveryReason::Unconfirmed : RecoveryReason::None;
  }
  bool ready(uint32_t now) const { return healthy && !low && uint32_t(now-healthyAt) >= HealthyMs; }
};
} // namespace forkrot
