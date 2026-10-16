#pragma once
#include "build_config.hpp"
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include <lvgl.h>
#include <cstdint>
#include "crowpanel_touch.hpp"
namespace forkrot {
struct CrowPanelView {
  bool dualAxis = false;
  bool elevation = false, feedback = false, targetValid = false;
  bool pending = false, moving = false, autoMode = false, autoHour = false;
  int direction = 0;
  float current = 0, target = 0;
  bool wifiConnected = false, wifiAp = false;
  int wifiRssi = -127;
  bool bluetoothEnabled = false, bluetoothConnected = false;
  char ip[40] = "IP: —", heap[48] = "Heap: —";
  char status[80] = "Привод не подключён";
};
class CrowPanelUi {
  lv_obj_t *mode_ = nullptr, *axis_ = nullptr, *angle_ = nullptr;
  lv_obj_t *target_ = nullptr, *status_ = nullptr, *wifi_ = nullptr;
  lv_obj_t *wifiSlash_ = nullptr, *wifiAp_ = nullptr;
  lv_obj_t *bluetooth_ = nullptr, *ip_ = nullptr, *heap_ = nullptr;
  lv_obj_t *modeButton_ = nullptr, *stopButton_ = nullptr;
  lv_obj_t *currentMarker_ = nullptr, *targetMarker_ = nullptr;
  lv_obj_t *motionArrow_ = nullptr;
public:
  void create();
  void update(const CrowPanelView &view);
  void setPressed(crowpanel::TouchAction action);
};
}
#endif
