// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "display.hpp"
#include "panel_metrics.hpp"
#if GRAPHICAL_DISPLAY
#include "device_actions.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "network.hpp"
#include "heap_metrics.hpp"
#include "storage.hpp"
#include "drive_fault_text.hpp"
#include <algorithm>
#include <cstring>
namespace forkrot {
PanelCommandContext Display::commandContext() const {
  const auto &controller=runtime_.controller;
  return {controller.model,controller.mode,controller.rangeRevision,runtime_.hud.selectionEpoch,
          runtime_.hud.isHud()?hudKnob_.axis():knob_.axis(),controller.displayedTarget(),controller.pendingValid,controller.hasDriveError()};
}
void Display::loadTouchCalibration() {
#if DISPLAY_KIND == DISPLAY_ILI9341
  storage_.loadTouchCalibration(touchCalibration_);
#endif
}
void Display::startAfterCalibrationLoad() {
  touchButtons_.cancel();
  redraw_=true;inputReady_=true;touchSelection_=runtime_.hud.selectionEpoch;
  inputRangeRevision_=runtime_.controller.rangeRevision;inputModel_=runtime_.controller.model;
#if DISPLAY_KIND == DISPLAY_ILI9341
  if(!touchCalibration_.touchCalValid) startTouchCalibration();
#endif
}
void Display::serviceTouch() {
  if(!inputReady_ || !display_) return;
  serviceKnob();
#if CROWPANEL_USB_TRACE
  // Pump even while the display is waiting for VSYNC or has failed.
  serviceTrace(millis());
#endif
  uint32_t now = millis();
  if (uint32_t(now - lastTouch_) < 10) return;
#if CROWPANEL_TOUCH_TRACE
  if (uint32_t(now - lastTouch_) >= 80)
    Serial.printf("[TouchGap] t=%lu gap=%lu down=%d\n", (unsigned long)now,
                  (unsigned long)(now - lastTouch_), touchButtons_.contactDown());
#endif
  lastTouch_ = now;
#if DISPLAY_KIND == DISPLAY_ILI9341
  if(rendering_ && tftPage!=TFT_PAGE_DASH) return;
  if(!rendering_) serviceTouchRecoveryHold();
  if(tftPage!=TFT_PAGE_DASH) { serviceCalibrationTouch();return; }
#endif
  const auto sample=readTouch();
  ++panelMetrics.touchReads;
  if(!sample.healthy) ++panelMetrics.touchErrors;
  panelMetrics.touchHealthy=sample.healthy;panelMetrics.touchDown=sample.down;
  panelMetrics.touchX=sample.x;panelMetrics.touchY=sample.y;
  now=sample.sampledAt;
  const uint16_t x=sample.x,y=sample.y;
  touchHealthy_=sample.healthy;
  const bool touch=sample.down && sample.healthy && inputHealthy_;
  const bool touchHealthy=sample.healthy && inputHealthy_;
  if (display_) {
    const bool hud = runtime_.hud.isHud();
    if(!framePresented_) return;
    auto layout = presentedLayout_;
    const auto command=commandContext();
    const auto &requested=ui_.touchLayout();
    layout.confirmAllowed=layout.confirmAllowed && requested.confirmAllowed && layout.actionEpoch==requested.actionEpoch &&
                          presentedCommand_==command;
    if(runtime_.controller.moving()) {
      layout.stop=layout.stopArea;
      // During a partial page change either visible STOP position must work.
      if(requested.stopArea.contains(x,y)) layout.stop=requested.stopArea;
    }
    layout.enabled = layout.enabled && !displayFailed_ && touchHealthy && !runtime_.controller.movementBlocked() &&
                     presentedCommand_.sameScope(command) && (!hud || runtime_.hud.available()) &&
                     (!hud || (!hudKnob_.menu && (touchButtons_.contactDown() ||
                                                  !layout.name.contains(x, y))));
    const bool selectionChanged = touchSelection_ != runtime_.hud.selectionEpoch;
    const bool contextChanged=selectionChanged || inputRangeRevision_!=runtime_.controller.rangeRevision || inputModel_!=runtime_.controller.model;
    inputRangeRevision_=runtime_.controller.rangeRevision;inputModel_=runtime_.controller.model;
    if (contextChanged) {
      layout.enabled = false;
      touchSelection_ = runtime_.hud.selectionEpoch;
      speedScreen_ = false; speedRequested_[0] = speedRequested_[1] = -1;
    }
    // Commands use the timestamp of the input sample, before slow rendering.
    if(!touchHealthy) layout.enabled=false;
    auto event = touchButtons_.sample(touch, x, y, layout, now);
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
#if CROWPANEL_USB_TRACE
    trace_.touch(touchHealthy && inputHealthy_, touch, x, y, event, touchButtons_.pressed(), now);
#endif
    const auto action = event.action;
    if (!touch) speedRequested_[0] = speedRequested_[1] = -1;
    if (event.manual && !selectionChanged) {
      if (runtime_.controller.manualAvailable() || event.release) {
        if (!++touchSerial_) {
          ++touchSerial_;
        }
        const auto result = runtime_.controller.manualDrive(0, 1, event.gesture, touchSerial_, event.mask, event.az,
                                                          event.el, event.release, now, 100, event.absoluteSpeed);
        if (result == Result::Unavailable && hud) { touchNotice_ = true; touchNoticeAt_ = now; }
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
    if(action==panel::TouchAction::Stop) {
      runtime_.controller.stopNow(-1,now);knob_.cancelInput();hudKnob_.cancelInput();redraw_=true;
    }
    if(action==panel::TouchAction::Axis) {
      if(hud) hudKnob_.selectAxis();else knob_.selectAxis();
      touchButtons_.cancel();redraw_=true;
    }
#if DISPLAY_KIND == DISPLAY_ILI9341
    if(action==panel::TouchAction::Settings) {
      runtime_.controller.stopNow(-1,now);touchButtons_.cancel();
      settingsRequested_=true;
    }
#endif
    if (action == panel::TouchAction::SpeedOpen) {
      knob_.cancelInput(); hudKnob_.cancelInput();
      speedScreen_ = true; speedRequested_[0] = speedRequested_[1] = -1; redraw_ = true;
    }
    if (action == panel::TouchAction::SpeedClose) {
      speedScreen_ = false; redraw_ = true;
    }
    if (action == panel::TouchAction::SpeedValue) {
      auto &c = runtime_.controller;
      speedRequested_[1 - event.axis] = -1;
      if (speedRequested_[event.axis] != int(event.percent)) {
        const auto result = c.setRuntimeSpeed(event.axis, c.runtimeSpeedLimit(event.axis) * event.percent / 100.f, now);
        if (result == Result::Ok || result == Result::Deferred) speedRequested_[event.axis] = int(event.percent);
      }
      redraw_ = true;
    }
    if (action == panel::TouchAction::Confirm) {
      if (!hud || runtime_.hud.available()) {
        runtime_.controller.executeDisplayed(now);
      }
      redraw_ = true;
    }
    if (action == panel::TouchAction::Target) {
      const float bearing = layout.bearing(x,y);
      if (hud) hudKnob_.setBearing(bearing, now);
      else knob_.setBearing(bearing, now);
      redraw_ = true;
    }
    if (runtime_.hud.isHud()) {
      if (action == panel::TouchAction::Mode)
        hudKnob_.toggleMode(now);
      if (touch && !hudTouchDown_ && !speedScreen_) {
        if (layout.name.contains(x, y))
          hudKnob_.open();
        else if (hudKnob_.menu) {
          if(layout.rows[0].contains(x,y)) hudKnob_.moveSelection(-1);
          else if(layout.rows[1].contains(x,y)) hudKnob_.choose(hudKnob_.highlighted);
          else if(layout.rows[2].contains(x,y)) hudKnob_.moveSelection(1);
        }
      }
      hudTouchDown_ = touch;
    } else {
      if (action == panel::TouchAction::Mode)
        knob_.toggleMode(now);
    }

  }
  if(hudKnob_.selectionChanged) {
    hudKnob_.selectionChanged=false;
    if(!storage_.saveHudConfig(runtime_.hud.config)) runtime_.hud.catalogError=true;
  }
  if (knob_.modeChanged) { knob_.modeChanged = false; actions_.saveForkSettings(); }
  hardware_.controlRotator(); // Apply STOP from local input before returning.
}
void Display::updateLocalDisplay() {
#if CROWPANEL_USB_TRACE
  serviceTrace(millis());
#endif
  if (!display_ || displayFailed_) return;
  if (!serviceFrame()) return;
  const uint32_t now = millis();
#if DISPLAY_KIND == DISPLAY_ILI9341
  if(settingsRequested_) {settingsRequested_=false;tftPage=TFT_PAGE_CAL_HOME;tftNeedsRedraw=true;touchWaitRelease=true;drawTftPage();}
  if(tftPage!=TFT_PAGE_DASH) { rendering_=true;lv_timer_handler();rendering_=false;return; }
#endif
  if (redraw_ || uint32_t(now - lastUi_) >= 20) {
    lastUi_ = now; redraw_ = false;
    updatePanelView(now);
  }
  // All LVGL calls stay on the Arduino loop task; no locks or cross-task writes.
  const uint32_t started=micros();
  rendering_=true;lv_timer_handler();rendering_=false;
  panelMetrics.renderMaxUs=std::max(panelMetrics.renderMaxUs,uint32_t(micros()-started));
}
// Release the view/formatting stack before LVGL enters nested flush callbacks.
[[gnu::noinline]] void Display::updatePanelView(uint32_t now) {
  (void)now;
    constexpr bool compact=PanelWidth<320 || PanelHeight<320;
    const auto &controller = runtime_.controller;
    PanelView view;
    view.visible=runtime_.displaySettings.visible;
    view.movingNow=controller.moving();
    view.settingsAvailable=DISPLAY_KIND==DISPLAY_ILI9341;
    view.enabled=!controller.movementBlocked() && (!runtime_.hud.isHud() || runtime_.hud.available());
    if (runtime_.controller.hardware.simulation) {
    std::snprintf(view.status, sizeof view.status, "%s", controller.moving() ? "Имитация · вращение" : compact ? "Имитация" : "Имитация · остановлен");
    } else if (DISPLAY_KIND!=DISPLAY_CROWPANEL_21 || CROWPANEL_LOCAL_DRIVE || runtime_.hud.isHud()) {
      std::snprintf(view.status, sizeof view.status, "%s", controller.moving() ? "Вращение" : compact ? "" : "Остановлен");
    }
    if (!inputHealthy_ || !touchHealthy_) std::snprintf(view.status, sizeof view.status, "Ошибка ввода");
    const unsigned axis = runtime_.hud.isHud() ? hudKnob_.axis() : knob_.axis();
    view.hud = runtime_.hud.isHud();
    if (view.hud) {
      const auto *p = runtime_.hud.active();
      view.hudBackground = p ? p->background : 0;
      std::snprintf(view.hudName, sizeof view.hudName, "%s", p ? p->label : "Выберите поворотку");
      if (p && p->endpoint.uart)
        std::snprintf(view.hudAddress, sizeof view.hudAddress, "hud:UART");
      else if (p && p->endpoint.address) {
        const IPAddress peerAddress(p->endpoint.address);
        std::snprintf(view.hudAddress, sizeof view.hudAddress, "hud:%u.%u.%u.%u",
                      peerAddress[0], peerAddress[1], peerAddress[2], peerAddress[3]);
      }
      std::snprintf(view.status, sizeof view.status, "%s",
                    runtime_.hud.available() ? (controller.moving() ? "Вращение" : compact ? "" : "Остановлен")
                                             : "Нет связи с повороткой");
      if(hudKnob_.busyNotice(now))std::snprintf(view.status,sizeof view.status,"Очередь команд занята");
      if(!inputHealthy_ || !touchHealthy_)std::snprintf(view.status,sizeof view.status,"Ошибка ввода");
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
                      ? ((r->state.fault[0] || r->state.axes[0].motionError != MotionError::None ||
                          r->state.axes[1].motionError != MotionError::None) ? "Ошибка" :
                         (r->state.axes[0].direction || r->state.axes[1].direction) ? "Ход" : "Стоп")
                      : "Нет связи",
                  static_cast<unsigned long>((runtime_.hud.now - r->receivedAt) / 1000));
            }
            else
              std::snprintf(view.hudRows[i], sizeof view.hudRows[i], "%s\nНет связи", r->label);
          }
      }
    }
    view.dualAxis = controller.model == Model::G5500;
    view.speedScreen = speedScreen_;
    for (unsigned i = 0; i < 2; ++i) {
      view.speedAvailable[i] = controller.runtimeSpeedAvailable(i);
      const float limit = controller.runtimeSpeedLimit(i);
      view.speedPercent[i] = limit > 0 ? unsigned(std::lround(100 * controller.runtimeSpeed(i) / limit)) : 5;
      if (speedRequested_[i] >= 0 && touchButtons_.contactDown()) view.speedPercent[i] = unsigned(speedRequested_[i]);
    }
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
    view.targetValid = online && (view.pending || controller.axes[axis].targetKnown);
    view.target = view.pending ? (axis ? controller.pending.el : controller.pending.az) : controller.axes[axis].target;
    const unsigned other = 1 - axis;
    view.secondaryFeedback = view.dualAxis && online && controller.axes[other].calibrated;
    view.secondaryUncalibrated = view.dualAxis && online && !controller.axes[other].calibrated;
    view.secondaryCurrent = controller.axes[other].current;
    view.secondaryPending = view.dualAxis && controller.pendingValid &&
                            (other ? controller.pending.hasEl : controller.pending.hasAz);
    view.secondaryTargetValid =
        view.dualAxis && online && (view.secondaryPending || controller.axes[other].targetKnown);
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
    if (online) {
      view.statusFault = writeDriveFault(view.status, sizeof view.status,
          controller.axes[0].motionError,
          view.dualAxis ? controller.axes[1].motionError : MotionError::None,
          controller.fault.c_str(),
          !controller.variableSpeed(0) || controller.motionSettings[0].ready,
          !view.dualAxis || !controller.variableSpeed(1) || controller.motionSettings[1].ready);
    }
    if (!inputHealthy_ || !touchHealthy_) {
      if (view.statusFault) {
        const size_t used = std::strlen(view.status);
        std::snprintf(view.status + used, sizeof view.status - used, "; ошибка ввода");
      } else std::snprintf(view.status, sizeof view.status, "Ошибка ввода");
      view.statusFault = true;
    }
    const bool connected = WiFi.status() == WL_CONNECTED;
    const bool ap = (WiFi.getMode() & WIFI_AP) != 0;
    view.wifiConnected = connected;
    view.wifiAp = ap;
    view.wifiRssi = connected ? int(WiFi.RSSI()) : -127;
    network_.formatAddresses(view.ip, sizeof view.ip, "  ",view.shows(DisplayLocalAddress),view.shows(DisplayApAddress));
    if(compact) {
      if(auto *sta=std::strstr(view.ip,"STA: "))
        std::memmove(sta,sta+5,std::strlen(sta+5)+1);
    }
    view.bluetoothEnabled = runtime_.controller.hardware.bluetooth;
    view.bluetoothConnected = view.bluetoothEnabled && network_.bluetoothConnected();
    std::snprintf(view.heap, sizeof view.heap, "Heap: %lu КБ", static_cast<unsigned long>(heapFreeBytes() / 1024));
    const auto command=commandContext();
    if(!(requestedCommand_==command)) {
      // Publish even a command change with unchanged rounded text, without
      // redrawing the whole panel. Widgets invalidate their own changed areas.
      const lv_area_t pixel{0,0,0,0};lv_obj_invalidate_area(ui_.root(), &pixel);
    }
    requestedCommand_=command;
    ui_.setPressed(touchButtons_.pressed());
    ui_.update(view);
#if CROWPANEL_USB_TRACE
    CrowPanelTraceState state;
    state.model = int(controller.model); state.mode = int(controller.mode);
    state.simulation = controller.hardware.simulation; state.online = online;
    state.displayFailed = displayFailed_;
    for (unsigned i = 0; i < 2; ++i) {
      const auto &a = controller.axes[i]; auto &s = state.axes[i];
      s.current = a.current; s.target = a.target;
      s.calibrated = a.calibrated; s.targetKnown = a.targetKnown;
      s.direction = a.direction; s.motionPhase = int(a.motionPhase); s.motionError = int(a.motionError);
      s.outputPercent = a.speedOutputPercent;
      s.measuredVelocity = a.measuredVelocity; s.requestedVelocity = a.requestedVelocity;
    }
    trace_.view(view, touchButtons_.pressed(), state, now);
#endif
}

}
#endif
