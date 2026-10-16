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
  view.dualAxis = true;
  view.targetValid = true; view.target = 210;
  view.wifiConnected = true; view.wifiAp = true; view.wifiRssi = -58;
  std::strcpy(view.ip, "IP: 192.168.3.234");
  std::strcpy(view.heap, "Heap: 123 КБ");
  ui.update(view); lv_refr_now(d);
  char path[1024]; std::snprintf(path, sizeof path, "%s/no-drive.ppm", argv[1]); save(path);
  const unsigned previous = flushes;
  for (int i = 0; i < 100; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
  assert(flushes == previous); // Unchanged dashboard must not continuously repaint.
  view.feedback = true; view.current = 450; view.target = 359.9;
  view.direction = -1;
  view.autoMode = true; view.autoHour = true;
  std::strcpy(view.status, "Поворот по цели");
  view.bluetoothEnabled = true; view.bluetoothConnected = true;
  ui.update(view); lv_refr_now(d);
  std::snprintf(path, sizeof path, "%s/layout-test.ppm", argv[1]); save(path);
  view.elevation = true; view.current = 180; view.target = 15.5;
  view.wifiConnected = false;
  std::strcpy(view.ip, "AP: 192.168.4.1");
  ui.update(view); lv_refr_now(d);
  std::snprintf(path, sizeof path, "%s/elevation.ppm", argv[1]); save(path);
  view.elevation = false; view.current = 16.5; view.target = 40;
  view.direction = 1;
  view.autoHour = false;
  std::strcpy(view.status, "Имитация · вращение");
  ui.update(view); lv_refr_now(d);
  std::snprintf(path, sizeof path, "%s/simulation.ppm", argv[1]); save(path);
  view.dualAxis = false;
  ui.update(view); lv_refr_now(d);
  std::snprintf(path, sizeof path, "%s/azimuth-only.ppm", argv[1]); save(path);
  view.dualAxis = true;
  // Render all four movement indicators and verify that stopping erases them.
  struct Motion { const char *name; bool elevation; int direction; float current, target; };
  for (const auto &motion : {Motion{"az-left", false, -1, 450, 359.9},
       Motion{"az-right", false, 1, 9, 120}, Motion{"el-down", true, -1, 180, 30},
       Motion{"el-up", true, 1, 90, 150}}) {
    view.elevation = motion.elevation;
    view.current = motion.current; view.target = motion.target;
    view.direction = 0;
    ui.update(view); lv_refr_now(d);
    const auto stopped = pixels;
    view.direction = motion.direction;
    ui.update(view); lv_refr_now(d);
    assert(pixels != stopped);
    std::snprintf(path, sizeof path, "%s/%s.ppm", argv[1], motion.name); save(path);
    const auto idle = flushes;
    for (int i = 0; i < 10; ++i) { ui.update(view); lv_tick_inc(100); lv_timer_handler(); }
    assert(flushes == idle);
    view.direction = 0;
    ui.update(view); lv_refr_now(d);
    assert(pixels == stopped);
  }
  view.elevation = false;
  // RSSI changes within one state must not repaint; disconnect/recovery must clear the slash.
  view.current = 120; view.target = 120;
  view.wifiConnected = true; view.wifiAp = false; view.wifiRssi = -79;
  std::strcpy(view.status, "Имитация · остановлен");
  std::strcpy(view.ip, "IP: 192.168.3.234");
  ui.update(view); lv_refr_now(d);
  const auto goodWifi = pixels;
  assert(!nearColor(240, 361, 14, 0x63dfb5));
  assert(!nearColor(240, 361, 14, 0xff6b6b));
  std::snprintf(path, sizeof path, "%s/wifi-good.ppm", argv[1]); save(path);
  auto wifiIdle = flushes;
  view.wifiRssi = -58;
  ui.update(view); lv_tick_inc(100); lv_timer_handler();
  assert(flushes == wifiIdle);
  view.wifiRssi = -80;
  ui.update(view); lv_refr_now(d);
  const auto weakWifi = pixels;
  assert(nearColor(240, 361, 14, 0xff6b6b));
  assert(!nearColor(240, 361, 14, 0x63dfb5));
  std::snprintf(path, sizeof path, "%s/wifi-weak.ppm", argv[1]); save(path);
  wifiIdle = flushes;
  view.wifiRssi = -90;
  ui.update(view); lv_tick_inc(100); lv_timer_handler();
  assert(flushes == wifiIdle);
  view.wifiConnected = false; view.wifiRssi = 0;
  std::strcpy(view.ip, "IP: —");
  ui.update(view); lv_refr_now(d);
  assert(pixels != weakWifi);
  assert(nearColor(228, 350, 1, 0xff6b6b));
  std::snprintf(path, sizeof path, "%s/wifi-lost.ppm", argv[1]); save(path);
  view.wifiAp = true;
  std::strcpy(view.ip, "AP: 192.168.4.1");
  ui.update(view); lv_refr_now(d);
  std::snprintf(path, sizeof path, "%s/wifi-ap.ppm", argv[1]); save(path);
  view.wifiConnected = true; view.wifiAp = false; view.wifiRssi = -60;
  std::strcpy(view.ip, "IP: 192.168.3.234");
  ui.update(view); lv_refr_now(d);
  assert(pixels == goodWifi);
  // Disabled Bluetooth is blank; an enabled adapter still reports no client.
  view.bluetoothEnabled = false; view.bluetoothConnected = false;
  ui.update(view); lv_refr_now(d);
  const auto bluetoothOff = pixels;
  assert(!nearColor(240, 383, 5, 0x8fabc3));
  std::snprintf(path, sizeof path, "%s/bluetooth-off.ppm", argv[1]); save(path);
  view.bluetoothEnabled = true;
  ui.update(view); lv_refr_now(d);
  const auto bluetoothWaiting = pixels;
  assert(pixels != bluetoothOff);
  assert(nearColor(240, 383, 5, 0x8fabc3));
  std::snprintf(path, sizeof path, "%s/bluetooth-waiting.ppm", argv[1]); save(path);
  view.bluetoothConnected = true;
  ui.update(view); lv_refr_now(d);
  assert(pixels != bluetoothWaiting);
  std::snprintf(path, sizeof path, "%s/bluetooth-connected.ppm", argv[1]); save(path);
  view.bluetoothEnabled = false;
  ui.update(view); lv_refr_now(d);
  assert(pixels == bluetoothOff);
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
  std::puts("PASS: LVGL layout, Wi-Fi states/recovery, Bluetooth visibility, movement arrows/stop, marker bearings/overlap/validity, button feedback and idle redraw");
}
