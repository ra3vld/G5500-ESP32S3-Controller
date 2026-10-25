// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "modbus_config.hpp"
#include "api.hpp"
#include "hud.hpp"
#include "setup_state.hpp"
namespace forkrot {
// The single control model plus the last hardware/UI projection. Peripheral
// buffers and settings belong to their respective services, not this object.
struct Runtime {
  Controller controller;
  SetupState setup;
  bool setupStorageOk = true;
  // Editing survives network delays; only an explicitly armed motion test expires.
  static constexpr uint32_t SetupTestTimeoutMs = 1500;
  uint32_t setupLease = 0, setupRenewed = 0, setupTestToken = 0, setupTestEpoch = 0;
  bool setupSaving = false, setupRestartRequired = false;
  ModbusConfig modbus;
  ModbusStatus modbusStatus;
  HardwareRecord hardwareRecord;
  RecoveryHistory recoveryHistory;
  MemoryGuard memoryGuard;
  bool recovery = false, hardwareTrial = false, hardwareProbe = false;
  bool hardwareHudUart = false, hardwareHudRole = false;
  uint32_t hardwareToken = 0;
  RecoveryReason recoveryReason = RecoveryReason::None;
  Protocol protocol{controller};
  Settings loggerSettings;
  Hud hud{controller, protocol};
  bool configuredAsHud() const { return recovery ? hardwareHudRole : hud.isHud(); }
  float currentAz = 0, currentEl = 0, targetAz = 0, targetEl = 0;
  bool targetValid = false, azTrack = false, elTrack = false;
  uint32_t lastTargetRxMs = 0;
};
} // namespace forkrot
