#include "crowpanel_ui.hpp"
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include <cstdio>
#include <cstring>
#include <cmath>
namespace forkrot {
namespace {
constexpr uint32_t bg = 0x0a111c, text = 0xeff5ff, muted = 0x8fabc3;
constexpr uint32_t targetColor = 0x64cfff;
constexpr int dialRadius = 227;
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
  lv_obj_set_style_border_color(object, lv_color_hex(value), 0);
  lv_obj_set_style_border_width(object, hollow ? 3 : 0, 0);
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
  modeButton_ = button(crowpanel::ModeButton, 0x183329);
  mode_ = buttonLabel(modeButton_);
  axis_ = label(103, 320, &crowpanel_font_22, muted);
  angle_ = label(127, 430, &crowpanel_font_96, text);
  target_ = label(237, 410, &crowpanel_font_28, targetColor);
  status_ = label(273, 430, &crowpanel_font_22, 0xf0b85c);
  stopButton_ = button(crowpanel::StopButton, 0x642f3a);
  auto *stopText = buttonLabel(stopButton_);
  lv_label_set_text(stopText, "СТОП");
  wifi_ = label(363, 380, &crowpanel_font_11, muted);
  bluetooth_ = label(378, 340, &crowpanel_font_11, muted);
  ip_ = label(393, 300, &crowpanel_font_11, text);
  heap_ = label(408, 220, &crowpanel_font_11, muted);
  currentMarker_ = marker(10, text, false);
  targetMarker_ = marker(22, targetColor, true);
}
void CrowPanelUi::setPressed(crowpanel::TouchAction action) {
  if (action == crowpanel::TouchAction::Mode) lv_obj_add_state(modeButton_, LV_STATE_PRESSED);
  else lv_obj_remove_state(modeButton_, LV_STATE_PRESSED);
  if (action == crowpanel::TouchAction::Stop) lv_obj_add_state(stopButton_, LV_STATE_PRESSED);
  else lv_obj_remove_state(stopButton_, LV_STATE_PRESSED);
}
void CrowPanelUi::update(const CrowPanelView &v) {
  set(mode_, v.autoHour ? "Режим: АВТО · 1 ЧАС" : v.autoMode ? "Режим: АВТО" : "Режим: РУЧНОЙ");
  color(mode_, v.autoMode ? 0x63dfb5 : 0xf0b85c);
  set(axis_, v.elevation ? "УГОЛ МЕСТА · EL" : "АЗИМУТ · AZ");
  char value[80];
  if (v.feedback) std::snprintf(value, sizeof value, "%.0f°", double(v.current));
  else std::snprintf(value, sizeof value, "—");
  set(angle_, value);
  if (v.targetValid) std::snprintf(value, sizeof value, "%s %.1f°", v.pending ? "Ожидание" : "Цель", double(v.target));
  else std::snprintf(value, sizeof value, "Цель —");
  set(target_, value); color(target_, v.pending ? 0xf0b85c : 0x64cfff);
  set(status_, v.status); set(wifi_, v.wifi); set(bluetooth_, v.bluetooth);
  set(ip_, v.ip); set(heap_, v.heap);
  positionMarker(currentMarker_, v.current, v.feedback, 10);
  positionMarker(targetMarker_, v.target, v.targetValid, 22);
}
}
#endif
