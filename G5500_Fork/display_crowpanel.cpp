#include "display.hpp"
#if DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include "device_actions.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "network.hpp"
#include "heap_metrics.hpp"
#include "storage.hpp"
#include "crowpanel_rgb_driver.h"
#include <algorithm>
#include <cstring>
namespace forkrot {
namespace {
portMUX_TYPE vsyncMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t vsyncs = 0;
uint32_t vsyncLastUs = 0, vsyncMinUs = UINT32_MAX, vsyncMaxUs = 0;
uint32_t frames = 0, waitMaxUs = 0, syncTimeouts = 0;
bool displayFailed = false;
esp_lcd_panel_handle_t diagnosticPanel = nullptr;
bool IRAM_ATTR onVsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *, void *) {
  const uint32_t at = micros();
  portENTER_CRITICAL_ISR(&vsyncMux);
  if (vsyncs) {
    const uint32_t gap = at - vsyncLastUs;
    if (gap < vsyncMinUs) vsyncMinUs = gap;
    if (gap > vsyncMaxUs) vsyncMaxUs = gap;
  }
  vsyncLastUs = at;
  ++vsyncs;
  portEXIT_CRITICAL_ISR(&vsyncMux);
  return false;
}
}
void Display::flushFrame(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  (void)area;
  if (!lv_display_flush_is_last(display) || displayFailed) {
    lv_display_flush_ready(display);
    return;
  }
  auto &self = *static_cast<Display *>(lv_display_get_user_data(display));
  // Reuse the old framebuffer only after the SRAM refill driver releases it.
  self.serviceKnob();
  uint32_t presented = 0;
  self.flushStarted_ = micros();
  self.flushPending_ = esp_lcd_panel_draw_bitmap(self.panel_, 0, 0, 480, 480, pixels) == ESP_OK &&
      crowpanel_rgb_frame_status(self.panel_, &self.flushGeneration_, &presented) == ESP_OK;
  if (!self.flushPending_) {
    displayFailed = true;
    digitalWrite(board::PIN_BACKLIGHT, LOW);
    lv_display_flush_ready(display);
  }
}
crowpanel::DisplayStats crowpanel::displayStats() {
  crowpanel_rgb_restart_stats_t restarts{};
  if (diagnosticPanel) crowpanel_rgb_get_restart_stats(diagnosticPanel, &restarts);
  portENTER_CRITICAL(&vsyncMux);
  const auto count = vsyncs, minGap = vsyncMinUs, maxGap = vsyncMaxUs;
  portEXIT_CRITICAL(&vsyncMux);
  return {count, frames, waitMaxUs, syncTimeouts, displayFailed,
          restarts.count, restarts.frame_buffer_mask, restarts.errors, minGap, maxGap};
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
  diagnosticPanel = panel_;
  ui_.create();
  ui_.update(CrowPanelView{});
  lv_refr_now(display_);
  if (!displayFailed) digitalWrite(board::PIN_BACKLIGHT, HIGH);
  diagnostics_.dbgPrintln(displayFailed ? "[CrowPanel] VSYNC timeout; display disabled" :
      "[CrowPanel] ST7701 480x480; 8MHz double framebuffer, 20-line SRAM bounce; encoder ready");
}
void Display::serviceKnob() {
  if (!inputReady_) return;
  const uint32_t now = millis();
  if (uint32_t(now - lastInput_) < 5) return;
  lastInput_ = now;
  bool down = false;
  const bool healthy = crowpanel::readButton(down);
  inputHealthy_ = healthy;
  const int delta = crowpanel::encoderDelta();
  if (runtime_.hud.isHud()) {
    hudKnob_.sample(healthy, down, delta, now, runtime_.loggerSettings.encoderInverted);
    if (hudKnob_.selectionChanged) {
      hudKnob_.selectionChanged = false;
      if (!storage_.saveHudConfig(runtime_.hud.config))
        runtime_.hud.catalogError = true;
    }
  } else
    knob_.sample(healthy, down, delta, now, runtime_.loggerSettings.encoderInverted);
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
    const bool hud = runtime_.hud.isHud();
    const bool targetAllowed = !hud || (!hudKnob_.menu && !crowpanel::HudNameButton.contains(x, y));
    const bool confirm = targetAllowed && (!hud || runtime_.hud.available()) && ui_.confirmationHit(x, y);
    const auto action = touchButtons_.sample(touch, x, y, targetAllowed, confirm);
    if (action == crowpanel::TouchAction::Confirm) {
      if (hud) hudKnob_.confirmPending(now);
      else knob_.confirmPending(now);
      redraw_ = true;
    }
    if (action == crowpanel::TouchAction::Target) {
      const float bearing = crowpanel::dialBearing(x, y);
      if (hud) hudKnob_.setBearing(bearing, now);
      else knob_.setBearing(bearing, now);
      redraw_ = true;
    }
    if (runtime_.hud.isHud()) {
      if (action == crowpanel::TouchAction::Mode)
        hudKnob_.toggleMode(now);
      if (touch && !hudTouchDown_) {
        if (crowpanel::HudNameButton.contains(x, y))
          hudKnob_.open();
        else if (hudKnob_.menu) {
          if (y >= crowpanel::HudRowTop && y < crowpanel::HudRowTop + crowpanel::HudRowHeight)
            hudKnob_.moveSelection(-1);
          else if (y >= crowpanel::HudRowTop + crowpanel::HudRowHeight && y < crowpanel::HudRowTop + 2 * crowpanel::HudRowHeight)
            hudKnob_.choose(hudKnob_.highlighted);
          else if (y >= crowpanel::HudRowTop + 2 * crowpanel::HudRowHeight && y < crowpanel::HudList.y + crowpanel::HudList.height)
            hudKnob_.moveSelection(1);
        }
      }
      hudTouchDown_ = touch;
    } else {
      if (action == crowpanel::TouchAction::Mode)
        knob_.toggleMode(now);
    }
    ui_.setPressed(touchButtons_.pressed());
  }
  if (knob_.modeChanged) { knob_.modeChanged = false; actions_.saveForkSettings(); }
  hardware_.controlRotator(); // Apply STOP from local input before returning.
}
void Display::updateLocalDisplay() {
  if (!display_ || displayFailed) return;
  if (flushPending_) {
    uint32_t submitted = 0, presented = 0;
    if (crowpanel_rgb_frame_status(panel_, &submitted, &presented) != ESP_OK) {
      displayFailed = true; digitalWrite(board::PIN_BACKLIGHT, LOW);
      flushPending_ = false; lv_display_flush_ready(display_); return;
    }
    const uint32_t elapsed = micros() - flushStarted_;
    if (int32_t(presented - flushGeneration_) < 0) {
      if (elapsed < 150000) return; // Network/input keep running; no LVGL while DMA owns buffer.
      ++syncTimeouts; displayFailed = true; digitalWrite(board::PIN_BACKLIGHT, LOW);
    } else ++frames;
    waitMaxUs = std::max(waitMaxUs, elapsed);
    flushPending_ = false;
    lv_display_flush_ready(display_);
    if (displayFailed) return;
  }
  const uint32_t now = millis();
  if (redraw_ || uint32_t(now - lastUi_) >= 20) {
    lastUi_ = now; redraw_ = false;
    const auto &controller = runtime_.controller;
    CrowPanelView view;
#if (SELECTED_BOARD == BOARD_CROWPANEL_21 && CROWPANEL_SIMULATION)
    std::snprintf(view.status, sizeof view.status, "%s", controller.moving() ? "Имитация · вращение" : "Имитация · остановлен");
#endif
    if (!inputHealthy_) std::snprintf(view.status, sizeof view.status, "Ошибка кнопки / I2C");
    const unsigned axis = runtime_.hud.isHud() ? hudKnob_.axis() : knob_.axis();
    view.hud = runtime_.hud.isHud();
    if (view.hud) {
      const auto *p = runtime_.hud.active();
      std::snprintf(view.hudName, sizeof view.hudName, "%s", p ? p->label : "Выберите поворотку");
      if (p && p->endpoint.uart)
        std::snprintf(view.hudAddress, sizeof view.hudAddress, "hud:UART");
      else if (p && p->endpoint.address) {
        const IPAddress peerAddress(p->endpoint.address);
        std::snprintf(view.hudAddress, sizeof view.hudAddress, "hud:%u.%u.%u.%u",
                      peerAddress[0], peerAddress[1], peerAddress[2], peerAddress[3]);
      }
      std::snprintf(view.status, sizeof view.status, "%s",
                    runtime_.hud.available() ? (controller.moving() ? "Вращение" : "Остановлен")
                                             : "Нет связи с повороткой");
      if(hudKnob_.busyNotice(now))std::snprintf(view.status,sizeof view.status,"Очередь команд занята");
      if(!inputHealthy_)std::snprintf(view.status,sizeof view.status,"Ошибка кнопки / I2C");
      view.hudSelector = hudKnob_.menu;
      if (view.hudSelector) {
        auto *selected = runtime_.hud.peer(hudKnob_.highlighted);
        HudPeer *previous = nullptr;
        HudPeer *last = nullptr;
        HudPeer *first = nullptr;
        HudPeer *next = nullptr;
        bool passed = false;
        for (auto *peer = runtime_.hud.peers(); peer; peer = peer->next.get())
          if (peer->saved) {
            if (!first)
              first = peer;
            if (peer == selected) {
              previous = last;
              passed = true;
            } else if (passed && !next)
              next = peer;
            last = peer;
          }
        if (!previous)
          previous = last;
        if (!next)
          next = first;
        HudPeer *rows[] = {previous, selected, next};
        for (unsigned i = 0; i < 3; ++i)
          if (rows[i]) {
            const auto *r = rows[i];
            if (r->received)
              std::snprintf(
                  view.hudRows[i], sizeof view.hudRows[i], "%s\n%.0f / %.0f · %s · %lus", r->label,
                  double(r->state.axes[0].current), double(r->state.axes[1].current),
                  r->fresh(runtime_.hud.now)
                      ? ((r->state.axes[0].direction || r->state.axes[1].direction) ? "Ход" : "Стоп")
                      : "Нет связи",
                  static_cast<unsigned long>((runtime_.hud.now - r->receivedAt) / 1000));
            else
              std::snprintf(view.hudRows[i], sizeof view.hudRows[i], "%s\nНет связи", r->label);
          }
      }
    }
    view.dualAxis = controller.model == Model::G5500;
    view.elevation = axis != 0;
    view.maximum = axis ? 180.f : controller.maximum;
    view.sourceId = view.hud ? runtime_.hud.config.selected : 0;
    view.autoMode = controller.mode != Mode::Manual; view.autoHour = controller.mode == Mode::AutoHour;
    view.feedback = (runtime_.hud.isHud() ? runtime_.hud.available() : bool((SELECTED_BOARD == BOARD_CROWPANEL_21 && CROWPANEL_SIMULATION))) &&
                    controller.axes[axis].calibrated;
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
