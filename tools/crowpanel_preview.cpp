// Render the actual firmware dashboard off-device; no mock HTML or screenshots.
#include "crowpanel_ui.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
static std::array<uint16_t, 480 * 480> pixels{};
static std::array<uint16_t, 480 * 40> stripe{};
static std::array<uint16_t, 480 * 480> frame1{}, frame2{};
static bool directMode = false;
static unsigned flushes = 0;
static bool nearColor(int cx, int cy, int radius, uint32_t rgb) {
  const uint16_t expected = ((rgb >> 19) << 11) | (((rgb >> 10) & 63) << 5) | ((rgb >> 3) & 31);
  for (int y = cy - radius; y <= cy + radius; ++y)
    for (int x = cx - radius; x <= cx + radius; ++x)
      if (x >= 0 && x < 480 && y >= 0 && y < 480 && pixels[y * 480 + x] == expected) return true;
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
  view.targetValid = true; view.target = 210;
  std::strcpy(view.wifi, "Wi-Fi: -58 dBm + AP");
  std::strcpy(view.ip, "IP: 192.168.3.234");
  std::strcpy(view.heap, "Heap: 123 КБ");
  ui.update(view); lv_refr_now(d);
  char path[1024]; std::snprintf(path, sizeof path, "%s/no-drive.ppm", argv[1]); save(path);
  const unsigned previous = flushes;
  for (int i = 0; i < 100; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
  assert(flushes == previous); // Unchanged dashboard must not continuously repaint.
  view.feedback = true; view.current = 450; view.target = 359.9;
  view.autoMode = true; view.autoHour = true;
  std::strcpy(view.status, "Поворот по цели");
  std::strcpy(view.bluetooth, "Bluetooth: подключён");
  ui.update(view); lv_refr_now(d);
  std::snprintf(path, sizeof path, "%s/layout-test.ppm", argv[1]); save(path);
  view.elevation = true; view.current = 180; view.target = 15.5;
  std::strcpy(view.wifi, "Wi-Fi: нет связи · AP");
  std::strcpy(view.ip, "AP: 192.168.4.1");
  ui.update(view); lv_refr_now(d);
  std::snprintf(path, sizeof path, "%s/elevation.ppm", argv[1]); save(path);
  view.elevation = false; view.current = 16.5; view.target = 40;
  view.autoHour = false;
  std::strcpy(view.status, "Имитация · вращение");
  ui.update(view); lv_refr_now(d);
  std::snprintf(path, sizeof path, "%s/simulation.ppm", argv[1]); save(path);
  // Both markers must remain distinct at identical bearings, including AZ overlap >360.
  struct Bearing { float angle; int x, y; };
  for (const auto &bearing : {Bearing{0, 240, 13}, Bearing{90, 467, 240},
       Bearing{180, 240, 467}, Bearing{270, 13, 240}, Bearing{450, 467, 240}}) {
    view.current = view.target = bearing.angle;
    view.feedback = view.targetValid = true;
    ui.update(view); lv_refr_now(d);
    assert(nearColor(bearing.x, bearing.y, 2, 0xeff5ff));
    assert(nearColor(bearing.x, bearing.y, 11, 0x64cfff));
    const auto idle = flushes;
    for (int i = 0; i < 10; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
    assert(flushes == idle);
    view.feedback = view.targetValid = false;
    ui.update(view); lv_refr_now(d);
    assert(!nearColor(bearing.x, bearing.y, 2, 0xeff5ff));
    assert(!nearColor(bearing.x, bearing.y, 11, 0x64cfff));
  }
  ui.setPressed(forkrot::crowpanel::TouchAction::Mode); lv_refr_now(d);
  assert(nearColor(110, 78, 2, 0x365779));
  ui.setPressed(forkrot::crowpanel::TouchAction::None); lv_refr_now(d);
  assert(nearColor(110, 78, 2, 0x183329));
  std::puts("PASS: LVGL layout, marker bearings/overlap/validity, button feedback and idle redraw");
}
