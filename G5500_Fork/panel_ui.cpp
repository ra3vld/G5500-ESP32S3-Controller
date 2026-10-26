// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "panel_ui.hpp"
#if GRAPHICAL_DISPLAY
#include "crowpanel_dial.hpp"
#include "hud_background.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <initializer_list>
namespace forkrot {
namespace {
constexpr uint32_t bg = 0x0a111c, text = 0xeff5ff, muted = 0x8fabc3;
constexpr uint32_t targetColor = 0x64cfff;
constexpr uint32_t wifiWarning = 0xffd54f, wifiDisconnected = 0xff6b6b;
constexpr int weakWifiRssi = -80;
constexpr lv_point_precise_t wifiSlash[] = {{2, 2}, {26, 26}};
constexpr int dialRadius = 227;
constexpr int currentMarkerSize = 20, targetMarkerSize = 26;
constexpr lv_point_precise_t arrowLeft[] = {{25, 7}, {12, 18}, {25, 29}};
constexpr lv_point_precise_t arrowRight[] = {{11, 7}, {24, 18}, {11, 29}};
constexpr lv_point_precise_t arrowDown[] = {{7, 11}, {18, 24}, {29, 11}};
constexpr lv_point_precise_t arrowUp[] = {{7, 25}, {18, 12}, {29, 25}};
constexpr lv_point_precise_t movingLeft[] = {{25, 7}, {12, 18}, {25, 29}, {12, 18}, {30, 18}};
constexpr lv_point_precise_t movingRight[] = {{11, 7}, {24, 18}, {11, 29}, {24, 18}, {6, 18}};
constexpr lv_point_precise_t movingUp[] = {{7, 25}, {18, 12}, {29, 25}, {18, 12}, {18, 30}};
constexpr lv_point_precise_t movingDown[] = {{7, 11}, {18, 24}, {29, 11}, {18, 24}, {18, 6}};
constexpr const lv_point_precise_t *idleArrows[] = {arrowLeft, arrowRight, arrowUp, arrowDown};
constexpr const lv_point_precise_t *movingArrows[] = {movingLeft, movingRight, movingUp, movingDown};
void set(lv_obj_t *object, const char *value) {
  if (std::strcmp(lv_label_get_text(object), value)) lv_label_set_text(object, value);
}
void color(lv_obj_t *object, uint32_t value) {
  if (lv_color_to_u32(lv_obj_get_style_text_color(object, LV_PART_MAIN)) != lv_color_to_u32(lv_color_hex(value)))
    lv_obj_set_style_text_color(object, lv_color_hex(value), 0);
}
void font(lv_obj_t *object, const lv_font_t *value) {
  if (lv_obj_get_style_text_font(object, LV_PART_MAIN) != value)
    lv_obj_set_style_text_font(object, value, 0);
}
lv_obj_t *buttonLabel(lv_obj_t *parent,const lv_font_t *face) {
  auto *object = lv_label_create(parent);
  lv_obj_set_style_text_font(object, face, 0);
  lv_obj_set_style_text_color(object, lv_color_hex(text), 0);
  lv_obj_center(object);
  return object;
}
lv_obj_t *marker(int size, uint32_t value, bool hollow) {
  auto *object = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(object);
  lv_obj_set_size(object, size, size);
  lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(object, hollow ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(value), 0);
  lv_obj_set_style_border_color(object, lv_color_hex(hollow ? value : bg), 0);
  lv_obj_set_style_border_width(object, hollow ? 3 : 1, 0);
  lv_obj_set_scrollable(object, false);
  lv_obj_set_hidden(object, true);
  return object;
}
int arcAngle(float bearing) {
  return (int(std::lround(bearing)) % 360 + 630) % 360;
}
}
const lv_font_t *PanelUi::face(int size) const {
#if !PANEL_COMPACT_FONTS
  if(diameter_>=320) {
    if(size>=112) return &crowpanel_font_112;
    if(size>=80) return &crowpanel_font_80;
    if(size>=36) return &crowpanel_font_36;
    if(size>=28) return &crowpanel_font_28;
    if(size>=22) return &crowpanel_font_22;
    return &crowpanel_font_11;
  }
#endif
  if(size>=160) return &panel_font_80;
  if(size>=112) return &panel_font_56;
  if(size>=80) return &panel_font_40;
  if(size>=48) return &panel_font_24;
  if(size>=36) return &panel_font_18;
  if(size>=28) return &panel_font_14;
  return &crowpanel_font_11;
}
panel::TouchRect PanelUi::rect(int left,int top,int width,int height) const {
  return {uint16_t(x(left)),uint16_t(y(top)),uint16_t(p(width)),uint16_t(p(height))};
}
panel::TouchRect PanelUi::bounds(lv_obj_t *object,int padding) const {
  lv_obj_update_layout(object);lv_area_t a;lv_obj_get_coords(object,&a);
  const int l=std::max(0,int(a.x1)-padding),t=std::max(0,int(a.y1)-padding);
  const int r=std::min(width_,int(a.x2)+1+padding),b=std::min(height_,int(a.y2)+1+padding);
  return {uint16_t(l),uint16_t(t),uint16_t(std::max(0,r-l)),uint16_t(std::max(0,b-t))};
}
lv_obj_t *PanelUi::label(int top,int width,int size,uint32_t color) {
  auto *obj=lv_label_create(root_);
  lv_obj_set_width(obj,width==LV_SIZE_CONTENT?width:p(width));
  lv_obj_align(obj,LV_ALIGN_TOP_MID,0,y(top));
  lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_set_style_text_font(obj,face(size),0);
  lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
  lv_label_set_long_mode(obj,LV_LABEL_LONG_CLIP);lv_label_set_text(obj,"");return obj;
}
lv_obj_t *PanelUi::button(panel::TouchRect r,uint32_t background) {
  auto *obj=lv_button_create(root_);lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj,x(r.x),y(r.y));lv_obj_set_size(obj,p(r.width),std::max(28,p(r.height)));
  lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);lv_obj_set_style_bg_color(obj,lv_color_hex(background),0);
  lv_obj_set_style_bg_color(obj,lv_color_hex(0x365779),LV_STATE_PRESSED);
  lv_obj_set_style_border_color(obj,lv_color_hex(muted),0);lv_obj_set_style_border_width(obj,1,0);
  lv_obj_set_style_radius(obj,p(12),0);lv_obj_set_scrollable(obj,false);return obj;
}
void PanelUi::positionMarker(lv_obj_t *obj,float degrees,bool valid,int size) {
  const bool visible=valid && std::isfinite(degrees);
  if(visible) {
    const float angle=std::fmod(degrees,360.f)*3.14159265358979323846f/180.f;
    lv_obj_set_pos(obj,width_/2+std::lround(p(dialRadius)*std::sin(angle))-size/2,
                      height_/2-std::lround(p(dialRadius)*std::cos(angle))-size/2);
  }
  lv_obj_set_hidden(obj,!visible);
}
void PanelUi::invalidateLimit(float boundary,bool lower,bool target) {
  if(!std::isfinite(boundary)) return;
  // Narrow arc bounds keep warning updates on their own lane, leaving room in
  // LVGL's invalid-area list for the labels and independently moving markers.
  const float start=boundary-(lower?crowpanel::LimitGradientDegrees:0);
  for(int step=0;step<crowpanel::LimitGradientDegrees;step+=30) {
    lv_area_t a;lv_draw_arc_get_area(width_/2,height_/2,p(target?239:225),arcAngle(start+step),
        arcAngle(start+std::min(step+30,crowpanel::LimitGradientDegrees)),p(target?10:16),false,&a);
    a.x1-=3;a.y1-=3;a.x2+=3;a.y2+=3;lv_obj_invalidate_area(limits_,&a);
  }
  // The stop tick reaches past the inner warning lane, only at its boundary.
  lv_area_t tick;lv_draw_arc_get_area(width_/2,height_/2,p(240),arcAngle(boundary-1),
      arcAngle(boundary+1),p(34),false,&tick);
  tick.x1-=3;tick.y1-=3;tick.x2+=3;tick.y2+=3;lv_obj_invalidate_area(limits_,&tick);
}
void PanelUi::animateMarker(void *data, int32_t value) {
  auto &motion = *static_cast<MarkerMotion *>(data);
  motion.shown = float(value) / 1000.f;
  motion.owner->positionMarker(motion.object, motion.shown, true, motion.size);
}
void PanelUi::updateMarker(MarkerMotion &motion, float degrees, bool valid, bool reset, bool shortest) {
  valid = valid && std::isfinite(degrees);
  if (!valid) {
    if (motion.valid) lv_anim_delete(&motion, animateMarker);
    motion.valid = false;
    lv_obj_set_hidden(motion.object, true);
    return;
  }
  if (reset || !motion.valid) {
    lv_anim_delete(&motion, animateMarker);
    motion.shown = motion.requested = degrees;
    motion.valid = true;
    motion.owner->positionMarker(motion.object, degrees, true, motion.size);
    return;
  }
  if (degrees == motion.requested) return;
  lv_anim_delete(&motion, animateMarker);
  motion.requested = degrees;
  float destination = degrees;
  if (shortest) {
    motion.shown = std::fmod(motion.shown, 360.f);
    destination = motion.shown + crowpanel::shortestBearingDelta(motion.shown, degrees);
  }
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, &motion);
  lv_anim_set_exec_cb(&animation, animateMarker);
  lv_anim_set_values(&animation, std::lround(motion.shown * 1000.f), std::lround(destination * 1000.f));
  lv_anim_set_duration(&animation, 160);
  lv_anim_set_path_cb(&animation, lv_anim_path_linear);
  lv_anim_start(&animation);
}
void PanelUi::updateLimitBand(std::array<uint8_t,180> &band,uint8_t lower,uint8_t upper) {
  const auto tail=[](float distance,uint8_t strength) {
    distance=std::fmod(distance,360.f);
    if(distance<0)distance+=360.f;
    const float fade=std::max(0.f,1.f-distance/crowpanel::LimitGradientDegrees);
    return uint8_t(std::lround(strength*fade*fade));
  };
  for(unsigned i=0;i<band.size();++i) {
    const float bearing=float(i*2+1);
    band[i]=std::max(tail(minimum_-bearing,lower),tail(bearing-maximum_,upper));
  }
}
void PanelUi::drawLimits(lv_event_t *event) {
  auto &self = *static_cast<PanelUi *>(lv_event_get_user_data(event));
  auto *layer = lv_event_get_layer(event);
  const auto drawBand = [&](const std::array<uint8_t,180> &band,uint8_t lower, uint8_t upper, uint32_t tint,
                            int radius, int width, int stopInner, int stopOuter) {
    if (!lower && !upper) return;
    const auto &clip=layer->_clip_area;
    const int cx=self.width_/2,cy=self.height_/2;
    const int nearX=std::max({0,cx-int(clip.x2),int(clip.x1)-cx});
    const int nearY=std::max({0,cy-int(clip.y2),int(clip.y1)-cy});
    const int farX=std::max(std::abs(int(clip.x1)-cx),std::abs(int(clip.x2)-cx));
    const int farY=std::max(std::abs(int(clip.y1)-cy),std::abs(int(clip.y2)-cy));
    const int padding=std::max(1,self.p(3))+2;
    const int inner=std::max(0,self.p(std::min(radius-width,stopInner))-padding);
    const int outer=self.p(std::max(radius,stopOuter))+padding;
    // Centre-only labels and areas outside the rim need no gradient tasks.
    if(farX*farX+farY*farY<inner*inner || nearX*nearX+nearY*nearY>outer*outer) return;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = {self.width_/2,self.height_/2};
    arc.radius = self.p(radius); arc.width = std::max(1,self.p(width));
    // Blend against the empty inner rim, then overlap opaque arc ends. This
    // avoids dark antialias seams between translucent segments of the gradient.
    for (int bearing = 0; bearing < 360; bearing += 2) {
      const auto alpha = band[bearing/2];
      if (!alpha) continue;
      arc.opa = LV_OPA_COVER;
      arc.color = lv_color_mix(lv_color_hex(tint), lv_color_hex(self.background_), alpha);
      arc.start_angle = arcAngle(bearing);
      arc.end_angle = arcAngle(bearing + 2 + (band[(bearing/2+1)%band.size()] ? 1 : 0));
      // LVGL otherwise allocates a task for the entire circle, even when
      // this short segment is outside the current partial-buffer stripe.
      lv_area_t area;
      lv_draw_arc_get_area(arc.center.x,arc.center.y,arc.radius,arc.start_angle,arc.end_angle,arc.width,false,&area);
      area.x1-=2;area.y1-=2;area.x2+=2;area.y2+=2;
      if(area.x2<clip.x1 || area.x1>clip.x2 || area.y2<clip.y1 || area.y1>clip.y2) continue;
      lv_draw_arc(layer, &arc);
    }
    for (unsigned end = 0; end < 2; ++end) {
      const uint8_t strength = end ? upper : lower;
      if (!strength) continue;
      const float boundary = end ? self.maximum_ : self.minimum_;
      const float radians = boundary * (3.14159265358979323846f / 180.f);
      lv_draw_line_dsc_t line;
      lv_draw_line_dsc_init(&line);
      line.p1 = {lv_value_precise_t(self.width_/2 + self.p(stopInner) * std::sin(radians)), lv_value_precise_t(self.height_/2 - self.p(stopInner) * std::cos(radians))};
      line.p2 = {lv_value_precise_t(self.width_/2 + self.p(stopOuter) * std::sin(radians)), lv_value_precise_t(self.height_/2 - self.p(stopOuter) * std::cos(radians))};
      line.width = std::max(1,self.p(3)); line.color = lv_color_hex(tint); line.opa = strength;
      lv_draw_line(layer, &line);
    }
  };
  drawBand(self.limitBand_,self.lowerLimit_, self.upperLimit_, 0xff3b3b, 225, 16, 207, 239);
  // The selected goal has its own outer lane, so it cannot hide the actual
  // position warning when both approach the same mechanical stop.
  drawBand(self.targetLimitBand_,self.targetLowerLimit_, self.targetUpperLimit_, targetColor, 239, 10, 230, 239);
}
void PanelUi::create(bool round) {
  width_=lv_display_get_horizontal_resolution(lv_display_get_default());
  height_=lv_display_get_vertical_resolution(lv_display_get_default());
  diameter_=std::min(width_,height_);round_=round;root_=lv_screen_active();
  auto *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(bg), 0);
  lv_obj_set_scrollable(screen, false);
  auto *ring = lv_obj_create(screen);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, p(456), p(456));
  lv_obj_set_pos(ring, x(12), y(12));
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_border_color(ring, lv_color_hex(0x294156), 0);
  lv_obj_set_scrollable(ring, false);
  limits_ = lv_obj_create(screen);
  lv_obj_remove_style_all(limits_);
  lv_obj_set_pos(limits_, 0, 0); lv_obj_set_size(limits_, width_, height_);
  lv_obj_set_scrollable(limits_, false);
  lv_obj_set_clickable(limits_, false);
  lv_obj_add_event_cb(limits_, drawLimits, LV_EVENT_DRAW_MAIN, this);
  hudName_ = label(panel::HudNameButton.y, 230, 22, text);
  lv_label_set_long_mode(hudName_, LV_LABEL_LONG_DOT);
  lv_obj_set_height(hudName_,face(22)->line_height);
  lv_obj_set_hidden(hudName_, true);
  hudAddress_ = label(65, 340, 11, muted);
  lv_obj_set_hidden(hudAddress_, true);
  modeButton_ = button(panel::ModeButton, 0x183329);
  mode_ = buttonLabel(modeButton_,face(22));
  range_ = label(65, 300, 11, muted);
  axis_ = label(132, 320, 22, muted);
  angle_ = label(170, LV_SIZE_CONTENT, 112, text);
  secondaryAngle_ = label(170, LV_SIZE_CONTENT, 28, text);
  secondaryTarget_ = label(282, LV_SIZE_CONTENT, 22, targetColor);
  lv_obj_set_align(angle_, LV_ALIGN_TOP_LEFT);
  lv_obj_set_align(secondaryAngle_, LV_ALIGN_TOP_LEFT);
  lv_obj_set_align(secondaryTarget_, LV_ALIGN_TOP_LEFT);
  for (unsigned i = 0; i < 4; ++i) {
    arrows_[i] = lv_line_create(screen);
    lv_obj_remove_style_all(arrows_[i]);
    lv_obj_set_size(arrows_[i], p(36), p(36));
    for(unsigned j=0;j<3;++j) arrowPoints_[i][j]={lv_value_precise_t(p(int(idleArrows[i][j].x))),lv_value_precise_t(p(int(idleArrows[i][j].y)))};
    lv_line_set_points(arrows_[i], arrowPoints_[i], 3);
    lv_obj_set_style_line_width(arrows_[i], std::max(2,p(4)), 0);
    lv_obj_set_style_line_rounded(arrows_[i], true, 0);
    lv_obj_set_style_line_color(arrows_[i], lv_color_hex(0x64717e), 0);
  }
  targetCaption_ = label(282, LV_SIZE_CONTENT, 22, targetColor);
  target_ = label(282, LV_SIZE_CONTENT, 36, targetColor);
  lv_obj_set_align(targetCaption_, LV_ALIGN_TOP_LEFT);
  lv_obj_set_align(target_, LV_ALIGN_TOP_LEFT);
  status_ = label(322, 430, 22, 0xf0b85c);
  wifi_ = label(435, LV_SIZE_CONTENT, 22, wifiDisconnected);
