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
    if (board::PIN_BACKLIGHT >= 0) digitalWrite(board::PIN_BACKLIGHT, LOW);
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
  if (!displayFailed && board::PIN_BACKLIGHT >= 0) digitalWrite(board::PIN_BACKLIGHT, HIGH);
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
#if CROWPANEL_TOUCH_TRACE
  if (uint32_t(now - lastTouch_) >= 80)
    Serial.printf("[TouchGap] t=%lu gap=%lu down=%d\n", (unsigned long)now,
                  (unsigned long)(now - lastTouch_), touchButtons_.contactDown());
#endif
  lastTouch_ = now;
  uint16_t x = 0, y = 0;
  bool touchHealthy = false;
  const bool touch = inputHealthy_ && crowpanel::readTouch(x, y, &touchHealthy);
  if (display_) {
    const bool hud = runtime_.hud.isHud();
    auto layout = ui_.touchLayout();
    layout.enabled = layout.enabled && !displayFailed && touchHealthy &&
                     (!hud || (!hudKnob_.menu && (touchButtons_.contactDown() ||
                                                  !crowpanel::HudNameButton.contains(x, y))));
    const bool selectionChanged = touchSelection_ != runtime_.hud.selectionEpoch;
    if (selectionChanged) {
      layout.enabled = false;
      touchSelection_ = runtime_.hud.selectionEpoch;
    }
    auto event = touchButtons_.sample(touch, x, y, layout);
#if CROWPANEL_TOUCH_TRACE
    static uint32_t tracedGesture = 0;
    static bool tracedCancelled = false;
    if (event.manual && (event.gesture != tracedGesture || event.release)) {
      Serial.printf("[TouchManual] t=%lu gesture=%lu release=%d mask=%u xy=%u,%u input=%d healthy=%d enabled=%d selection=%d\n",
                    (unsigned long)now, (unsigned long)event.gesture, event.release, event.mask, x, y,
                    inputHealthy_, touchHealthy, layout.enabled, selectionChanged);
      if (event.gesture != tracedGesture) tracedCancelled = false;
      tracedGesture = event.gesture;
    }
#endif
    const auto action = event.action;
    if (event.manual && !selectionChanged) {
      if (runtime_.controller.manualAvailable()) {
        if (!++touchSerial_) {
          ++touchSerial_;
        }
        const auto result = runtime_.controller.manualDrive(0, 1, event.gesture, touchSerial_, event.mask, event.az,
                                                          event.el, event.release, now);
#if CROWPANEL_TOUCH_TRACE
        if (result == Result::Cancelled && !tracedCancelled) {
          Serial.printf("[TouchCancelled] t=%lu gesture=%lu release=%d xy=%u,%u\n",
                        (unsigned long)now, (unsigned long)event.gesture, event.release, x, y);
          tracedCancelled = true;
        }
#else
        (void)result;
#endif
      } else if (!event.release && hud && runtime_.hud.available()) {
        touchNotice_ = true;
        touchNoticeAt_ = now;
      }
      redraw_ = true;
    }
    if (action == crowpanel::TouchAction::Confirm) {
      if (!hud || runtime_.hud.available()) {
        runtime_.controller.executeDisplayed(now);
      }
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
      displayFailed = true; if (board::PIN_BACKLIGHT >= 0) digitalWrite(board::PIN_BACKLIGHT, LOW);
      flushPending_ = false; lv_display_flush_ready(display_); return;
    }
    const uint32_t elapsed = micros() - flushStarted_;
    if (int32_t(presented - flushGeneration_) < 0) {
      if (elapsed < 150000) return; // Network/input keep running; no LVGL while DMA owns buffer.
      ++syncTimeouts; displayFailed = true; if (board::PIN_BACKLIGHT >= 0) digitalWrite(board::PIN_BACKLIGHT, LOW);
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
    if (runtime_.controller.hardware.simulation) {
    std::snprintf(view.status, sizeof view.status, "%s", controller.moving() ? "Имитация · вращение" : "Имитация · остановлен");
    } else if (CROWPANEL_LOCAL_DRIVE) {
      std::snprintf(view.status, sizeof view.status, "%s", controller.moving() ? "Вращение" : "Остановлен");
    }
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
            if (r->received) {
              char az[16] = "NC", el[16] = "NC";
              if (r->state.axes[0].calibrated) std::snprintf(az, sizeof az, "%.0f", double(r->state.axes[0].current));
              if (r->state.axes[1].calibrated) std::snprintf(el, sizeof el, "%.0f", double(r->state.axes[1].current));
              std::snprintf(
                  view.hudRows[i], sizeof view.hudRows[i], "%s\n%s%s%s · %s · %lus", r->label,
                  az, r->state.model == Model::G5500 ? " / " : "", r->state.model == Model::G5500 ? el : "",
                  r->fresh(runtime_.hud.now)
                      ? ((r->state.axes[0].direction || r->state.axes[1].direction) ? "Ход" : "Стоп")
                      : "Нет связи",
                  static_cast<unsigned long>((runtime_.hud.now - r->receivedAt) / 1000));
            }
            else
              std::snprintf(view.hudRows[i], sizeof view.hudRows[i], "%s\nНет связи", r->label);
          }
      }
    }
    view.dualAxis = controller.model == Model::G5500;
    view.elevation = axis != 0;
    view.minimum = controller.range(axis).start;
    view.maximum = controller.range(axis).end;
    view.sourceId = view.hud ? runtime_.hud.config.selected : 0;
    view.autoMode = controller.mode != Mode::Manual; view.autoHour = controller.mode == Mode::AutoHour;
    const bool online = !view.hud || runtime_.hud.available();
    view.feedback = online && controller.axes[axis].calibrated;
    view.uncalibrated = online && !controller.axes[axis].calibrated;
    view.current = controller.axes[axis].current;
    view.direction = controller.axes[axis].direction;
    view.pending = controller.pendingValid && (axis ? controller.pending.hasEl : controller.pending.hasAz);
    view.targetValid = view.pending || controller.axes[axis].targetKnown;
    view.target = view.pending ? (axis ? controller.pending.el : controller.pending.az) : controller.axes[axis].target;
    const unsigned other = 1 - axis;
    view.secondaryFeedback = view.dualAxis && online && controller.axes[other].calibrated;
    view.secondaryUncalibrated = view.dualAxis && online && !controller.axes[other].calibrated;
    view.secondaryCurrent = controller.axes[other].current;
    view.secondaryPending = view.dualAxis && controller.pendingValid &&
                            (other ? controller.pending.hasEl : controller.pending.hasAz);
    view.secondaryTargetValid =
        view.dualAxis && (view.secondaryPending || controller.axes[other].targetKnown);
    view.secondaryTarget = view.secondaryPending
                               ? (other ? controller.pending.el : controller.pending.az)
                               : controller.axes[other].target;
    view.azDirection = controller.axes[0].direction;
    view.elDirection = controller.axes[1].direction;
    if (touchNotice_ && uint32_t(now - touchNoticeAt_) < 2000) {
      std::snprintf(view.status, sizeof view.status, "Обновите прошивку контроллера");
    } else {
      touchNotice_ = false;
    }
    const bool connected = WiFi.status() == WL_CONNECTED;
    const bool ap = (WiFi.getMode() & WIFI_AP) != 0;
    view.wifiConnected = connected;
    view.wifiAp = ap;
    view.wifiRssi = connected ? int(WiFi.RSSI()) : -127;
    const auto address = connected ? WiFi.localIP() : ap ? WiFi.softAPIP() : IPAddress();
    if (connected || ap) std::snprintf(view.ip, sizeof view.ip, "%s: %u.%u.%u.%u", connected ? "IP" : "AP", address[0], address[1], address[2], address[3]);
    view.bluetoothEnabled = runtime_.controller.hardware.bluetooth;
    view.bluetoothConnected = view.bluetoothEnabled && network_.bluetoothConnected();
    std::snprintf(view.heap, sizeof view.heap, "Heap: %lu КБ", static_cast<unsigned long>(heapFreeBytes() / 1024));
    ui_.update(view);
  }
  // All LVGL calls stay on the Arduino loop task; no locks or cross-task writes.
  lv_timer_handler();
}
}
#endif
