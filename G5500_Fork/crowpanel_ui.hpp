#pragma once
#include "build_config.hpp"
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include <lvgl.h>
#include <cstdint>
#include "crowpanel_touch.hpp"
#include "crowpanel_view.hpp"
namespace forkrot {
class CrowPanelUi {
  lv_font_t speedFont_{};
  lv_obj_t *speedPanel_ = nullptr, *speedBars_[2]{}, *speedValues_[2]{}, *speedAxes_[2]{}, *speedBack_ = nullptr;
  lv_obj_t *hudName_ = nullptr, *hudAddress_ = nullptr, *hudList_ = nullptr, *hudRows_[3]{};
  lv_obj_t *mode_ = nullptr, *axis_ = nullptr, *range_ = nullptr, *angle_ = nullptr;
  lv_obj_t *targetCaption_ = nullptr, *target_ = nullptr, *status_ = nullptr, *wifi_ = nullptr;
  lv_obj_t *wifiSlash_ = nullptr, *wifiAp_ = nullptr;
  lv_obj_t *bluetooth_ = nullptr, *ip_ = nullptr, *heap_ = nullptr;
  lv_obj_t *modeButton_ = nullptr;
  lv_obj_t *currentMarker_ = nullptr, *targetMarker_ = nullptr;
  lv_obj_t *arrows_[4]{}, *secondaryAngle_ = nullptr, *secondaryTarget_ = nullptr;
  crowpanel::TouchLayout touchLayout_{};
  struct MarkerMotion {
    lv_obj_t *object = nullptr;
    float shown = 0, requested = 0;
    int size = 0;
    bool valid = false;
  } currentMotion_, targetMotion_;
  lv_obj_t *limits_ = nullptr;
  uint8_t lowerLimit_ = 0, upperLimit_ = 0;
  uint8_t targetLowerLimit_ = 0, targetUpperLimit_ = 0;
  float minimum_ = 0, maximum_ = 450;
  uint64_t sourceId_ = 0;
  bool elevation_ = false, confirmationVisible_ = false;
  crowpanel::TouchRect confirmationCaption_{}, confirmationAngle_{};
  static void animateMarker(void *motion, int32_t value);
  static void updateMarker(MarkerMotion &motion, float degrees, bool valid, bool reset, bool shortest);
  static void drawLimits(lv_event_t *event);
public:
  const crowpanel::TouchLayout &touchLayout() const {
    return touchLayout_;
  }
  void create();
  void update(const CrowPanelView &view);
  void setPressed(crowpanel::TouchAction action);
  bool confirmationHit(uint16_t x, uint16_t y) const {
    return confirmationVisible_ && (confirmationCaption_.contains(x, y) || confirmationAngle_.contains(x, y));
  }
};
}
#endif
