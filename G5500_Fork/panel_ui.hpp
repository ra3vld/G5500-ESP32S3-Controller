// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config.hpp"
#if GRAPHICAL_DISPLAY
#include <lvgl.h>
#include <cstdint>
#include <array>
#include "panel_input.hpp"
#include "panel_view.hpp"
#include "panel_connection.hpp"
namespace forkrot {
class PanelUi {
  PanelView previous_;
  bool previousValid_=false;
  uint32_t actionEpoch_=0;
  uint32_t background_=0x0a111c;
  int width_=480,height_=480,diameter_=480;
  bool round_=true;
  int p(int value) const { return (value*diameter_+240)/480; }
  int x(int value) const { return (width_-diameter_)/2+p(value); }
  int y(int value) const { return (height_-diameter_)/2+p(value); }
  const lv_font_t *face(int size) const;
  lv_obj_t *label(int top,int width,int size,uint32_t color);
  lv_obj_t *button(panel::TouchRect rect,uint32_t color);
  panel::TouchRect bounds(lv_obj_t *object,int padding=0) const;
  panel::TouchRect rect(int left,int top,int width,int height) const;
  lv_obj_t *settingsButton_=nullptr,*actionButton_=nullptr,*actionLabel_=nullptr;
  lv_obj_t *root_=nullptr;
  void configureLayout(const PanelView &view);
  void layoutCurrent(const PanelView &view);
  void updateConnectionIndicator(const PanelView &,panel::ConnectionWarning);
  panel::ConnectionIndicator connectionIndicator_;
  panel::ConnectionWarning connectionWarning_=panel::ConnectionWarning::None;
  std::array<lv_font_t,13> currentFonts_{};
  std::array<lv_font_t,2> secondaryFonts_{};
  unsigned currentFontCount_=0;
  bool currentLayoutValid_=false;
  uint64_t currentLayoutKey_=0;
  panel::TouchRect currentRegion_{},currentArrows_[4]{};
  lv_point_precise_t arrowPoints_[4][5]{},wifiSlashPoints_[2]{};
  bool arrowMoving_[4]{};
  int compactArrowDirection_=-1;
  void positionMarker(lv_obj_t *object,float degrees,bool valid,int size);
  void invalidateLimit(float boundary,bool lower,bool target);
  void updateLimitBand(std::array<uint8_t,180> &band,uint8_t lower,uint8_t upper);

  lv_font_t speedFont_{};
  lv_obj_t *speedPanel_ = nullptr, *speedBars_[2]{}, *speedValues_[2]{}, *speedAxes_[2]{}, *speedBack_ = nullptr;
  lv_obj_t *hudName_ = nullptr, *hudAddress_ = nullptr, *hudList_ = nullptr, *hudRows_[3]{};
  lv_obj_t *mode_ = nullptr, *axis_ = nullptr, *range_ = nullptr, *angle_ = nullptr;
  lv_obj_t *targetCaption_ = nullptr, *target_ = nullptr, *status_ = nullptr, *wifi_ = nullptr;
  lv_obj_t *wifiSlash_ = nullptr, *wifiAp_ = nullptr, *mqtt_ = nullptr;
  lv_obj_t *bluetooth_ = nullptr, *ip_ = nullptr, *heap_ = nullptr;
  lv_obj_t *modeButton_ = nullptr;
  lv_obj_t *currentMarker_ = nullptr, *targetMarker_ = nullptr;
  lv_obj_t *arrows_[4]{}, *secondaryAngle_ = nullptr, *secondaryTarget_ = nullptr;
  panel::TouchLayout touchLayout_{};
  struct MarkerMotion {
    PanelUi *owner=nullptr;
    lv_obj_t *object = nullptr;
    float shown = 0, requested = 0;
    int size = 0;
    bool valid = false;
  } currentMotion_, targetMotion_;
  lv_obj_t *limits_ = nullptr;
  uint8_t lowerLimit_ = 0, upperLimit_ = 0;
  uint8_t targetLowerLimit_ = 0, targetUpperLimit_ = 0;
  std::array<uint8_t,180> limitBand_{}, targetLimitBand_{};
  float minimum_ = 0, maximum_ = 450;
  uint64_t sourceId_ = 0;
  bool elevation_ = false, confirmationVisible_ = false;
  panel::TouchRect confirmationCaption_{}, confirmationAngle_{};
  static void animateMarker(void *motion, int32_t value);
  static void updateMarker(MarkerMotion &motion, float degrees, bool valid, bool reset, bool shortest);
  static void drawLimits(lv_event_t *event);
public:
  const panel::TouchLayout &touchLayout() const {
    return touchLayout_;
  }
  void create(bool round=true);
  lv_obj_t *root() const { return root_; }
  void update(const PanelView &view);
  void setPressed(panel::TouchAction action);
  bool confirmationHit(uint16_t x, uint16_t y) const {
    return confirmationVisible_ && (confirmationCaption_.contains(x, y) || confirmationAngle_.contains(x, y));
  }
};
using CrowPanelUi = PanelUi;
}
#endif
