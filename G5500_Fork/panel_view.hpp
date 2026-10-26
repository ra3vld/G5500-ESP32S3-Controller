// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "display_settings.hpp"
#include <cstdint>
#include <cstring>
#include <tuple>
namespace forkrot {
struct PanelView {
  uint8_t visible=defaultDisplayFields(DISPLAY_KIND);
  bool shows(DisplayField field) const {return visible&field;}
  bool movingNow=false, settingsAvailable=false, enabled=true;
  bool hud = false, hudSelector = false;
  uint8_t hudBackground = 0;
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
  bool operator==(const PanelView &v) const {
    return std::tie(movingNow,settingsAvailable,enabled,hud,hudSelector,dualAxis,speedScreen,elevation,feedback,targetValid,uncalibrated,secondaryUncalibrated,pending,moving,autoMode,autoHour,direction,current,target,minimum,maximum,secondaryCurrent,secondaryTarget,secondaryFeedback,secondaryTargetValid,secondaryPending,azDirection,elDirection,sourceId,wifiConnected,wifiAp,wifiRssi,bluetoothEnabled,bluetoothConnected,statusFault) ==
           std::tie(v.movingNow,v.settingsAvailable,v.enabled,v.hud,v.hudSelector,v.dualAxis,v.speedScreen,v.elevation,v.feedback,v.targetValid,v.uncalibrated,v.secondaryUncalibrated,v.pending,v.moving,v.autoMode,v.autoHour,v.direction,v.current,v.target,v.minimum,v.maximum,v.secondaryCurrent,v.secondaryTarget,v.secondaryFeedback,v.secondaryTargetValid,v.secondaryPending,v.azDirection,v.elDirection,v.sourceId,v.wifiConnected,v.wifiAp,v.wifiRssi,v.bluetoothEnabled,v.bluetoothConnected,v.statusFault) &&
      hudBackground==v.hudBackground && visible==v.visible &&
      speedAvailable[0]==v.speedAvailable[0] && speedAvailable[1]==v.speedAvailable[1] && speedPercent[0]==v.speedPercent[0] && speedPercent[1]==v.speedPercent[1] &&
      std::strcmp(hudName,v.hudName)==0 && std::strcmp(hudAddress,v.hudAddress)==0 && std::strcmp(ip,v.ip)==0 && std::strcmp(heap,v.heap)==0 && std::strcmp(status,v.status)==0 &&
      std::strcmp(hudRows[0],v.hudRows[0])==0 && std::strcmp(hudRows[1],v.hudRows[1])==0 && std::strcmp(hudRows[2],v.hudRows[2])==0;
  }
};
using CrowPanelView = PanelView;
}
