#include "crowpanel_ui.hpp"
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include "crowpanel_dial.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
namespace forkrot {
namespace {
constexpr uint32_t bg = 0x0a111c, text = 0xeff5ff, muted = 0x8fabc3;
constexpr uint32_t targetColor = 0x64cfff;
constexpr uint32_t wifiWarning = 0xffd54f, wifiDisconnected = 0xff6b6b;
constexpr int weakWifiRssi = -80;
constexpr lv_point_precise_t wifiSlash[] = {{2, 2}, {26, 26}};
constexpr int dialRadius = 227;
constexpr int currentMarkerSize = 20, targetMarkerSize = 26;
constexpr lv_point_precise_t arrowLeft[] = {{34, 18}, {2, 18}, {14, 6}, {2, 18}, {14, 30}};
constexpr lv_point_precise_t arrowRight[] = {{2, 18}, {34, 18}, {22, 6}, {34, 18}, {22, 30}};
constexpr lv_point_precise_t arrowDown[] = {{18, 2}, {18, 34}, {6, 22}, {18, 34}, {30, 22}};
constexpr lv_point_precise_t arrowUp[] = {{18, 34}, {18, 2}, {6, 14}, {18, 2}, {30, 14}};
lv_obj_t *label(int y, int width, const lv_font_t *font, uint32_t color) {
  auto *object = lv_label_create(lv_screen_active());
  lv_obj_set_width(object, width);
  lv_obj_align(object, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(object, font, 0);
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
  lv_label_set_text(object, "");
  return object;
}
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
lv_obj_t *button(const crowpanel::TouchRect &rect, uint32_t background) {
  auto *object = lv_button_create(lv_screen_active());
  lv_obj_remove_style_all(object);
  lv_obj_set_pos(object, rect.x, rect.y);
  lv_obj_set_size(object, rect.width, rect.height);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(background), 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(0x365779), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(object, lv_color_hex(muted), 0);
  lv_obj_set_style_border_width(object, 1, 0);
  lv_obj_set_style_radius(object, 12, 0);
  lv_obj_set_scrollable(object, false);
  return object;
}
lv_obj_t *buttonLabel(lv_obj_t *parent) {
  auto *object = lv_label_create(parent);
  lv_obj_set_style_text_font(object, &crowpanel_font_22, 0);
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
void positionMarker(lv_obj_t *object, float degrees, bool valid, int size) {
  const bool visible = valid && std::isfinite(degrees);
  if (visible) {
    // Compass bearings: north at the top; 450 degrees occupies the same point as 90.
    const float radians = std::fmod(degrees, 360.f) * (3.14159265358979323846f / 180.f);
    lv_obj_set_pos(object, 240 + std::lround(dialRadius * std::sin(radians)) - size / 2,
                   240 - std::lround(dialRadius * std::cos(radians)) - size / 2);
    lv_obj_set_hidden(object, false);
  } else lv_obj_set_hidden(object, true);
}
int arcAngle(float bearing) {
  return (int(std::lround(bearing)) % 360 + 630) % 360;
}
void invalidateLimit(lv_obj_t *object, float boundary, bool lower) {
  lv_area_t area;
  lv_draw_arc_get_area(240, 240, 240, arcAngle(boundary - (lower ? 120 : 0)),
                      arcAngle(boundary + (lower ? 0 : 120)), 34, false, &area);
  // The 3px stop line straddles the sector edge. Include its half-width and
  // antialias fringe so fading/removal also clears pixels outside the arc.
  area.x1 -= 3; area.y1 -= 3;
  area.x2 += 3; area.y2 += 3;
  lv_obj_invalidate_area(object, &area);
}
crowpanel::TouchRect paddedBounds(lv_obj_t *object) {
  const int x = std::max(0, int(lv_obj_get_x(object)) - 8);
  const int y = std::max(0, int(lv_obj_get_y(object)) - 8);
  const int right = std::min(480, int(lv_obj_get_x(object) + lv_obj_get_width(object)) + 8);
  const int bottom = std::min(480, int(lv_obj_get_y(object) + lv_obj_get_height(object)) + 8);
  return {uint16_t(x), uint16_t(y), uint16_t(right - x), uint16_t(bottom - y)};
}
}
void CrowPanelUi::animateMarker(void *data, int32_t value) {
  auto &motion = *static_cast<MarkerMotion *>(data);
  motion.shown = float(value) / 1000.f;
  positionMarker(motion.object, motion.shown, true, motion.size);
}
void CrowPanelUi::updateMarker(MarkerMotion &motion, float degrees, bool valid, bool reset, bool shortest) {
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
    positionMarker(motion.object, degrees, true, motion.size);
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
void CrowPanelUi::drawLimits(lv_event_t *event) {
  auto &self = *static_cast<CrowPanelUi *>(lv_event_get_user_data(event));
  auto *layer = lv_event_get_layer(event);
  const auto drawBand = [&](uint8_t lower, uint8_t upper, uint32_t tint,
                            int radius, int width, int stopInner, int stopOuter) {
    if (!lower && !upper) return;
    const auto opacity = [&](float bearing) {
      const auto tail = [](float distance, uint8_t strength) {
        distance = std::fmod(distance + 720.f, 360.f);
        const float fade = std::max(0.f, 1.f - distance / 120.f);
        return uint8_t(std::lround(strength * fade * fade));
      };
      return std::max(tail(-bearing, lower), tail(bearing - self.maximum_, upper));
    };
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = {240, 240};
    arc.radius = radius; arc.width = width;
    // Blend against the empty inner rim, then overlap opaque arc ends. This
    // avoids dark antialias seams between translucent segments of the gradient.
    for (int bearing = 0; bearing < 360; bearing += 2) {
      const auto alpha = opacity(float(bearing + 1));
      if (!alpha) continue;
      arc.opa = LV_OPA_COVER;
      arc.color = lv_color_mix(lv_color_hex(tint), lv_color_hex(bg), alpha);
      arc.start_angle = arcAngle(bearing);
      arc.end_angle = arcAngle(bearing + 2 + (opacity(float(bearing + 3)) ? 1 : 0));
      lv_draw_arc(layer, &arc);
    }
    for (unsigned end = 0; end < 2; ++end) {
      const uint8_t strength = end ? upper : lower;
      if (!strength) continue;
      const float boundary = end ? self.maximum_ : 0.f;
      const float radians = boundary * (3.14159265358979323846f / 180.f);
      lv_draw_line_dsc_t line;
      lv_draw_line_dsc_init(&line);
      line.p1 = {lv_value_precise_t(240 + stopInner * std::sin(radians)), lv_value_precise_t(240 - stopInner * std::cos(radians))};
      line.p2 = {lv_value_precise_t(240 + stopOuter * std::sin(radians)), lv_value_precise_t(240 - stopOuter * std::cos(radians))};
      line.width = 3; line.color = lv_color_hex(tint); line.opa = strength;
      lv_draw_line(layer, &line);
    }
  };
  drawBand(self.lowerLimit_, self.upperLimit_, 0xff3b3b, 225, 16, 207, 239);
  // The selected goal has its own outer lane, so it cannot hide the actual
  // position warning when both approach the same mechanical stop.
  drawBand(self.targetLowerLimit_, self.targetUpperLimit_, targetColor, 239, 10, 230, 239);
}
void CrowPanelUi::create() {
  auto *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(bg), 0);
  lv_obj_set_scrollable(screen, false);
  auto *ring = lv_obj_create(screen);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, 456, 456);
  lv_obj_set_pos(ring, 12, 12);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_border_color(ring, lv_color_hex(0x294156), 0);
  lv_obj_set_scrollable(ring, false);
  limits_ = lv_obj_create(screen);
  lv_obj_remove_style_all(limits_);
  lv_obj_set_pos(limits_, 0, 0); lv_obj_set_size(limits_, 480, 480);
  lv_obj_set_scrollable(limits_, false);
  lv_obj_set_clickable(limits_, false);
  lv_obj_add_event_cb(limits_, drawLimits, LV_EVENT_DRAW_MAIN, this);
  hudName_ = label(crowpanel::HudNameButton.y, 230, &crowpanel_font_22, text);
  lv_label_set_long_mode(hudName_, LV_LABEL_LONG_DOT);
  lv_obj_set_hidden(hudName_, true);
  hudAddress_ = label(65, 230, &crowpanel_font_11, muted);
  lv_obj_set_hidden(hudAddress_, true);
  modeButton_ = button(crowpanel::ModeButton, 0x183329);
  mode_ = buttonLabel(modeButton_);
  axis_ = label(132, 320, &crowpanel_font_22, muted);
  angle_ = label(170, LV_SIZE_CONTENT, &crowpanel_font_112, text);
  motionArrow_ = lv_line_create(screen);
  lv_obj_remove_style_all(motionArrow_);
  lv_obj_set_size(motionArrow_, 36, 36);
  lv_obj_set_style_line_width(motionArrow_, 4, 0);
  lv_obj_set_style_line_rounded(motionArrow_, true, 0);
  lv_obj_set_style_line_color(motionArrow_, lv_color_hex(text), 0);
  lv_obj_set_hidden(motionArrow_, true);
  targetCaption_ = label(282, LV_SIZE_CONTENT, &crowpanel_font_22, targetColor);
  target_ = label(282, LV_SIZE_CONTENT, &crowpanel_font_36, targetColor);
  lv_obj_set_align(targetCaption_, LV_ALIGN_TOP_LEFT);
  lv_obj_set_align(target_, LV_ALIGN_TOP_LEFT);
  status_ = label(322, 430, &crowpanel_font_22, 0xf0b85c);
  wifi_ = label(435, LV_SIZE_CONTENT, &lv_font_montserrat_22, wifiDisconnected);
  lv_label_set_text(wifi_, LV_SYMBOL_WIFI);
  wifiSlash_ = lv_line_create(screen);
  lv_obj_remove_style_all(wifiSlash_);
  lv_obj_set_pos(wifiSlash_, 226, 433);
  lv_line_set_points(wifiSlash_, wifiSlash, 2);
  lv_obj_set_style_line_width(wifiSlash_, 3, 0);
  lv_obj_set_style_line_rounded(wifiSlash_, true, 0);
  lv_obj_set_style_line_color(wifiSlash_, lv_color_hex(wifiDisconnected), 0);
  wifiAp_ = label(442, LV_SIZE_CONTENT, &crowpanel_font_11, muted);
  lv_obj_align(wifiAp_, LV_ALIGN_TOP_MID, 27, 442);
  bluetooth_ = label(378, 340, &crowpanel_font_11, muted);
  ip_ = label(393, 300, &crowpanel_font_11, text);
  heap_ = label(408, 220, &crowpanel_font_11, muted);
  currentMarker_ = marker(currentMarkerSize, text, false);
  targetMarker_ = marker(targetMarkerSize, targetColor, true);
  currentMotion_.object = currentMarker_; currentMotion_.size = currentMarkerSize;
  targetMotion_.object = targetMarker_; targetMotion_.size = targetMarkerSize;
  hudList_ = lv_obj_create(screen);
  lv_obj_set_pos(hudList_, crowpanel::HudList.x, crowpanel::HudList.y);
  lv_obj_set_size(hudList_, crowpanel::HudList.width, crowpanel::HudList.height);
  lv_obj_set_style_bg_color(hudList_, lv_color_hex(0x101e2b), 0);
  lv_obj_set_style_bg_opa(hudList_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(hudList_, 0, 0);
  lv_obj_set_style_pad_all(hudList_, 0, 0);
  lv_obj_set_scrollable(hudList_, false);
  for (unsigned i = 0; i < 3; ++i) {
    hudRows_[i] = lv_label_create(hudList_);
    lv_obj_set_pos(hudRows_[i], 10, 10 + int(i) * crowpanel::HudRowHeight);
    lv_obj_set_width(hudRows_[i], 390);
    lv_obj_set_style_text_font(hudRows_[i], &crowpanel_font_22, 0);
    lv_obj_set_style_text_color(hudRows_[i], lv_color_hex(i == 1 ? 0x63dfb5 : 0x96a9b7), 0);
    lv_obj_set_style_text_align(hudRows_[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(hudRows_[i], LV_LABEL_LONG_CLIP);
  }
  lv_obj_set_hidden(hudList_, true);
}
void CrowPanelUi::setPressed(crowpanel::TouchAction action) {
  if (action == crowpanel::TouchAction::Mode) lv_obj_add_state(modeButton_, LV_STATE_PRESSED);
  else lv_obj_remove_state(modeButton_, LV_STATE_PRESSED);
}
void CrowPanelUi::update(const CrowPanelView &v) {
  lv_obj_set_hidden(hudName_, !v.hud);
  lv_obj_set_hidden(hudAddress_, !v.hud);
  if (v.hud) {
    set(hudName_, v.hudName);
    set(hudAddress_, v.hudAddress);
  }
  lv_obj_set_hidden(hudList_, !v.hudSelector);
  if (v.hudSelector)
    for (unsigned i = 0; i < 3; ++i)
      set(hudRows_[i], v.hudRows[i]);
  set(mode_, v.autoHour ? "АВТО · 1 ЧАС" : v.autoMode ? "АВТО" : "РУЧНОЙ");
  color(mode_, v.autoMode ? 0x63dfb5 : 0xf0b85c);
  set(axis_, !v.hudSelector && v.dualAxis ? (v.elevation ? "ЭЛЕВАЦИЯ · EL" : "АЗИМУТ · AZ") : "");
  const bool confirmation = !v.autoMode && v.targetValid && v.pending;
  font(angle_, confirmation ? &crowpanel_font_36 : &crowpanel_font_112);
  font(targetCaption_, confirmation ? &crowpanel_font_36 : &crowpanel_font_22);
  font(target_, confirmation ? &crowpanel_font_112 : &crowpanel_font_36);
  lv_obj_set_y(status_, confirmation ? 350 : 322);
  char value[80];
  if (v.feedback) std::snprintf(value, sizeof value, "%.0f°", double(v.current));
  else std::snprintf(value, sizeof value, "—");
  set(angle_, value);
  lv_obj_set_hidden(angle_, v.hudSelector);
  lv_obj_set_hidden(targetCaption_, v.hudSelector);
  lv_obj_set_hidden(target_, v.hudSelector);
  const bool showDirection = !v.hudSelector && v.feedback && std::isfinite(v.current) && v.direction != 0;
  if (showDirection) {
    const bool decreasing = v.direction < 0;
    const auto *points = v.elevation ? (decreasing ? arrowDown : arrowUp) : (decreasing ? arrowLeft : arrowRight);
    if (lv_line_get_points(motionArrow_) != points) lv_line_set_points(motionArrow_, points, 5);
    lv_obj_set_hidden(motionArrow_, false);
    lv_obj_update_layout(angle_);
    lv_obj_set_pos(motionArrow_, lv_obj_get_x(angle_) + (decreasing ? -50 : lv_obj_get_width(angle_) + 14),
                   lv_obj_get_y(angle_) + (lv_obj_get_height(angle_) - 36) / 2);
  } else lv_obj_set_hidden(motionArrow_, true);
  if (v.targetValid) std::snprintf(value, sizeof value, "%.1f°", double(v.target));
  else std::snprintf(value, sizeof value, "—");
  set(targetCaption_, confirmation ? "ЕХАТЬ" : "Цель");
  set(target_, value);
  color(targetCaption_, confirmation ? 0xf0b85c : targetColor);
  color(target_, confirmation ? 0xf0b85c : targetColor);
  lv_obj_update_layout(target_);
  if (confirmation) {
    // Give the received angle the central, large readout until it is confirmed.
    lv_obj_set_pos(targetCaption_, (480 - lv_obj_get_width(targetCaption_)) / 2, 210);
    lv_obj_set_pos(target_, (480 - lv_obj_get_width(target_)) / 2, 250);
    lv_obj_update_layout(target_);
    confirmationCaption_ = paddedBounds(targetCaption_);
    confirmationAngle_ = paddedBounds(target_);
  } else {
    // Keep the mixed-size caption and angle centered on a shared text baseline.
    const int captionWidth = lv_obj_get_width(targetCaption_);
    const int targetX = (480 - captionWidth - 12 - lv_obj_get_width(target_)) / 2;
    const int captionY = 282 + crowpanel_font_36.line_height - crowpanel_font_36.base_line
                            - crowpanel_font_22.line_height + crowpanel_font_22.base_line;
    lv_obj_set_pos(targetCaption_, targetX, captionY);
    lv_obj_set_pos(target_, targetX + captionWidth + 12, 282);
  }
  confirmationVisible_ = confirmation && !v.hudSelector;
  set(status_, v.status);
  color(wifi_, v.wifiConnected ? wifiWarning : wifiDisconnected);
  lv_obj_set_hidden(wifi_, v.wifiConnected && v.wifiRssi > weakWifiRssi);
  lv_obj_set_hidden(wifiSlash_, v.wifiConnected);
  set(wifiAp_, v.wifiAp ? "AP" : "");
  set(bluetooth_, !v.bluetoothEnabled ? "" : v.bluetoothConnected ? "Bluetooth: подключён" : "Bluetooth: нет связи");
  set(ip_, v.ip); set(heap_, v.heap);
  const bool contextChanged = sourceId_ != v.sourceId || elevation_ != v.elevation || maximum_ != v.maximum;
  const bool maximumValid = std::isfinite(v.maximum) && v.maximum > 0;
  const bool rangeValid = v.feedback && std::isfinite(v.current) && maximumValid;
  const auto lower = rangeValid ? crowpanel::limitOpacity(v.current) : uint8_t(0);
  const auto upper = rangeValid ? crowpanel::limitOpacity(v.maximum - v.current) : uint8_t(0);
  const bool targetRangeValid = maximumValid && v.targetValid && std::isfinite(v.target) &&
                                v.target >= 0 && v.target <= v.maximum;
  const auto targetLower = targetRangeValid ? crowpanel::limitOpacity(v.target) : uint8_t(0);
  const auto targetUpper = targetRangeValid ? crowpanel::limitOpacity(v.maximum - v.target) : uint8_t(0);
  if (lower != lowerLimit_ || targetLower != targetLowerLimit_) invalidateLimit(limits_, 0, true);
  if (upper != upperLimit_ || targetUpper != targetUpperLimit_ || maximum_ != v.maximum) {
    invalidateLimit(limits_, maximum_, false);
    if (std::isfinite(v.maximum)) invalidateLimit(limits_, v.maximum, false);
  }
  lowerLimit_ = lower; upperLimit_ = upper;
  targetLowerLimit_ = targetLower; targetUpperLimit_ = targetUpper;
  maximum_ = std::isfinite(v.maximum) ? v.maximum : 450;
  sourceId_ = v.sourceId; elevation_ = v.elevation;
  updateMarker(currentMotion_, v.current, v.feedback, contextChanged, false);
  updateMarker(targetMotion_, v.target, v.targetValid, contextChanged, true);
}
}
#endif
