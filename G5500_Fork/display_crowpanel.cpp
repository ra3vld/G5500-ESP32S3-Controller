#include "display.hpp"
#if DISPLAY_SUPPORT && DISPLAY_KIND == DISPLAY_CROWPANEL_21
#include "device_actions.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "network.hpp"
#include "heap_metrics.hpp"
#include "storage.hpp"
#include "crowpanel_rgb_driver.h"
#include "drive_fault_text.hpp"
#include <algorithm>
#include <cstring>
#include <new>
#if CROWPANEL_USB_TRACE
#include <esp_app_desc.h>
#include <esp_system.h>
#include <esp_random.h>
#endif
namespace forkrot {
namespace {
portMUX_TYPE vsyncMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t vsyncs = 0;
uint32_t vsyncLastUs = 0, vsyncMinUs = UINT32_MAX, vsyncMaxUs = 0;
uint32_t frames = 0, waitMaxUs = 0, syncTimeouts = 0;
bool diagnosticFailed = false;
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
  if (diagnosticFailed) {
    lv_display_flush_ready(display);
    return;
  }
  auto &self = *static_cast<Display *>(lv_display_get_user_data(display));
  const void *frame=pixels;
  if(self.frameComposer_) {
    // Direct LVGL buffers contain a full logical image. Rotate only this
    // invalidated rectangle into the inactive physical frame, with IRQs enabled.
    const auto *source=reinterpret_cast<const uint16_t *>(pixels)+area->y1*480+area->x1;
    if(!self.frameComposer_->blit(area->x1,area->y1,unsigned(area->x2-area->x1+1),
        unsigned(area->y2-area->y1+1),source,480)) {
      self.displayFailed_=diagnosticFailed=true;
      self.disableBacklight();lv_display_flush_ready(display);return;
    }
    frame=self.frameComposer_->pending();
  }
  if(!lv_display_flush_is_last(display)) {lv_display_flush_ready(display);return;}
  self.serviceKnob();
  uint32_t presented = 0;
  self.flushStarted_ = micros();
  self.flushPending_ = esp_lcd_panel_draw_bitmap(self.panel_, 0, 0, 480, 480, frame) == ESP_OK &&
      crowpanel_rgb_frame_status(self.panel_, &self.flushGeneration_, &presented) == ESP_OK;
  if (!self.flushPending_) {
    self.displayFailed_=diagnosticFailed=true;
    self.disableBacklight();
    lv_display_flush_ready(display);
  }
}
crowpanel::DisplayStats crowpanel::displayStats() {
  crowpanel_rgb_restart_stats_t restarts{};
  if (diagnosticPanel) crowpanel_rgb_get_restart_stats(diagnosticPanel, &restarts);
  portENTER_CRITICAL(&vsyncMux);
  const auto count = vsyncs, minGap = vsyncMinUs, maxGap = vsyncMaxUs;
  portEXIT_CRITICAL(&vsyncMux);
  return {count, frames, waitMaxUs, syncTimeouts, diagnosticFailed,
          restarts.count, restarts.frame_buffer_mask, restarts.errors, minGap, maxGap};
}
#if CROWPANEL_USB_TRACE
void Display::serviceTrace(uint32_t now) {
  trace_.displayFailure(displayFailed_, now);
  trace_.service(now, diagnostics_.traceUsbConnected(), [this](const char *data, size_t size) {
    return diagnostics_.traceUsbWrite(reinterpret_cast<const uint8_t *>(data), size);
  });
}
#endif
void Display::initLocalDisplay() {
#if CROWPANEL_USB_TRACE
  const auto *app = esp_app_get_description();
  char firmware[80], build[48], elf[65];
  std::snprintf(firmware, sizeof firmware, "%s %s", app->project_name, app->version);
  std::snprintf(build, sizeof build, "%s %s", app->date, app->time);
  for (unsigned i = 0; i < 32; ++i)
    std::snprintf(elf + 2 * i, sizeof elf - 2 * i, "%02x", unsigned(app->app_elf_sha256[i]));
  trace_.begin((uint64_t(esp_random()) << 32) | esp_random(), firmware, build, elf);
  serviceTrace(millis());
#endif
  crowpanel::beginEncoder();
  panel_ = crowpanel::beginPanel();
  if (!panel_) { diagnostics_.dbgPrintln("[CrowPanel] LCD/PSRAM/PCF8574 initialization failed"); return; }
  constexpr size_t bytes=480*480*sizeof(uint16_t);
  void *front = nullptr, *back = nullptr;
  esp_lcd_rgb_panel_event_callbacks_t callbacks{};
  callbacks.on_vsync = onVsync;
  if (esp_lcd_rgb_panel_get_frame_buffer(panel_, 2, &front, &back) != ESP_OK ||
      esp_lcd_rgb_panel_register_event_callbacks(panel_, &callbacks, nullptr) != ESP_OK) {
    esp_lcd_panel_del(panel_); panel_ = nullptr;
    diagnostics_.dbgPrintln("[CrowPanel] framebuffer/VSYNC setup failed"); return;
  }
  if(runtime_.displaySettings.screen) {
    frameComposer_.reset(new(std::nothrow) crowpanel::FrameComposer);
    logicalFrames_[0]=heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    logicalFrames_[1]=heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!frameComposer_ || !logicalFrames_[0] || !logicalFrames_[1] ||
       !frameComposer_->begin(static_cast<uint16_t *>(front),static_cast<uint16_t *>(back),
                              480,480,runtime_.displaySettings.screen)) {
      for(auto &buffer:logicalFrames_){free(buffer);buffer=nullptr;}
      frameComposer_.reset();esp_lcd_panel_del(panel_);panel_=nullptr;
      diagnostics_.dbgPrintln("[CrowPanel] orientation buffers unavailable");return;
    }
  }
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  display_ = lv_display_create(480, 480);
  if (!display_) {
    for(auto &buffer:logicalFrames_){free(buffer);buffer=nullptr;}
    frameComposer_.reset();
    esp_lcd_panel_del(panel_); panel_ = nullptr;
    diagnostics_.dbgPrintln("[CrowPanel] LVGL display allocation failed"); return;
  }
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
  lv_display_set_user_data(display_, this);
  // Keep the zero-orientation fast path: LVGL renders directly into DMA buffers.
  lv_display_set_buffers(display_, frameComposer_?logicalFrames_[0]:back,
                         frameComposer_?logicalFrames_[1]:front,bytes,LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(display_, flushFrame);
  diagnosticPanel = panel_;
  ui_.create();
  ui_.update(CrowPanelView{});
#if CROWPANEL_USB_TRACE
  trace_.view(CrowPanelView{}, crowpanel::TouchAction::None, CrowPanelTraceState{}, millis());
#endif
  lv_refr_now(display_);
  if (!displayFailed_ && board::PIN_BACKLIGHT >= 0) digitalWrite(board::PIN_BACKLIGHT, HIGH);
  diagnostics_.dbgPrintln(displayFailed_ ? "[CrowPanel] VSYNC timeout; display disabled" :
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
  if (healthy && (down || delta)) runtime_.noteUserActivity();
#if CROWPANEL_USB_TRACE
  trace_.knob(healthy, down, delta, now);
#endif
  if (speedScreen_ || menuOpen_) {
    knob_.cancelInput(); hudKnob_.cancelInput();
    if (!healthy || (down && !speedKnobDown_)) runtime_.controller.stopNow(-1, now);
    speedKnobDown_ = down;
    hardware_.controlRotator();
    return;
  }
  speedKnobDown_ = false;
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
TouchSample Display::readTouch() {
  TouchSample sample;sample.sampledAt=millis();sample.down=crowpanel::readTouch(sample.x,sample.y,&sample.healthy);return orientTouch(sample);
}
bool Display::serviceFrame() {
  if (flushPending_) {
    uint32_t submitted = 0, presented = 0;
    if (crowpanel_rgb_frame_status(panel_, &submitted, &presented) != ESP_OK) {
      displayFailed_ = diagnosticFailed = true; disableBacklight();
      flushPending_ = false; lv_display_flush_ready(display_); return false;
    }
    const uint32_t elapsed = micros() - flushStarted_;
    if (int32_t(presented - flushGeneration_) < 0) {
      if (elapsed < 150000) return false; // Network/input keep running; no LVGL while DMA owns buffer.
      ++syncTimeouts; displayFailed_ = diagnosticFailed = true; disableBacklight();
    } else {if(frameComposer_)frameComposer_->presented();++frames;publishFrame();}
    waitMaxUs = std::max(waitMaxUs, elapsed);
    flushPending_ = false;
    lv_display_flush_ready(display_);
    if (displayFailed_) return false;
  }
  diagnosticFailed=displayFailed_;
  return !displayFailed_;
}
}
#endif
