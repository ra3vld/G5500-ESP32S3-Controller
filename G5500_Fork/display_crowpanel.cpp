#include "display.hpp"
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include "device_actions.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "network.hpp"
#include "heap_metrics.hpp"
#include "crowpanel_rgb_driver.h"
#include <algorithm>
#include <cstring>
namespace forkrot {
namespace {
portMUX_TYPE vsyncMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t vsyncs = 0;
uint32_t frames = 0, waitMaxUs = 0, syncTimeouts = 0;
bool displayFailed = false;
bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  portENTER_CRITICAL_ISR(&vsyncMux);
  ++vsyncs;
  portEXIT_CRITICAL_ISR(&vsyncMux);
  return false;
}
uint32_t vsyncCount() {
  portENTER_CRITICAL(&vsyncMux);
  const uint32_t count = vsyncs;
  portEXIT_CRITICAL(&vsyncMux);
  return count;
}
}
void Display::flushFrame(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  (void)area;
  if (!lv_display_flush_is_last(display) || displayFailed) {
    lv_display_flush_ready(display);
    return;
  }
  auto &self = *static_cast<Display *>(lv_display_get_user_data(display));
  // Direct-mode pixels is a complete back buffer. IDF writes back its cache and
  // links it into the DMA chain. Never render into the currently scanned buffer.
  self.serviceKnob();
  if (esp_lcd_panel_draw_bitmap(self.panel_, 0, 0, 480, 480, pixels) != ESP_OK) displayFailed = true;
  const uint32_t first = vsyncCount(), started = micros();
  // IDF 5.5's VSYNC callback runs before its DMA restart. Waiting for two VSYNCs
  // after submission also covers a submission racing with the first interrupt.
  while (!displayFailed && uint32_t(vsyncCount() - first) < 2) {
    if (uint32_t(micros() - started) > 150000) { ++syncTimeouts; displayFailed = true; break; }
    // Stay on the loop task: no LVGL calls or NVS writes while flushing.
    // Short knob presses must remain visible during the two-VSYNC wait.
    self.serviceKnob();
    delay(1);
  }
  waitMaxUs = std::max(waitMaxUs, uint32_t(micros() - started));
  if (displayFailed) digitalWrite(board::PIN_BACKLIGHT, LOW);
  else ++frames;
  lv_display_flush_ready(display);
}
crowpanel::DisplayStats crowpanel::displayStats() {
  return {vsyncCount(), frames, waitMaxUs, syncTimeouts, displayFailed};
}
void Display::initLocalDisplay() {
  crowpanel::beginEncoder();
  panel_ = crowpanel::beginPanel();
  if (!panel_) { diagnostics_.dbgPrintln("[CrowPanel] LCD/PSRAM/PCF8574 initialization failed"); return; }
  constexpr size_t bytes = 480 * 480 * sizeof(uint16_t);
  void *front = nullptr, *back = nullptr;
  esp_lcd_rgb_panel_event_callbacks_t callbacks{};
  callbacks.on_vsync = onVsync;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel_, 2, &front, &back) != ESP_OK ||
      esp_lcd_rgb_panel_register_event_callbacks(panel_, &callbacks, nullptr) != ESP_OK) {
    esp_lcd_panel_del(panel_); panel_ = nullptr;
    diagnostics_.dbgPrintln("[CrowPanel] framebuffer/VSYNC setup failed"); return;
  }
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  display_ = lv_display_create(480, 480);
  if (!display_) {
    esp_lcd_panel_del(panel_); panel_ = nullptr;
    diagnostics_.dbgPrintln("[CrowPanel] LVGL display allocation failed"); return;
  }
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
  lv_display_set_user_data(display_, this);
  // DMA starts on front; the first LVGL render must use back.
  lv_display_set_buffers(display_, back, front, bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(display_, flushFrame);
  ui_.create();
  ui_.update(CrowPanelView{});
  lv_refr_now(display_);
  if (!displayFailed) digitalWrite(board::PIN_BACKLIGHT, HIGH);
  diagnostics_.dbgPrintln(displayFailed ? "[CrowPanel] VSYNC timeout; display disabled" :
      "[CrowPanel] ST7701 480x480; 8MHz double DMA, VSYNC sync, fixed RGB driver; encoder ready");
}
void Display::serviceKnob() {
  if (!inputReady_) return;
  const uint32_t now = millis();
  if (uint32_t(now - lastInput_) < 5) return;
  lastInput_ = now;
  bool down = false;
  const bool healthy = crowpanel::readButton(down);
  inputHealthy_ = healthy;
  knob_.sample(healthy, down, crowpanel::encoderDelta(), now, runtime_.loggerSettings.encoderInverted);
  hardware_.controlRotator(); // Apply STOP immediately, including while awaiting VSYNC.
}
void Display::serviceTouch() {
  serviceKnob();
  const uint32_t now = millis();
  if (uint32_t(now - lastTouch_) < 10) return;
  lastTouch_ = now;
  uint16_t x = 0, y = 0;
  const bool touch = inputHealthy_ && crowpanel::readTouch(x, y);
  if (display_) {
    const auto action = touchButtons_.sample(touch, x, y);
    if (action == crowpanel::TouchAction::Stop) knob_.stop(now);
    else if (action == crowpanel::TouchAction::Mode) knob_.toggleMode(now);
    ui_.setPressed(touchButtons_.pressed());
  }
  if (knob_.modeChanged) { knob_.modeChanged = false; actions_.saveForkSettings(); }
  hardware_.controlRotator(); // Apply STOP from local input before returning.
}
void Display::updateLocalDisplay() {
  if (!display_ || displayFailed) return;
  const uint32_t now = millis();
  if (redraw_ || uint32_t(now - lastUi_) >= 100) {
    lastUi_ = now; redraw_ = false;
    const auto &controller = runtime_.controller;
    CrowPanelView view;
#if CROWPANEL_SIMULATION
    std::snprintf(view.status, sizeof view.status, "%s", controller.moving() ? "Имитация · вращение" : "Имитация · остановлен");
#endif
    if (!inputHealthy_) std::snprintf(view.status, sizeof view.status, "Ошибка кнопки / I2C");
    const unsigned axis = knob_.axis();
    view.dualAxis = controller.model == Model::G5500;
    view.elevation = axis != 0;
    view.autoMode = controller.mode != Mode::Manual; view.autoHour = controller.mode == Mode::AutoHour;
    view.feedback = CROWPANEL_SIMULATION && controller.axes[axis].calibrated;
    view.current = controller.axes[axis].current;
    view.direction = controller.axes[axis].direction;
    view.pending = controller.pendingValid && (axis ? controller.pending.hasEl : controller.pending.hasAz);
    view.targetValid = view.pending || controller.targetKnown;
    view.target = view.pending ? (axis ? controller.pending.el : controller.pending.az) : controller.axes[axis].target;
    const bool connected = WiFi.status() == WL_CONNECTED;
    const bool ap = (WiFi.getMode() & WIFI_AP) != 0;
    view.wifiConnected = connected;
    view.wifiAp = ap;
    view.wifiRssi = connected ? int(WiFi.RSSI()) : -127;
    const auto address = connected ? WiFi.localIP() : ap ? WiFi.softAPIP() : IPAddress();
    if (connected || ap) std::snprintf(view.ip, sizeof view.ip, "%s: %u.%u.%u.%u", connected ? "IP" : "AP", address[0], address[1], address[2], address[3]);
    view.bluetoothEnabled = BLUETOOTH_ENABLED != 0;
    view.bluetoothConnected = view.bluetoothEnabled && network_.bluetoothConnected();
    std::snprintf(view.heap, sizeof view.heap, "Heap: %lu КБ", static_cast<unsigned long>(heapFreeBytes() / 1024));
    ui_.update(view);
  }
  // All LVGL calls stay on the Arduino loop task; no locks or cross-task writes.
  lv_timer_handler();
}
}
#endif
