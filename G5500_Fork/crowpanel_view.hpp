// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
struct CrowPanelView {
  bool hud = false, hudSelector = false;
  char hudName[49]{}, hudAddress[32] = "hud:—", hudRows[3][128]{};
  bool dualAxis = false, speedScreen = false;
  bool speedAvailable[2]{};
  unsigned speedPercent[2]{5, 5};
  bool elevation = false, feedback = false, targetValid = false;
  bool uncalibrated = false, secondaryUncalibrated = false;
  bool pending = false, moving = false, autoMode = false, autoHour = false;
  int direction = 0;
  float current = 0, target = 0, minimum = 0, maximum = 450;
  float secondaryCurrent = 0, secondaryTarget = 0;
  bool secondaryFeedback = false, secondaryTargetValid = false, secondaryPending = false;
  int azDirection = 0, elDirection = 0;
  uint64_t sourceId = 0;
  bool wifiConnected = false, wifiAp = false;
  int wifiRssi = -127;
  bool bluetoothEnabled = false, bluetoothConnected = false;
  char ip[64] = "IP: —", heap[48] = "Heap: —";
  bool statusFault = false;
  char status[384] = "Привод не подключён";
};
}
