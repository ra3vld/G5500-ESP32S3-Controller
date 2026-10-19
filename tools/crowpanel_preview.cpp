// Render the actual firmware dashboard off-device; no mock HTML or screenshots.
#include "crowpanel_ui.hpp"
#include "network_addresses.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
static std::array<uint16_t, 480 * 480> pixels{};
static std::array<uint16_t, 480 * 40> stripe{};
static std::array<uint16_t, 480 * 480> frame1{}, frame2{};
static bool directMode = false;
static unsigned flushes = 0;
static lv_obj_t *findLabel(const char *text, int minimumY = 0) {
  auto *screen = lv_screen_active();
  for (unsigned i = 0; i < lv_obj_get_child_count(screen); ++i) {
    auto *child = lv_obj_get_child(screen, i);
    if (lv_obj_check_type(child, &lv_label_class) && !std::strcmp(lv_label_get_text(child), text) &&
        lv_obj_get_y(child) >= minimumY) return child;
  }
  return nullptr;
}
static bool nearColor(int cx, int cy, int radius, uint32_t rgb) {
  const uint16_t expected = ((rgb >> 19) << 11) | (((rgb >> 10) & 63) << 5) | ((rgb >> 3) & 31);
  for (int y = cy - radius; y <= cy + radius; ++y)
    for (int x = cx - radius; x <= cx + radius; ++x)
      if (x >= 0 && x < 480 && y >= 0 && y < 480 && pixels[y * 480 + x] == expected) return true;
  return false;
}
static bool markerAt(float degrees, uint32_t color) {
  const float radians = degrees * 3.14159265358979323846f / 180.f;
  return nearColor(240 + std::lround(227 * std::sin(radians)),
                   240 - std::lround(227 * std::cos(radians)), 2, color);
}
static bool redAt(float degrees) {
  const float radians = degrees * 3.14159265358979323846f / 180.f;
  const int cx = 240 + std::lround(220 * std::sin(radians));
  const int cy = 240 - std::lround(220 * std::cos(radians));
  for (int y = cy - 2; y <= cy + 2; ++y) for (int x = cx - 2; x <= cx + 2; ++x) {
    const auto pixel = pixels[y * 480 + x];
    const int r = ((pixel >> 11) & 31) * 255 / 31, g = ((pixel >> 5) & 63) * 255 / 63;
    if (r - g > 40 && r > 2 * g) return true;
  }
  return false;
}
static bool blueLimitAt(float degrees) {
  const float radians = degrees * 3.14159265358979323846f / 180.f;
  const int cx = 240 + std::lround(235 * std::sin(radians));
  const int cy = 240 - std::lround(235 * std::cos(radians));
  for (int y = cy - 2; y <= cy + 2; ++y) for (int x = cx - 2; x <= cx + 2; ++x) {
    const auto pixel = pixels[y * 480 + x];
    const int r = ((pixel >> 11) & 31) * 255 / 31;
    const int g = ((pixel >> 5) & 63) * 255 / 63, b = (pixel & 31) * 255 / 31;
    if (b - r > 24 && g - r > 16) return true;
  }
  return false;
}
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *data) {
  ++flushes;
  if (directMode) {
    if (lv_display_flush_is_last(d)) std::memcpy(pixels.data(), data, sizeof pixels);
    lv_display_flush_ready(d);
    return;
  }
  auto *p = reinterpret_cast<uint16_t *>(data);
  for (int y = a->y1; y <= a->y2; ++y)
    for (int x = a->x1; x <= a->x2; ++x) {
      assert(x >= 0 && x < 480 && y >= 0 && y < 480);
      pixels[y * 480 + x] = *p++;
    }
  lv_display_flush_ready(d);
}
static void save(const char *path) {
  FILE *f = std::fopen(path, "wb"); assert(f);
  std::fprintf(f, "P6\n480 480\n255\n");
  for (int y = 0; y < 480; ++y) for (int x = 0; x < 480; ++x) {
    auto c = pixels[y * 480 + x];
    if ((x - 240) * (x - 240) + (y - 240) * (y - 240) > 240 * 240) c = 0;
    uint8_t rgb[] = {uint8_t(((c >> 11) & 31) * 255 / 31), uint8_t(((c >> 5) & 63) * 255 / 63), uint8_t((c & 31) * 255 / 31)};
    assert(std::fwrite(rgb, 1, sizeof rgb, f) == sizeof rgb);
  }
  std::fclose(f);
}
static void settle(lv_display_t *display) {
  lv_tick_inc(200);
  lv_timer_handler();
  lv_refr_now(display);
}
static void checkFullRedraw(lv_display_t *display, const char *directory) {
  const auto incremental = pixels;
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(display);
  if (incremental != pixels) {
    int count = 0, left = 480, top = 480, right = 0, bottom = 0;
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 480; ++x)
      if (incremental[y * 480 + x] != pixels[y * 480 + x]) {
        ++count; left = std::min(left, x); right = std::max(right, x);
        top = std::min(top, y); bottom = std::max(bottom, y);
      }
    std::fprintf(stderr, "Stale stop pixels: %d in (%d,%d)-(%d,%d)\n", count, left, top, right, bottom);
    char path[512];
    std::snprintf(path, sizeof path, "%s/stop-full.ppm", directory); save(path);
    const auto full = pixels; pixels = incremental;
    std::snprintf(path, sizeof path, "%s/stop-incremental.ppm", directory); save(path);
    pixels = full;
  }
  assert(incremental == pixels);
}
int main(int argc, char **argv) {
  assert(argc == 2 || argc == 3);
  directMode = argc == 3;
  lv_init();
  auto *d = lv_display_create(480, 480);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  if (directMode) lv_display_set_buffers(d, frame1.data(), frame2.data(), sizeof frame1, LV_DISPLAY_RENDER_MODE_DIRECT);
  else lv_display_set_buffers(d, stripe.data(), nullptr, sizeof stripe, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d, flush);
  forkrot::CrowPanelUi ui; ui.create();
  forkrot::CrowPanelView view;
  view.dualAxis = true;
  view.targetValid = true; view.target = 210;
  view.wifiConnected = true; view.wifiAp = true; view.wifiRssi = -58;
  forkrot::formatNetworkAddresses(view.ip, sizeof view.ip, "192.168.3.234", "192.168.4.1", "  ");
  std::strcpy(view.heap, "Heap: 123 КБ");
  ui.update(view); settle(d);
  char path[1024]; std::snprintf(path, sizeof path, "%s/no-drive.ppm", argv[1]); save(path);
  const unsigned previous = flushes;
  for (int i = 0; i < 100; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
  assert(flushes == previous); // Unchanged dashboard must not continuously repaint.
  view.feedback = true; view.current = 450; view.target = 359.9;
  view.direction = -1;
  view.autoMode = true; view.autoHour = true;
  std::strcpy(view.status, "Поворот по цели");
  view.bluetoothEnabled = true; view.bluetoothConnected = true;
  ui.update(view); settle(d);
  std::snprintf(path, sizeof path, "%s/layout-test.ppm", argv[1]); save(path);
  view.elevation = true; view.maximum = 180; view.current = 180; view.target = 15.5;
  view.wifiConnected = false;
  std::strcpy(view.ip, "AP: 192.168.4.1");
  ui.update(view); settle(d);
  std::snprintf(path, sizeof path, "%s/elevation.ppm", argv[1]); save(path);
  view.elevation = false; view.maximum = 450; view.current = 16.5; view.target = 40;
  view.direction = 1;
  view.autoHour = false;
  std::strcpy(view.status, "Имитация · вращение");
  ui.update(view); settle(d);
  std::snprintf(path, sizeof path, "%s/simulation.ppm", argv[1]); save(path);
  view.dualAxis = false;
  ui.update(view); settle(d);
  std::snprintf(path, sizeof path, "%s/azimuth-only.ppm", argv[1]); save(path);
  view.dualAxis = true;
  // Render all four movement indicators and verify that stopping erases them.
  struct Motion { const char *name; bool elevation; int direction; float current, target; };
  for (const auto &motion : {Motion{"az-left", false, -1, 450, 359.9},
       Motion{"az-right", false, 1, 9, 120}, Motion{"el-down", true, -1, 180, 30},
       Motion{"el-up", true, 1, 90, 150}}) {
    view.elevation = motion.elevation; view.maximum = motion.elevation ? 180 : 450;
    view.current = motion.current; view.target = motion.target;
    view.direction = 0;
    ui.update(view); settle(d);
    const auto stopped = pixels;
    view.direction = motion.direction;
    ui.update(view); settle(d);
    assert(pixels != stopped);
    std::snprintf(path, sizeof path, "%s/%s.ppm", argv[1], motion.name); save(path);
    const auto idle = flushes;
    for (int i = 0; i < 10; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
    assert(flushes == idle);
    view.direction = 0;
    ui.update(view); settle(d);
    assert(pixels == stopped);
  }
  view.elevation = false; view.maximum = 450;
  // RSSI changes within one state must not repaint; disconnect/recovery must clear the slash.
  view.current = 120; view.target = 120;
  view.wifiConnected = true; view.wifiAp = false; view.wifiRssi = -79;
  std::strcpy(view.status, "Имитация · остановлен");
  std::strcpy(view.ip, "IP: 192.168.3.234");
  ui.update(view); settle(d);
  const auto goodWifi = pixels;
  assert(!nearColor(240, 446, 14, 0x63dfb5));
  assert(!nearColor(240, 446, 14, 0xff6b6b));
  std::snprintf(path, sizeof path, "%s/wifi-good.ppm", argv[1]); save(path);
  auto wifiIdle = flushes;
  view.wifiRssi = -58;
  ui.update(view); lv_tick_inc(100); lv_timer_handler();
  assert(flushes == wifiIdle);
  view.wifiRssi = -80;
  ui.update(view); settle(d);
  const auto weakWifi = pixels;
  assert(nearColor(240, 446, 14, 0xffd54f));
  assert(!nearColor(240, 446, 14, 0xff6b6b));
  assert(!nearColor(240, 446, 14, 0x63dfb5));
  std::snprintf(path, sizeof path, "%s/wifi-weak.ppm", argv[1]); save(path);
  wifiIdle = flushes;
  view.wifiRssi = -90;
  ui.update(view); lv_tick_inc(100); lv_timer_handler();
  assert(flushes == wifiIdle);
  view.wifiConnected = false; view.wifiRssi = 0;
  std::strcpy(view.ip, "IP: —");
  ui.update(view); settle(d);
  assert(pixels != weakWifi);
  assert(nearColor(228, 435, 1, 0xff6b6b));
  std::snprintf(path, sizeof path, "%s/wifi-lost.ppm", argv[1]); save(path);
  view.wifiAp = true;
  std::strcpy(view.ip, "AP: 192.168.4.1");
  ui.update(view); settle(d);
  std::snprintf(path, sizeof path, "%s/wifi-ap.ppm", argv[1]); save(path);
  view.wifiConnected = true; view.wifiAp = false; view.wifiRssi = -60;
  std::strcpy(view.ip, "IP: 192.168.3.234");
  ui.update(view); settle(d);
  assert(pixels == goodWifi);
  // Disabled Bluetooth is blank; an enabled adapter still reports no client.
  view.bluetoothEnabled = false; view.bluetoothConnected = false;
  ui.update(view); settle(d);
  const auto bluetoothOff = pixels;
  assert(!nearColor(240, 383, 5, 0x8fabc3));
  std::snprintf(path, sizeof path, "%s/bluetooth-off.ppm", argv[1]); save(path);
  view.bluetoothEnabled = true;
  ui.update(view); settle(d);
  const auto bluetoothWaiting = pixels;
  assert(pixels != bluetoothOff);
  assert(nearColor(240, 383, 5, 0x8fabc3));
  std::snprintf(path, sizeof path, "%s/bluetooth-waiting.ppm", argv[1]); save(path);
  view.bluetoothConnected = true;
  ui.update(view); settle(d);
  assert(pixels != bluetoothWaiting);
  std::snprintf(path, sizeof path, "%s/bluetooth-connected.ppm", argv[1]); save(path);
  view.bluetoothEnabled = false;
  ui.update(view); settle(d);
  assert(pixels == bluetoothOff);
  // Both markers must remain distinct at identical bearings, including AZ overlap >360.
  struct Bearing { float angle; int x, y; };
  for (const auto &bearing : {Bearing{0, 240, 13}, Bearing{90, 467, 240},
       Bearing{180, 240, 467}, Bearing{270, 13, 240}, Bearing{450, 467, 240}}) {
    view.current = view.target = bearing.angle;
    view.feedback = view.targetValid = true;
    ui.update(view); settle(d);
    assert(nearColor(bearing.x, bearing.y, 2, 0xeff5ff));
    assert(nearColor(bearing.x, bearing.y, 11, 0x64cfff));
    const auto idle = flushes;
    for (int i = 0; i < 10; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
    assert(flushes == idle);
    view.feedback = view.targetValid = false;
    ui.update(view); settle(d);
    assert(!nearColor(bearing.x, bearing.y, 2, 0xeff5ff));
    assert(!nearColor(bearing.x, bearing.y, 11, 0x64cfff));
  }
  ui.setPressed(forkrot::crowpanel::TouchAction::Mode); settle(d);
  assert(nearColor(150, 106, 2, 0x365779));
  ui.setPressed(forkrot::crowpanel::TouchAction::None); settle(d);
  assert(nearColor(150, 106, 2, 0x183329));
  view.hud = true;
  view.feedback = true;
  view.targetValid = true;
  view.current = 121;
  view.target = 180;
  std::strcpy(view.hudName, "Дача · G-800DXA");
  std::strcpy(view.hudAddress, "hud:192.168.3.234");
  std::strcpy(view.status, "Вращение");
  view.direction = 1;
  ui.update(view);
  settle(d);
  std::snprintf(path, sizeof path, "%s/hud.ppm", argv[1]);
  save(path);
  view.hudSelector = true;
  std::strcpy(view.hudRows[0], "Мачта · G-5500\n120 / 30 · Ход · 0s");
  std::strcpy(view.hudRows[1], "Дача · G-800DXA\n121 / 0 · Стоп · 0s");
  std::strcpy(view.hudRows[2], "Спутниковая\n240 / 80 · Нет связи · 12s");
  ui.update(view);
  settle(d);
  std::snprintf(path, sizeof path, "%s/hud-selector.ppm", argv[1]);
  save(path);
  view.hudSelector = false;
  view.feedback = view.targetValid = false;
  std::strcpy(view.status, "Нет связи с повороткой");
  ui.update(view);
  settle(d);
  std::snprintf(path, sizeof path, "%s/hud-offline.ppm", argv[1]);
  save(path);
  // Pending manual targets take over the large readout, then restore the exact
  // normal image on confirmation/cancellation, for local, HUD and both axes.
  view = forkrot::CrowPanelView{};
  view.feedback = view.targetValid = true;
  view.current = 121;
  view.wifiConnected = true; view.wifiRssi = -60;
  std::strcpy(view.status, "Остановлен");
  std::strcpy(view.hudName, "Дача · G-800DXA");
  std::strcpy(view.hudAddress, "hud:192.168.3.234");
  struct Pending { const char *name; bool hud, elevation; float target; };
  for (const auto &state : {Pending{"manual-confirm-local", false, false, 100},
       Pending{"manual-confirm-hud", true, false, 450},
       Pending{"manual-confirm-el", true, true, 179.5f}}) {
    view.hud = state.hud; view.elevation = state.elevation; view.dualAxis = state.elevation; view.maximum = state.elevation ? 180 : 450;
    view.autoMode = view.pending = false; view.target = state.target;
    ui.update(view); settle(d);
    const auto normal = pixels;
    view.pending = true;
    ui.update(view); settle(d);
    assert(pixels != normal);
    auto *caption = findLabel("ЕХАТЬ"); assert(caption);
    const auto &hit = ui.touchLayout().target;
    assert(hit.contains(hit.x + hit.width / 2, hit.y + hit.height / 2));
    if (!view.dualAxis) {
      assert(ui.confirmationHit(240, 224) && ui.confirmationHit(240, 290));
    }
    assert(!ui.confirmationHit(13, 240) && !ui.confirmationHit(240, 180));
    char angleText[40]; std::snprintf(angleText, sizeof angleText, "%.1f°", double(view.target));
    auto *target = findLabel(angleText); assert(target);
    auto *current = findLabel("121°"); assert(current);
    assert(lv_obj_get_style_text_font(target, LV_PART_MAIN) ==
           (view.dualAxis ? &crowpanel_font_80 : &crowpanel_font_112));
    assert(lv_obj_get_style_text_font(current, LV_PART_MAIN) == &crowpanel_font_36);
    assert(lv_obj_get_y(current) + lv_obj_get_height(current) < lv_obj_get_y(caption));
    assert(lv_obj_get_y(caption) + lv_obj_get_height(caption) < lv_obj_get_y(target));
    assert(lv_obj_get_x(target) >= 30 && lv_obj_get_x(target) + lv_obj_get_width(target) <= 450);
    assert(lv_obj_get_y(target) + lv_obj_get_height(target) < lv_obj_get_y(findLabel(view.status)));
    std::snprintf(path, sizeof path, "%s/%s.ppm", argv[1], state.name); save(path);
    const auto idle = flushes;
    for (int i = 0; i < 10; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
    assert(flushes == idle);
    view.target = 99.5f;
    ui.update(view); settle(d);
    assert(findLabel("99.5°") && findLabel("ЕХАТЬ"));
    view.target = state.target; view.pending = false;
    ui.update(view); settle(d);
    assert(pixels == normal && !findLabel("ЕХАТЬ"));
    // Auto mode must never show the confirmation layout, even with stale pending state.
    view.autoMode = true;
    ui.update(view); settle(d);
    const auto automatic = pixels;
    view.pending = true;
    ui.update(view); settle(d);
    assert(pixels == automatic && !findLabel("ЕХАТЬ"));
  }
  view = forkrot::CrowPanelView{};
  view.hud = view.feedback = view.targetValid = true;
  std::strcpy(view.hudName, "Дача · G-800DXA");
  std::strcpy(view.hudAddress, "hud:192.168.3.234");
  std::strcpy(view.status, "Остановлен");
  view.current = view.target = 225;
  ui.update(view); settle(d);
  assert(!redAt(350) && !redAt(100));
  view.current = view.target = 265;
  ui.update(view); lv_refr_now(d);
  assert(markerAt(225, 0xeff5ff) && !markerAt(265, 0xeff5ff));
  lv_tick_inc(80); lv_timer_handler(); lv_refr_now(d);
  assert(markerAt(245, 0xeff5ff) && !markerAt(265, 0xeff5ff));
  lv_tick_inc(80); lv_timer_handler(); lv_refr_now(d);
  assert(markerAt(265, 0xeff5ff));
  view.current = view.target = 359;
  ui.update(view); settle(d);
  view.current = 361; view.target = 1;
  ui.update(view); lv_tick_inc(80); lv_timer_handler(); lv_refr_now(d);
  assert(markerAt(360, 0xeff5ff) && !markerAt(180, 0xeff5ff));
  settle(d);
  // Full mechanical angles determine which side of 0/450 is unavailable.
  view.target = 225;
  view.current = 90;
  ui.update(view); settle(d);
  assert(redAt(350) && !redAt(10) && !redAt(100));
  std::snprintf(path, sizeof path, "%s/lower-limit-90.ppm", argv[1]); save(path);
  view.current = 350;
  ui.update(view); settle(d);
  assert(redAt(100) && !redAt(80) && !redAt(330));
  std::snprintf(path, sizeof path, "%s/upper-limit-350.ppm", argv[1]); save(path);
  view.current = 450;
  ui.update(view); settle(d);
  assert(redAt(100));
  std::snprintf(path, sizeof path, "%s/upper-stop-450.ppm", argv[1]); save(path);
  view.maximum = 360; view.current = 350;
  ui.update(view); settle(d);
  assert(redAt(10) && !redAt(330));
  view.elevation = view.dualAxis = true; view.maximum = 180; view.current = 90; view.target = 120;
  ui.update(view); settle(d);
  assert(redAt(350) && redAt(190));
  std::snprintf(path, sizeof path, "%s/elevation-limits.ppm", argv[1]); save(path);
  view.feedback = false;
  ui.update(view); settle(d);
  assert(!redAt(350) && !redAt(190));
  // Goal warnings update while the measured position is stationary. Both
  // lanes stay visible at the same stop and old tails clear when only the
  // target, its validity, or the mechanical range changes.
  view.elevation = view.dualAxis = false; view.maximum = 450;
  view.feedback = true; view.current = 225; view.target = 120;
  ui.update(view); settle(d);
  assert(!blueLimitAt(350) && !blueLimitAt(100));
  view.target = 90;
  ui.update(view); settle(d);
  assert(blueLimitAt(350) && !blueLimitAt(100) && !redAt(350));
  std::snprintf(path, sizeof path, "%s/target-lower-limit-90.ppm", argv[1]); save(path);
  view.target = 330;
  ui.update(view); settle(d);
  assert(!blueLimitAt(350) && !blueLimitAt(100));
  view.target = 350;
  ui.update(view); settle(d);
  assert(!blueLimitAt(350) && blueLimitAt(100) && !redAt(100));
  view.current = 90;
  ui.update(view); settle(d);
  assert(redAt(350) && blueLimitAt(100));
  std::snprintf(path, sizeof path, "%s/current-low-target-high.ppm", argv[1]); save(path);
  view.current = view.target = 450;
  ui.update(view); settle(d);
  assert(redAt(100) && blueLimitAt(100));
  std::snprintf(path, sizeof path, "%s/current-target-upper-stop.ppm", argv[1]); save(path);
  view.target = 90; // Same bearing as 450, but the opposite mechanical stop.
  ui.update(view); settle(d);
  assert(blueLimitAt(350) && !blueLimitAt(100) && redAt(100));
  for (const float invalid : {NAN, -1.f, 451.f}) {
    view.target = invalid;
    ui.update(view); settle(d);
    assert(!blueLimitAt(350) && !blueLimitAt(100) && redAt(100));
  }
  view.target = 350; view.targetValid = false;
  ui.update(view); settle(d);
  assert(!blueLimitAt(100) && redAt(100));
  view.targetValid = true; view.maximum = 360; view.current = 225;
  ui.update(view); settle(d);
  assert(blueLimitAt(10) && !blueLimitAt(100) && !blueLimitAt(350));
  view.elevation = view.dualAxis = true; view.maximum = 180;
  view.current = view.target = 90;
  ui.update(view); settle(d);
  assert(blueLimitAt(350) && blueLimitAt(190) && redAt(350) && redAt(190));
  std::snprintf(path, sizeof path, "%s/current-target-elevation.ppm", argv[1]); save(path);
  view.feedback = false;
  ui.update(view); settle(d);
  assert(blueLimitAt(350) && blueLimitAt(190) && !redAt(350) && !redAt(190));
  view.maximum = NAN;
  ui.update(view); settle(d);
  assert(!blueLimitAt(350) && !blueLimitAt(190));
  // A stop's 3px line extends to both sides of its sector boundary. Exercise
  // fading and disappearance before the opposite warning can repaint it.
  for (bool targetOnly : {false, true}) {
    view = forkrot::CrowPanelView{};
    view.feedback = view.targetValid = true;
    view.current = targetOnly ? 225 : 400; view.target = targetOnly ? 400 : 225;
    ui.update(view); settle(d);
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(d);
    for (float angle : {380.f, 350.f, 331.f, 330.f, 225.f, 100.f, 120.f, 225.f}) {
      if (targetOnly) view.target = angle; else view.current = angle;
      ui.update(view); settle(d);
      checkFullRedraw(d, argv[1]);
      if (angle == 330.f) {
        std::snprintf(path, sizeof path, "%s/stop-cleared-%s.ppm", argv[1], targetOnly ? "target" : "current");
        save(path);
      }
    }
  }
  view = forkrot::CrowPanelView{};
  view.feedback = view.targetValid = true; view.current = view.target = 350;
  for (float maximum : {450.f, 360.f, 180.f, 450.f}) {
    view.maximum = maximum;
    ui.update(view); settle(d);
    checkFullRedraw(d, argv[1]);
  }
  // Dual readouts and all control hit areas must stay distinct at maximum values.
  auto overlaps = [](const forkrot::crowpanel::TouchRect &a,
                     const forkrot::crowpanel::TouchRect &b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
  };
  for (bool elevation : {false, true}) {
    for (bool pending : {false, true}) {
      view = forkrot::CrowPanelView{};
      view.dualAxis = true;
      view.elevation = elevation;
      view.feedback = view.targetValid = view.secondaryFeedback = view.secondaryTargetValid = true;
      view.current = elevation ? 180 : 450;
      view.secondaryCurrent = elevation ? 450 : 180;
      view.target = view.current;
      view.secondaryTarget = view.secondaryCurrent;
      view.pending = pending && !elevation;
      view.secondaryPending = pending && elevation;
      view.maximum = elevation ? 180 : 450;
      view.azDirection = 1;
      view.elDirection = -1;
      std::strcpy(view.status, "Вращение");
      ui.update(view);
      settle(d);
      checkFullRedraw(d, argv[1]);
      const auto &layout = ui.touchLayout();
      assert(!overlaps(layout.current, layout.target));
      for (unsigned i = 0; i < 4; ++i) {
        assert(layout.arrows[i].width >= 44 && layout.arrows[i].height >= 44);
        assert(!overlaps(layout.arrows[i], layout.current));
        assert(!overlaps(layout.arrows[i], layout.target));
        assert(!overlaps(layout.arrows[i], forkrot::crowpanel::ModeButton));
        for (unsigned j = 0; j < i; ++j) {
          assert(!overlaps(layout.arrows[i], layout.arrows[j]));
        }
      }
      auto *main = findLabel(elevation ? "180°" : "450°");
      assert(main);
      assert(lv_obj_get_style_text_font(main, LV_PART_MAIN) ==
             (pending ? &crowpanel_font_36 : &crowpanel_font_80));
      auto *other = findLabel(elevation ? "450° / " : " / 180°");
      auto *goal = findLabel(elevation ? "180.0°" : "450.0°");
      auto *otherGoal = findLabel(elevation ? "450.0° / " : " / 180.0°");
      assert(other && goal && otherGoal);
      auto checkRow = [&](lv_obj_t *primary, lv_obj_t *secondary,
                          const forkrot::crowpanel::TouchRect &hit) {
        auto *az = elevation ? secondary : primary;
        auto *el = elevation ? primary : secondary;
        if (lv_obj_get_x(az) + lv_obj_get_width(az) > lv_obj_get_x(el))
          std::fprintf(stderr, "Axis order: EL selected=%d pending=%d AZ='%s' x=%d w=%d EL='%s' x=%d w=%d\n",
                       elevation, pending, lv_label_get_text(az), int(lv_obj_get_x(az)),
                       int(lv_obj_get_width(az)), lv_label_get_text(el), int(lv_obj_get_x(el)),
                       int(lv_obj_get_width(el)));
        assert(lv_obj_get_x(az) + lv_obj_get_width(az) <= lv_obj_get_x(el));
        for (auto *number : {az, el}) {
          assert(hit.contains(lv_obj_get_x(number), lv_obj_get_y(number)));
          assert(hit.contains(lv_obj_get_x(number) + lv_obj_get_width(number) - 1,
                              lv_obj_get_y(number) + lv_obj_get_height(number) - 1));
        }
        const auto *azFont = lv_obj_get_style_text_font(az, LV_PART_MAIN);
        const auto *elFont = lv_obj_get_style_text_font(el, LV_PART_MAIN);
        assert(lv_obj_get_y(az) + azFont->line_height - azFont->base_line ==
               lv_obj_get_y(el) + elFont->line_height - elFont->base_line);
      };
      checkRow(main, other, layout.current);
      checkRow(goal, otherGoal, layout.target);
      assert(lv_obj_get_style_text_font(goal, LV_PART_MAIN) ==
             (pending ? &crowpanel_font_80 : &crowpanel_font_36));
      const auto &right = layout.arrows[1], &down = layout.arrows[3];
      assert(nearColor(right.x + 28, right.y + 22, 3, 0xeff5ff));
      assert(nearColor(down.x + 22, down.y + 28, 3, 0xeff5ff));
      std::snprintf(path, sizeof path, "%s/dual-%s-%s.ppm", argv[1], elevation ? "el" : "az",
                    pending ? "pending" : "normal");
      save(path);
      // Missing values keep their axis position and retain the entire touch row.
      for (bool missingPrimary : {false, true}) {
        view.feedback = view.targetValid = !missingPrimary;
        view.secondaryFeedback = view.secondaryTargetValid = missingPrimary;
        ui.update(view); settle(d); checkFullRedraw(d, argv[1]);
        main = findLabel(missingPrimary ? "—" : elevation ? "180°" : "450°");
        other = findLabel(missingPrimary ? (elevation ? "450° / " : " / 180°")
                                        : (elevation ? "— / " : " / —"));
        goal = findLabel(missingPrimary ? "—" : elevation ? "180.0°" : "450.0°",
                         ui.touchLayout().target.y);
        otherGoal = findLabel(missingPrimary ? (elevation ? "450.0° / " : " / 180.0°")
                                            : (elevation ? "— / " : " / —"),
                              ui.touchLayout().target.y);
        assert(main && other && goal && otherGoal);
        checkRow(main, other, ui.touchLayout().current);
        checkRow(goal, otherGoal, ui.touchLayout().target);
      }
    }
  }
  std::puts("PASS: red/cyan stop-line fading and removal match full redraw before the opposite limit appears");
  view.maximum = 450; view.elevation = view.dualAxis = false;
  view.feedback = true; view.current = view.target = 120; view.sourceId = 42;
  ui.update(view); lv_refr_now(d);
  assert(markerAt(120, 0xeff5ff)); // New rotator/axis/feedback starts without a fictitious sweep.
  settle(d);
  const auto idle = flushes;
  for (int i = 0; i < 10; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
  assert(flushes == idle);
  for (bool dual : {false,true}) for (bool pending : {false,true}) {
    view.dualAxis=dual;view.pending=pending;view.autoMode=!pending;
    view.secondaryFeedback=view.secondaryTargetValid=true;view.secondaryCurrent=view.secondaryTarget=180;
    for (const auto bounds : {std::pair<float,float>{-60,390},{50,500},{-3600,0},{3600,7200}}) {
      view.minimum=bounds.first;view.maximum=bounds.second;
      view.current=bounds.first;view.target=bounds.second;
      view.targetValid=true;
      ui.update(view);settle(d);checkFullRedraw(d,argv[1]);
      const auto &layout=ui.touchLayout();
      assert(layout.current.x>=60 && layout.current.x+layout.current.width<=420);
      assert(layout.target.x<480 && layout.target.x+layout.target.width<=450);
      char boundsLabel[80];std::snprintf(boundsLabel,sizeof boundsLabel,"%.1f°..%.1f°",double(view.minimum),double(view.maximum));
      assert(findLabel(boundsLabel));
      std::snprintf(path,sizeof path,"%s/range-%.0f-%s-%s.ppm",argv[1],double(bounds.first),dual?"dual":"az",pending?"pending":"normal");save(path);
    }
  }
  view.dualAxis=view.elevation=true;view.pending=false;view.autoMode=false;
  view.minimum=-30;view.maximum=210;view.current=-20;view.target=200;
  view.feedback=view.targetValid=view.secondaryFeedback=view.secondaryTargetValid=true;
  view.secondaryCurrent=350;view.secondaryTarget=380;
  ui.update(view);settle(d);checkFullRedraw(d,argv[1]);
  assert(findLabel("-30.0°..210.0°"));
  std::snprintf(path,sizeof path,"%s/custom-elevation.ppm",argv[1]);save(path);
  for (const auto *font : {&crowpanel_font_22, &crowpanel_font_28, &crowpanel_font_36,
                           &crowpanel_font_80, &crowpanel_font_112}) {
    for (const char letter : {'N', 'C'}) {
      lv_font_glyph_dsc_t glyph{};
      assert(font->get_glyph_dsc(font, &glyph, letter, 0) && glyph.box_w > 0);
    }
  }
  for (bool dual : {false, true}) for (bool elevation : {false, true}) for (bool pending : {false, true}) {
    if (!dual && elevation) continue;
    view = forkrot::CrowPanelView{};
    view.dualAxis = dual; view.elevation = elevation;
    view.uncalibrated = view.secondaryUncalibrated = true;
    view.targetValid = pending; view.pending = pending; view.target = 123;
    ui.update(view); settle(d); checkFullRedraw(d, argv[1]);
    assert(findLabel("NC") && !findLabel("NC°"));
    if (dual) assert(findLabel(elevation ? "NC / " : " / NC"));
    const auto &layout = ui.touchLayout();
    assert(!overlaps(layout.current, layout.target));
    for (const auto &arrow : layout.arrows) assert(!overlaps(arrow, layout.current));
    std::snprintf(path, sizeof path, "%s/nc-%s-%s-%s.ppm", argv[1], dual ? "dual" : "single",
                  elevation ? "el" : "az", pending ? "pending" : "normal"); save(path);
    // No connection remains a dash. Calibration restored at the same angle repaints the number.
    view.uncalibrated = view.secondaryUncalibrated = false;
    ui.update(view); settle(d); assert(!findLabel("NC") && findLabel("—"));
    view.feedback = view.secondaryFeedback = true;
    ui.update(view); settle(d); assert(findLabel("0°"));
  }
  // Both network addresses must fit without scrolling, even in the lowest
  // dual-axis confirmation layout and with maximum-length IPv4 addresses.
  view.feedback = view.secondaryFeedback = true;
  view.wifiConnected = view.wifiAp = true;
  forkrot::formatNetworkAddresses(view.ip, sizeof view.ip, "255.255.255.255", "255.255.255.255", "  ");
  for (bool dual : {false, true}) for (bool pending : {false, true}) {
    view.dualAxis = dual; view.elevation = false; view.targetValid = true;
    view.pending = pending; view.autoMode = false;
    ui.update(view); settle(d);
    auto *addresses = findLabel(view.ip);
    assert(addresses);
    lv_point_t measured;
    lv_text_get_size(&measured, view.ip, &crowpanel_font_11, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    assert(measured.x <= lv_obj_get_width(addresses));
    assert(lv_obj_get_y(addresses) + measured.y < 480);
    std::snprintf(path, sizeof path, "%s/network-%s-%s.ppm", argv[1], dual ? "dual" : "single", pending ? "pending" : "normal");
    save(path);
  }
  std::puts("PASS: simultaneous STA/AP IPv4 addresses fit all dashboard layouts");
  std::puts("PASS: NC glyphs, independent axes, offline dash, calibration restored, targets unchanged and layout");
  std::puts("PASS: LVGL marker animation/wrap/reset, independent current/target limit tails, confirmation hit/restoration, Wi-Fi, arrows, markers and idle redraw");
}