#if PANEL_COMPACT_FONTS
  lv_obj_set_style_text_font(wifi_,&lv_font_montserrat_14,0);
#else
  lv_obj_set_style_text_font(wifi_,diameter_<320?&lv_font_montserrat_14:&lv_font_montserrat_22,0);
#endif
  lv_label_set_text(wifi_, LV_SYMBOL_WIFI);
  wifiSlash_ = lv_line_create(screen);
  lv_obj_remove_style_all(wifiSlash_);
  lv_obj_set_pos(wifiSlash_, x(226), y(433));
  for(unsigned j=0;j<2;++j) wifiSlashPoints_[j]={lv_value_precise_t(p(int(wifiSlash[j].x))),lv_value_precise_t(p(int(wifiSlash[j].y)))};
  lv_line_set_points(wifiSlash_, wifiSlashPoints_, 2);
  lv_obj_set_style_line_width(wifiSlash_, 3, 0);
  lv_obj_set_style_line_rounded(wifiSlash_, true, 0);
  lv_obj_set_style_line_color(wifiSlash_, lv_color_hex(wifiDisconnected), 0);
  wifiAp_ = label(442, LV_SIZE_CONTENT, 11, muted);
  lv_obj_align(wifiAp_, LV_ALIGN_TOP_MID, p(27), y(442));
  bluetooth_ = label(378, 340, 11, muted);
  ip_ = label(393, 340, 11, text);
  heap_ = label(408, 220, 11, muted);
  currentMarker_ = marker(p(currentMarkerSize), text, false);
  targetMarker_ = marker(p(targetMarkerSize), targetColor, true);
  currentMotion_.object = currentMarker_; currentMotion_.size = p(currentMarkerSize);currentMotion_.owner=this;
  targetMotion_.object = targetMarker_; targetMotion_.size = p(targetMarkerSize);targetMotion_.owner=this;
  hudList_ = lv_obj_create(screen);
  lv_obj_set_pos(hudList_, x(panel::HudList.x), y(panel::HudList.y));
  lv_obj_set_size(hudList_, p(panel::HudList.width), p(panel::HudList.height));
  lv_obj_set_style_bg_color(hudList_, lv_color_hex(0x101e2b), 0);
  lv_obj_set_style_bg_opa(hudList_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(hudList_, 0, 0);
  lv_obj_set_style_pad_all(hudList_, 0, 0);
  lv_obj_set_scrollable(hudList_, false);
  for (unsigned i = 0; i < 3; ++i) {
    hudRows_[i] = lv_label_create(hudList_);
    lv_obj_set_pos(hudRows_[i], p(10), p(10 + int(i) * panel::HudRowHeight));
    lv_obj_set_width(hudRows_[i], p(390));
    lv_obj_set_style_text_font(hudRows_[i], face(22), 0);
    lv_obj_set_style_text_color(hudRows_[i], lv_color_hex(i == 1 ? 0x63dfb5 : 0x96a9b7), 0);
    lv_obj_set_style_text_align(hudRows_[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hudRows_[i], LV_LABEL_LONG_CLIP);
  }
  lv_obj_set_hidden(hudList_, true);
  speedPanel_ = lv_obj_create(screen);
  lv_obj_remove_style_all(speedPanel_);
  lv_obj_set_size(speedPanel_, width_, height_);
  lv_obj_set_style_bg_color(speedPanel_, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(speedPanel_, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(speedPanel_, false);
  const auto speedLabel = [&](const char *value, int top, const lv_font_t *face) {
    auto *obj = lv_label_create(speedPanel_);
    lv_label_set_text(obj, value);
    lv_obj_set_style_text_font(obj, face, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(text), 0);
    lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y(top));
    return obj;
  };
  speedLabel("Скорость", 84, face(22));
  speedFont_ = *face(36); speedFont_.fallback = face(22);
  for (unsigned i = 0; i < 2; ++i) {
    speedAxes_[i] = speedLabel(i ? "ЭЛЕВАЦИЯ · EL" : "АЗИМУТ · AZ", 145 + 95 * i, face(22));
    speedValues_[i] = speedLabel("50%", 170 + 95 * i, &speedFont_);
    speedBars_[i] = lv_bar_create(speedPanel_);
    lv_obj_set_pos(speedBars_[i], x(100), y(219 + 95 * i));
    lv_obj_set_size(speedBars_[i], p(280), p(16));
    lv_bar_set_range(speedBars_[i], 5, 100);
    lv_obj_set_style_bg_color(speedBars_[i], lv_color_hex(0x294156), LV_PART_MAIN);
    lv_obj_set_style_bg_color(speedBars_[i], lv_color_hex(targetColor), LV_PART_INDICATOR);
  }
  speedBack_ = lv_button_create(speedPanel_);
  lv_obj_set_pos(speedBack_, x(panel::SpeedBack.x), y(panel::SpeedBack.y));
  lv_obj_set_size(speedBack_, p(panel::SpeedBack.width), std::max(28,p(panel::SpeedBack.height)));
  auto *backLabel = lv_label_create(speedBack_);
  lv_obj_set_style_text_font(backLabel, face(22), 0);
  lv_label_set_text(backLabel, "Назад");
  lv_obj_center(backLabel);
  lv_obj_set_hidden(speedPanel_, true);
  actionButton_=button({160,360,160,44},0x742d35);
  actionLabel_=buttonLabel(actionButton_,face(22));lv_label_set_text(actionLabel_,"СТОП");
  lv_obj_set_hidden(actionButton_,true);
  settingsButton_=button({8,12,88,44},0x183329);
  auto *settingsLabel=buttonLabel(settingsButton_,face(22));lv_label_set_text(settingsLabel,"КАЛ");
  lv_obj_set_hidden(settingsButton_,true);
}
void PanelUi::configureLayout(const PanelView &v) {
  const bool compact=diameter_<320;
  // Preserve room for touch controls instead of shrinking every widget uniformly.
  if(compact) {
    lv_obj_set_pos(modeButton_,x(224),y(90));lv_obj_set_size(modeButton_,p(160),28);
    if(lv_obj_get_style_align(axis_,LV_PART_MAIN)!=LV_ALIGN_TOP_LEFT) lv_obj_set_align(axis_,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(axis_,x(110),y(100));lv_obj_set_width(axis_,p(108));
    set(axis_,v.hudSelector?"":v.dualAxis?(v.elevation?"EL":"AZ"):"AZ");
    font(axis_,face(28));
    const bool confirm=!v.autoMode && ((v.targetValid && v.pending) || (v.secondaryTargetValid && v.secondaryPending));
    const int currentTop=y(confirm?142:178);
    lv_obj_set_y(angle_,currentTop);lv_obj_set_y(secondaryAngle_,currentTop+lv_obj_get_height(angle_)-lv_obj_get_height(secondaryAngle_));
    const bool idle=!v.movingNow && !v.statusFault && (!v.status[0] || !std::strcmp(v.status,"Имитация"));
    const auto *goal=confirm?face(112):idle?face(48):face(36);
    const auto *caption=confirm?face(28):idle?face(28):face(22);
    font(target_,goal);font(targetCaption_,caption);font(secondaryTarget_,face(22));
    lv_obj_update_layout(target_);lv_obj_update_layout(targetCaption_);lv_obj_update_layout(secondaryTarget_);
    if(confirm) {
      for(const auto *candidate:{face(112),face(80),face(48),face(36)}) {
        const bool footer=v.shows(DisplayHeap) && (v.shows(DisplayLocalAddress)||v.shows(DisplayApAddress));
        if((v.statusFault || footer) && candidate->line_height>face(80)->line_height)continue;
        if(footer && v.status[0] && (!v.movingNow || v.statusFault) && candidate->line_height>face(48)->line_height)continue;
        goal=candidate;font(target_,goal);lv_obj_update_layout(target_);
        if(lv_obj_get_width(target_)<=width_-p(40))break;
      }
      lv_obj_set_pos(targetCaption_,(width_-lv_obj_get_width(targetCaption_))/2,y(238));
      lv_obj_set_pos(target_,(width_-lv_obj_get_width(target_))/2,y(268));
      lv_obj_set_pos(secondaryTarget_,(width_-lv_obj_get_width(secondaryTarget_))/2,
                     y(268)+lv_obj_get_height(target_));
    } else {
      const int total=lv_obj_get_width(targetCaption_)+p(10)+lv_obj_get_width(target_)+(v.dualAxis?lv_obj_get_width(secondaryTarget_):0);
      const int left=(width_-total)/2,top=y(idle?344:288);
      lv_obj_set_pos(targetCaption_,left,top+goal->line_height-caption->line_height);
      const int valuesLeft=left+lv_obj_get_width(targetCaption_)+p(10);
      const bool otherFirst=v.dualAxis && v.elevation;
      lv_obj_set_pos(target_,valuesLeft+(otherFirst?lv_obj_get_width(secondaryTarget_):0),top);
      lv_obj_set_pos(secondaryTarget_,valuesLeft+(otherFirst?0:lv_obj_get_width(target_)),top+goal->line_height-face(22)->line_height);
    }
    lv_obj_set_y(status_,y(confirm?400:v.movingNow?394:354));lv_obj_set_width(status_,p(340));
    char addresses[sizeof v.ip];std::snprintf(addresses,sizeof addresses,"%s",v.ip);
    if(auto *separator=confirm||v.shows(DisplayHeap)?nullptr:std::strstr(addresses,"  ")) { *separator='\n';std::memmove(separator+1,separator+2,std::strlen(separator+2)+1); }
    set(ip_,addresses);
    lv_obj_set_y(ip_,y(confirm?436:400));lv_obj_set_size(ip_,round_?p(270):width_-20,(confirm?1:2)*face(11)->line_height);
    lv_obj_set_hidden(status_,v.movingNow && !v.statusFault);
    if(v.statusFault && v.movingNow) {
      lv_obj_set_y(status_,y(394));lv_obj_set_y(ip_,y(422));lv_obj_set_height(ip_,face(11)->line_height);
    }
    set(hudAddress_,v.hudAddress);
    lv_obj_set_size(hudAddress_,p(300),face(11)->line_height);
    if(lv_label_get_long_mode(hudAddress_)!=LV_LABEL_LONG_DOT) lv_label_set_long_mode(hudAddress_,LV_LABEL_LONG_DOT);
    if(lv_label_get_long_mode(ip_)!=LV_LABEL_LONG_DOT) lv_label_set_long_mode(ip_,LV_LABEL_LONG_DOT);
    for(auto *obj:{bluetooth_,heap_,wifi_,wifiSlash_,wifiAp_}) lv_obj_set_hidden(obj,true);
    if(v.shows(DisplayHeap)) {
      if(v.shows(DisplayLocalAddress)||v.shows(DisplayApAddress)) {
        lv_obj_set_y(status_,y(confirm?380:354));
        lv_obj_set_y(ip_,y(confirm?408:400));lv_obj_set_height(ip_,face(11)->line_height);
      }
      lv_obj_set_y(heap_,y(v.shows(DisplayLocalAddress)||v.shows(DisplayApAddress)?(confirm?434:426):(confirm?436:400)));
    }
    const auto current=bounds(angle_);const auto secondary=bounds(secondaryAngle_);
    const int leftCurrent=std::min(int(current.x),v.dualAxis?int(secondary.x):int(current.x));
    const int rightCurrent=std::max(int(current.x+current.width),v.dualAxis?int(secondary.x+secondary.width):0);
    touchLayout_.current={uint16_t(leftCurrent),current.y,uint16_t(rightCurrent-leftCurrent),current.height};
    auto target=bounds(target_,4);const auto captionRect=bounds(targetCaption_,4);
    int left=std::min(target.x,captionRect.x),top=std::min(target.y,captionRect.y);
    int right=std::max(target.x+target.width,captionRect.x+captionRect.width);
    int bottom=std::max(target.y+target.height,captionRect.y+captionRect.height);
    if(v.dualAxis) {const auto r=bounds(secondaryTarget_,4);left=std::min(left,int(r.x));top=std::min(top,int(r.y));right=std::max(right,int(r.x+r.width));bottom=std::max(bottom,int(r.y+r.height));}
    touchLayout_.target={uint16_t(left),uint16_t(top),uint16_t(right-left),uint16_t(bottom-top)};
    const int side=std::max(30,p(60)),arrowSize=std::max(24,p(48));
    const int arrowTop=currentTop+(lv_obj_get_height(angle_)-side)/2;
    for(auto &r:touchLayout_.arrows) r={};
    const unsigned negative=v.elevation?3:0,positive=v.elevation?2:1;
    touchLayout_.arrows[negative]={0,uint16_t(arrowTop),uint16_t(side),uint16_t(side)};
    touchLayout_.arrows[positive]={uint16_t(width_-side),uint16_t(arrowTop),uint16_t(side),uint16_t(side)};
    const auto sign=[](int value){return (value>0)-(value<0);};
    const int az=sign(v.azDirection?v.azDirection:!v.elevation?v.direction:0);
    const int el=sign(v.elDirection?v.elDirection:v.elevation?v.direction:0);
    const bool moving=v.movingNow && (az || el);
    const int direction=moving?(az+1)+3*(el+1):9+int(v.elevation);
    for(unsigned i=0;i<4;++i) {
      lv_obj_set_hidden(arrows_[i],v.hudSelector || i>=2);
      if(i>=2)continue;
      lv_obj_set_size(arrows_[i],arrowSize,arrowSize);
      lv_obj_set_pos(arrows_[i],(i?width_-side:0)+(side-arrowSize)/2,arrowTop+(side-arrowSize)/2);
      const int lineWidth=std::max(3,p(6));
      if(lv_obj_get_style_line_width(arrows_[i],LV_PART_MAIN)!=lineWidth)
        lv_obj_set_style_line_width(arrows_[i],lineWidth,0);
      if(compactArrowDirection_!=direction) {
        float dx=moving?float(az):v.elevation?0.f:i?1.f:-1.f;
        float dy=moving?float(-el):v.elevation?(i?-1.f:1.f):0.f;
        const float norm=std::sqrt(dx*dx+dy*dy);dx/=norm;dy/=norm;
        const float c=arrowSize/2.f,tipX=c+dx*.36f*arrowSize,tipY=c+dy*.36f*arrowSize;
        const float wingX=tipX-dx*.34f*arrowSize,wingY=tipY-dy*.34f*arrowSize;
        const float points[5][2]={{wingX-dy*.25f*arrowSize,wingY+dx*.25f*arrowSize},{tipX,tipY},
          {wingX+dy*.25f*arrowSize,wingY-dx*.25f*arrowSize},{tipX,tipY},{c-dx*.36f*arrowSize,c-dy*.36f*arrowSize}};
        for(unsigned j=0;j<5;++j)arrowPoints_[i][j]={lv_value_precise_t(std::lround(points[j][0])),lv_value_precise_t(std::lround(points[j][1]))};
        lv_line_set_points(arrows_[i],arrowPoints_[i],moving?5:3);lv_obj_invalidate(arrows_[i]);
      }
      const auto tint=lv_color_hex(moving?text:0x64717e);
      if(lv_color_to_u32(lv_obj_get_style_line_color(arrows_[i],LV_PART_MAIN))!=lv_color_to_u32(tint))
        lv_obj_set_style_line_color(arrows_[i],tint,0);
    }
    compactArrowDirection_=direction;
  }
  const bool showStop=compact && v.movingNow;
  lv_obj_set_hidden(hudName_,!v.hud || !v.shows(DisplayName));
  lv_obj_set_hidden(hudAddress_,!v.hud || !v.shows(DisplayControllerAddress));
  lv_obj_set_hidden(ip_,!v.shows(DisplayLocalAddress) && !v.shows(DisplayApAddress));
  lv_obj_set_hidden(heap_,!v.shows(DisplayHeap));
  lv_obj_set_hidden(axis_,!v.shows(DisplayAxis));
  lv_obj_set_hidden(actionButton_,!showStop);
  lv_obj_set_pos(actionButton_,x(v.speedScreen?250:160),y(v.speedScreen?374:334));
  lv_obj_set_width(actionButton_,p(v.speedScreen?130:160));
  lv_obj_set_pos(speedBack_,x(showStop?100:160),y(374));
  lv_obj_set_width(speedBack_,p(showStop?130:160));
  lv_obj_set_hidden(settingsButton_,!v.settingsAvailable || v.hud || v.speedScreen || v.hudSelector);
  touchLayout_.mode=bounds(modeButton_);
  touchLayout_.name=v.hud && v.shows(DisplayName)?bounds(hudName_,p(4)):
      v.hud && v.shows(DisplayControllerAddress)?bounds(hudAddress_,p(4)):panel::TouchRect{};
  touchLayout_.axis=v.dualAxis && v.shows(DisplayAxis)?bounds(axis_,p(8)):panel::TouchRect{};
  touchLayout_.peers=bounds(hudList_);
  for(unsigned i=0;i<3;++i) touchLayout_.rows[i]=bounds(hudRows_[i],p(4));
  touchLayout_.settings=v.settingsAvailable && !v.hud && !v.speedScreen && !v.hudSelector?bounds(settingsButton_):panel::TouchRect{};
  touchLayout_.stopArea=compact?bounds(actionButton_):panel::TouchRect{};
  touchLayout_.confirmAllowed=v.enabled && !v.movingNow && (v.targetValid || v.secondaryTargetValid || v.statusFault);
  touchLayout_.actionEpoch=actionEpoch_;
  touchLayout_.stop=showStop?bounds(actionButton_):panel::TouchRect{};
  for(unsigned i=0;i<2;++i) {
    auto track=bounds(speedBars_[i]);const int pad=std::max(10,p(12));
    track.y-=pad;track.height+=2*pad;touchLayout_.speedTracks[i]=track;
  }
  touchLayout_.speedBack=bounds(speedBack_);
  touchLayout_.centerX=width_/2;touchLayout_.centerY=height_/2;
  touchLayout_.width=width_;touchLayout_.height=height_;
  touchLayout_.innerRadius=p(140);touchLayout_.outerRadius=p(240);
  touchLayout_.swipeDeadzone=std::max(6,p(12));touchLayout_.swipeFull=std::max(50,p(100));
  touchLayout_.enabled=touchLayout_.enabled && v.enabled;
  confirmationCaption_=bounds(targetCaption_,p(8));confirmationAngle_=bounds(target_,p(8));
}
void PanelUi::setPressed(panel::TouchAction action) {
  if (action == panel::TouchAction::Mode) lv_obj_add_state(modeButton_, LV_STATE_PRESSED);
  else lv_obj_remove_state(modeButton_, LV_STATE_PRESSED);
}
void PanelUi::update(const PanelView &v) {
  if(previousValid_ && previous_==v) return;
  const uint32_t background = hudBackgroundColor(v.hud ? v.hudBackground : 0);
  if (background_ != background) {
    background_ = background;
    lv_obj_set_style_bg_color(root_, lv_color_hex(background), 0);
    lv_obj_set_style_bg_color(speedPanel_, lv_color_hex(background), 0);
    lv_obj_set_style_border_color(currentMarker_, lv_color_hex(background), 0);
    lv_obj_invalidate(root_);
  }
  const bool compact=diameter_<320;
  if(!previousValid_ || previous_.sourceId!=v.sourceId || previous_.pending!=v.pending ||
      previous_.secondaryPending!=v.secondaryPending || previous_.target!=v.target ||
      previous_.secondaryTarget!=v.secondaryTarget || previous_.autoMode!=v.autoMode || previous_.elevation!=v.elevation)
    ++actionEpoch_;
  previous_=v;previousValid_=true;
  lv_obj_set_hidden(hudName_, !v.hud);
  lv_obj_set_hidden(hudAddress_, !v.hud);
  if (v.hud) set(hudName_,v.hudName);
  if (v.hud && !compact) {
    char address[96];
    std::snprintf(address, sizeof address, "%s · %.1f°..%.1f°", v.hudAddress, double(v.minimum), double(v.maximum));
    set(hudAddress_, address);
  }
  lv_obj_set_hidden(hudList_, !v.hudSelector);
  if (v.hudSelector)
    for (unsigned i = 0; i < 3; ++i)
      set(hudRows_[i], v.hudRows[i]);
  set(mode_, v.autoHour ? "АВТО · 1 ЧАС" : v.autoMode ? "АВТО" : "РУЧНОЙ");
  color(mode_, v.autoMode ? 0x63dfb5 : 0xf0b85c);
  set(axis_,compact?(v.hudSelector?"":v.dualAxis?(v.elevation?"EL":"AZ"):"AZ"):
      !v.hudSelector && v.dualAxis ? (v.elevation ? "ЭЛЕВАЦИЯ · EL" : "АЗИМУТ · AZ") : "");
  const bool confirmation = !v.autoMode && ((v.targetValid && v.pending) ||
                                            (v.secondaryTargetValid && v.secondaryPending));
  // Enlarged idle readings belong to the compact layout only. The full layout
  // keeps its original fonts and geometry when motion/status changes.
  const bool idle=compact && !v.movingNow && !v.statusFault && (!v.status[0] || !std::strcmp(v.status,"Имитация"));
  const bool prominent=confirmation && !idle;
  const bool emphasize=prominent && !compact;
  const lv_font_t *mainFont = compact && confirmation ? face(80) : idle ? face(v.dualAxis?112:160) : emphasize ? face(36)
                         : v.dualAxis ? face(80)
                                      : face(112);
  const auto *otherFont = emphasize ? face(22) : face(28);
  const auto *goalFont =
      idle ? face(48) : compact ? face(36) : confirmation ? (v.dualAxis ? face(80) : face(112)) : face(36);
  font(angle_, mainFont);
  font(target_, goalFont);
  const auto *captionFont=idle?face(28):!compact && confirmation?face(36):face(22);
  font(targetCaption_,captionFont);
  char value[80];
  std::snprintf(value, sizeof value, "%.1f°..%.1f°", double(v.minimum), double(v.maximum));
  set(range_, value);
  lv_obj_set_hidden(range_, v.hud || v.hudSelector);
  if (v.feedback) {
    std::snprintf(value, sizeof value, "%.0f°", double(v.current));
  } else {
    std::snprintf(value, sizeof value, "%s", v.uncalibrated ? "NC" : "—");
  }
  set(angle_, value);
  if (v.secondaryFeedback) {
    std::snprintf(value, sizeof value, v.elevation ? "%.0f° / " : " / %.0f°", double(v.secondaryCurrent));
  } else {
    std::snprintf(value, sizeof value, v.elevation ? "%s / " : " / %s",
                  v.secondaryUncalibrated ? "NC" : "—");
  }
  set(secondaryAngle_, v.dualAxis ? value : "");
  const bool pendingOther=compact && confirmation && !v.pending && v.secondaryPending;
  if (pendingOther?v.secondaryTargetValid:v.targetValid) {
    std::snprintf(value, sizeof value, "%.1f°", double(pendingOther?v.secondaryTarget:v.target));
  } else {
    std::snprintf(value, sizeof value, "—");
  }
  set(target_, value);
  if (v.secondaryTargetValid) {
    std::snprintf(value, sizeof value, v.elevation ? "%.1f° / " : " / %.1f°", double(v.secondaryTarget));
  } else {
    std::snprintf(value, sizeof value, "%s", v.elevation ? "— / " : " / —");
  }
  if(compact && confirmation && v.dualAxis) {
    const bool otherValid=pendingOther?v.targetValid:v.secondaryTargetValid;
    const bool otherEl=pendingOther?v.elevation:!v.elevation;
    std::snprintf(value,sizeof value,otherValid?"%s %.1f°":"%s —",otherEl?"EL":"AZ",double(pendingOther?v.target:v.secondaryTarget));
  }
  set(secondaryTarget_, v.dualAxis ? value : "");
  set(targetCaption_, compact && confirmation ? ((v.elevation!=pendingOther)?"ЕХАТЬ · EL":"ЕХАТЬ · AZ") : confirmation ? "ЕХАТЬ" : "Цель");
  color(targetCaption_, confirmation ? 0xf0b85c : targetColor);
  color(target_, confirmation ? 0xf0b85c : targetColor);
  color(secondaryTarget_, confirmation ? 0xf0b85c : targetColor);
  const int currentMaximumWidth=p(idle?360:330);
  if(compact && idle && !confirmation && v.dualAxis) {
    // A third digit on the other axis must not keep the selected angle at
    // its moving size after release. Fit the secondary reading first.
    const auto textWidth=[](lv_obj_t *label,const lv_font_t *face) {
      lv_point_t size;
      lv_text_get_size(&size,lv_label_get_text(label),face,
          lv_obj_get_style_text_letter_space(label,LV_PART_MAIN),
          lv_obj_get_style_text_line_space(label,LV_PART_MAIN),LV_COORD_MAX,LV_TEXT_FLAG_NONE);
      return size.x;
    };
    if(textWidth(angle_,mainFont)+textWidth(secondaryAngle_,otherFont)>currentMaximumWidth)
      otherFont=face(22);
  }
  font(secondaryAngle_,otherFont);
  // Fit continuous multi-turn coordinates inside the ring, leaving room for jog arrows.
  auto fit = [&](lv_obj_t *primary, lv_obj_t *secondary, const lv_font_t *initial, int maximumWidth) {
    const lv_font_t *chosen = initial;
    for (int size : {160,112,80,48,36,28}) {
      if(!compact && (size==160 || size==48)) continue;
      const auto *candidate=face(size);
      if (candidate->line_height > initial->line_height) continue;
      font(primary, candidate);
      lv_obj_update_layout(primary); lv_obj_update_layout(secondary);
      chosen = candidate;
      if (lv_obj_get_width(primary) + (v.dualAxis ? lv_obj_get_width(secondary) : 0) <= maximumWidth) break;
    }
    return chosen;
  };
  mainFont = fit(angle_, secondaryAngle_, mainFont, currentMaximumWidth);
  if(!compact) goalFont = fit(target_, secondaryTarget_, goalFont, p(confirmation ? 380 : 300));
  auto row = [&](lv_obj_t *primary, lv_obj_t *secondary, const lv_font_t *large,
                 const lv_font_t *small, int top, int prefix) {
    lv_obj_update_layout(primary);
    lv_obj_update_layout(secondary);
    const int width =
        lv_obj_get_width(primary) + (v.dualAxis ? lv_obj_get_width(secondary) : 0) + prefix;
    const int x = (width_ - width) / 2;
    // Keep AZ on the left and EL on the right; selection only changes font size.
    const bool secondaryFirst = v.dualAxis && v.elevation;
    lv_obj_set_pos(primary, x + prefix + (secondaryFirst ? lv_obj_get_width(secondary) : 0), y(top));
    lv_obj_set_pos(secondary, x + prefix + (secondaryFirst ? 0 : lv_obj_get_width(primary)),
                   compact ? y(top)+lv_obj_get_height(primary)-lv_obj_get_height(secondary) :
                   y(top) + large->line_height - large->base_line - small->line_height + small->base_line);
    return x;
  };
  font(axis_, compact?face(28):v.dualAxis && confirmation ? face(11) : face(22));
  const int currentY = compact ? (confirmation?142:178) : v.dualAxis ? (confirmation ? 184 : 198) : 170;
  const int currentLeft = row(angle_, secondaryAngle_, mainFont, otherFont, currentY, 0);
  lv_obj_update_layout(angle_);
  lv_obj_update_layout(secondaryAngle_);
  const int currentWidth =
      lv_obj_get_width(angle_) + (v.dualAxis ? lv_obj_get_width(secondaryAngle_) : 0);
  const int currentHeight = lv_obj_get_height(angle_);
  const int goalY = idle ? 344 : confirmation ? (v.dualAxis ? 300 : 250) : (v.dualAxis ? 316 : 282);
  lv_obj_update_layout(targetCaption_);
  const int captionWidth = lv_obj_get_width(targetCaption_);
  const int goalX = compact ? 0 : row(target_, secondaryTarget_, goalFont, face(22), goalY,
                        prominent ? 0 : captionWidth + p(12));
  if(!compact) lv_obj_set_pos(targetCaption_, prominent ? (width_ - captionWidth) / 2 : goalX,
                 prominent ? y(v.dualAxis ? 266 : 210)
                              : y(goalY) + goalFont->line_height - goalFont->base_line -
                                    captionFont->line_height + captionFont->base_line);
  for (auto *obj : {angle_, secondaryAngle_, targetCaption_, target_, secondaryTarget_}) {
    lv_obj_set_hidden(obj, v.hudSelector);
  }
  lv_obj_update_layout(target_);
  confirmationVisible_ = confirmation && !v.hudSelector;
  confirmationCaption_ = bounds(targetCaption_,p(8));
  confirmationAngle_ = bounds(target_,p(8));
  lv_obj_update_layout(secondaryTarget_);
  if (v.dualAxis) {
    const auto other = bounds(secondaryTarget_,p(8));
    const int left = std::min(int(confirmationAngle_.x), int(other.x));
    const int right = std::max(int(confirmationAngle_.x + confirmationAngle_.width), int(other.x + other.width));
    confirmationAngle_.x = uint16_t(left);
    confirmationAngle_.width = uint16_t(right - left);
  }
  const int goalLeft = confirmation ? std::min(int(confirmationCaption_.x), goalX) : goalX;
  const int goalRight = std::max(int(confirmationCaption_.x + confirmationCaption_.width),
                                 std::max(int(lv_obj_get_x(target_) + lv_obj_get_width(target_)),
                                          v.dualAxis ? int(lv_obj_get_x(secondaryTarget_) +
                                                           lv_obj_get_width(secondaryTarget_)) : 0));
  const int goalTop = confirmation ? int(lv_obj_get_y(targetCaption_)) : y(goalY);
  const int goalBottom = y(goalY) + goalFont->line_height;
  touchLayout_ = {};
  touchLayout_.dualAxis = v.dualAxis;
  touchLayout_.enabled = !v.hudSelector;
  touchLayout_.current = {uint16_t(currentLeft), uint16_t(y(currentY)), uint16_t(currentWidth),
                          uint16_t(currentHeight)};
  touchLayout_.target = (v.targetValid || v.secondaryTargetValid || v.statusFault)
                            ? panel::TouchRect{uint16_t(goalLeft - 4), uint16_t(goalTop),
                                                   uint16_t(goalRight - goalLeft + 8),
                                                   uint16_t(goalBottom - goalTop + 4)}
                            : panel::TouchRect{};
  const int centerY = y(currentY) + currentHeight / 2;
  touchLayout_.arrows[0] = {uint16_t(std::max(x(12), currentLeft - std::max(32,p(52)))), uint16_t(centerY - std::max(15,p(22))), uint16_t(std::max(30,p(44))),
                            uint16_t(std::max(30,p(44)))};
  touchLayout_.arrows[1] = {uint16_t(std::min(x(424), currentLeft + currentWidth + p(8))),
                            uint16_t(centerY - std::max(15,p(22))), uint16_t(std::max(30,p(44))), uint16_t(std::max(30,p(44)))};
  touchLayout_.arrows[2] = {uint16_t(width_/2-std::max(15,p(22))), uint16_t(y(currentY) - std::max(30,p(46))), uint16_t(std::max(30,p(44))), uint16_t(std::max(30,p(44)))};
  touchLayout_.arrows[3] = {uint16_t(width_/2-std::max(15,p(22))), uint16_t(y(currentY) + currentHeight + p(2)), uint16_t(std::max(30,p(44))), uint16_t(std::max(30,p(44)))};
  for (unsigned i = 0; i < 4; ++i) {
    const auto &r = touchLayout_.arrows[i];
    if(!compact) {
      lv_obj_set_pos(arrows_[i], r.x + (r.width-p(36))/2, r.y + (r.height-p(36))/2);
      lv_obj_set_hidden(arrows_[i], v.hudSelector || (i >= 2 && !v.dualAxis));
    }
    if(compact)continue;
    const int az = v.azDirection ? v.azDirection : (!v.elevation ? v.direction : 0);
    const int el = v.elDirection ? v.elDirection : (v.elevation ? v.direction : 0);
    const bool moving = i == 0 ? az < 0 : i == 1 ? az > 0 : i == 2 ? el > 0 : el < 0;
    const auto *points = moving ? movingArrows[i] : idleArrows[i];
    if(arrowMoving_[i]!=moving) {
      arrowMoving_[i]=moving;
      for(unsigned j=0;j<(moving?5u:3u);++j) arrowPoints_[i][j]={lv_value_precise_t(p(int(points[j].x))),lv_value_precise_t(p(int(points[j].y)))};
      lv_line_set_points(arrows_[i],arrowPoints_[i],moving?5:3);
      lv_obj_invalidate(arrows_[i]);
    }
    const auto tint = lv_color_hex(moving ? text : 0x64717e);
    if (lv_color_to_u32(lv_obj_get_style_line_color(arrows_[i], LV_PART_MAIN)) !=
        lv_color_to_u32(tint)) {
      lv_obj_set_style_line_color(arrows_[i], tint, 0);
    }
  }
  if(!compact) {
  lv_obj_set_y(status_, y(v.dualAxis ? (confirmation ? 372 : 350) : (confirmation ? 350 : 322)));
  lv_obj_set_y(bluetooth_, y(v.dualAxis && confirmation ? 402 : 378));
  lv_obj_set_y(ip_, y(v.dualAxis && confirmation ? 417 : 393));
  lv_obj_set_y(heap_, y(v.dualAxis && confirmation ? 432 : 408));
  lv_obj_set_y(wifi_, y(v.dualAxis && confirmation ? 451 : 435));
  lv_obj_set_y(wifiSlash_, y(v.dualAxis && confirmation ? 449 : 433));
  lv_obj_set_y(wifiAp_, y(v.dualAxis && confirmation ? 458 : 442));
  }
  const auto statusMode = v.statusFault ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP;
  if (lv_label_get_long_mode(status_) != statusMode) lv_label_set_long_mode(status_, statusMode);
  color(status_, v.statusFault ? 0xff6b6b : 0xf0b85c);
  set(status_, v.status);
  color(wifi_, v.wifiConnected ? wifiWarning : wifiDisconnected);
  if(!compact) {
    lv_obj_set_hidden(wifi_, v.wifiConnected && v.wifiRssi > weakWifiRssi);
    lv_obj_set_hidden(wifiSlash_, v.wifiConnected);
  }
  set(wifiAp_, v.wifiAp ? "AP" : "");
  set(bluetooth_, !v.bluetoothEnabled ? "" : v.bluetoothConnected ? "Bluetooth: подключён" : "Bluetooth: нет связи");
  if(!compact) set(ip_, v.ip);
  set(heap_, v.heap);
  const bool contextChanged = sourceId_ != v.sourceId || elevation_ != v.elevation || maximum_ != v.maximum || minimum_ != v.minimum;
  const bool maximumValid = std::isfinite(v.maximum) && std::isfinite(v.minimum) && v.maximum > v.minimum;
  const bool rangeValid = v.feedback && std::isfinite(v.current) && maximumValid;
  const auto lower = rangeValid ? crowpanel::limitOpacity(v.current - v.minimum) : uint8_t(0);
  const auto upper = rangeValid ? crowpanel::limitOpacity(v.maximum - v.current) : uint8_t(0);
  const bool targetRangeValid = maximumValid && v.targetValid && std::isfinite(v.target) &&
                                v.target >= v.minimum && v.target <= v.maximum;
  const auto targetLower = targetRangeValid ? crowpanel::limitOpacity(v.target - v.minimum) : uint8_t(0);
  const auto targetUpper = targetRangeValid ? crowpanel::limitOpacity(v.maximum - v.target) : uint8_t(0);
  if(minimum_!=v.minimum || maximum_!=v.maximum) {
    // A range change moves both warning lanes; publish one complete update.
    lv_obj_invalidate(limits_);
  } else {
    if(lower!=lowerLimit_) invalidateLimit(minimum_,true,false);
    if(upper!=upperLimit_) invalidateLimit(maximum_,false,false);
    if(targetLower!=targetLowerLimit_) invalidateLimit(minimum_,true,true);
    if(targetUpper!=targetUpperLimit_) invalidateLimit(maximum_,false,true);
  }
  const bool limitChanged=lower!=lowerLimit_ || upper!=upperLimit_ || minimum_!=v.minimum || maximum_!=v.maximum;
  const bool targetLimitChanged=targetLower!=targetLowerLimit_ || targetUpper!=targetUpperLimit_ || minimum_!=v.minimum || maximum_!=v.maximum;
  lowerLimit_ = lower; upperLimit_ = upper;
  targetLowerLimit_ = targetLower; targetUpperLimit_ = targetUpper;
  minimum_ = std::isfinite(v.minimum) ? v.minimum : 0;
  maximum_ = std::isfinite(v.maximum) ? v.maximum : 450;
  if(limitChanged) updateLimitBand(limitBand_,lower,upper);
  if(targetLimitChanged) updateLimitBand(targetLimitBand_,targetLower,targetUpper);
  sourceId_ = v.sourceId; elevation_ = v.elevation;
  updateMarker(currentMotion_, v.current, v.feedback, contextChanged, false);
  updateMarker(targetMotion_, v.target, v.targetValid, contextChanged, true);
  configureLayout(v);
  lv_obj_set_hidden(speedPanel_, !v.speedScreen);
  if (v.speedScreen) {
    confirmationVisible_ = false;
    const auto common=touchLayout_;
    touchLayout_ = {};
    touchLayout_.speedTracks[0]=common.speedTracks[0];touchLayout_.speedTracks[1]=common.speedTracks[1];
    touchLayout_.speedBack=common.speedBack;touchLayout_.stop=common.stop;touchLayout_.stopArea=common.stopArea;
    touchLayout_.confirmAllowed=false;touchLayout_.actionEpoch=common.actionEpoch;
    touchLayout_.speedScreen = true;
    touchLayout_.enabled = common.enabled;
    touchLayout_.dualAxis = v.dualAxis;
    for (unsigned i = 0; i < 2; ++i) {
      const bool visible = !i || v.dualAxis;
      touchLayout_.speedEnabled[i] = visible && v.speedAvailable[i];
      for (auto *obj : {speedAxes_[i], speedValues_[i], speedBars_[i]}) lv_obj_set_hidden(obj, !visible);
      std::snprintf(value, sizeof value, v.speedAvailable[i] ? "%u%%" : "—", v.speedPercent[i]);
      set(speedValues_[i], value);
      color(speedValues_[i], v.speedAvailable[i] ? text : muted);
      lv_bar_set_value(speedBars_[i], int(v.speedPercent[i]), LV_ANIM_OFF);
      lv_obj_set_style_opa(speedBars_[i], v.speedAvailable[i] ? LV_OPA_COVER : LV_OPA_30, 0);
    }
  }
}
}
#endif
