// G5500 Fork; original by UA1CFM. GPL-3.0.
/* Fork modifications by RA3VLD, 2026-10-12. GPLv3; see docs/upstream.md. */
/*
  G5500 ESP32-S3 Controller v1.6.23 RECONNECT SAFE TFT + XPT2046 TOUCH CAL + HC-05
  Copyright (c) UA1CFM
  Added:
    - PSTROTATOR / Yaesu GS-232B serial command handling
    - RGB activity indication:
        GREEN = RX command received from PSTROTATOR or Look4Sat/TCP
        RED   = TX response sent to PSTROTATOR or Look4Sat/TCP
        BLUE  = short heartbeat every 2 seconds
    - Non-blocking LED timing (millis), no delay used for RGB indication
    - Multi-control:
        UART0 USB = PSTROTATOR / GS-232 (Windows COM number may vary)
        TCP 4533 = Look4Sat / Hamlib rotctld + GS-232B
        Web UI remains available

  Default onboard addressable RGB LED pin: GPIO48.
  If the selected ESP32-S3 board defines RGB_BUILTIN, that definition is used.
*/

#include "firmware.hpp"
#include "application.hpp"
#include "runtime_metrics.hpp"
#include <new>
#include <esp_log.h>
namespace forkrot {
using namespace board;
namespace {
// Unlike initialized RTC data, noinit survives software resets/panics without
// relying on a successful NVS write. Ignore its undefined power-on contents.
RTC_NOINIT_ATTR uint32_t recoveryLatch;
constexpr uint32_t RecoveryCookie = 0x52335648;
}
void Application::failConfiguration(RecoveryReason reason) {
  runtime_.controller.stopNow(-1, millis());
  hardware_.applyOutputs();
  runtime_.controller.configurationLocked = true;
  __atomic_store_n(&recoveryLatch, RecoveryCookie, __ATOMIC_RELAXED);
  if (recoveryReserve_) { heap_caps_free(recoveryReserve_); recoveryReserve_ = nullptr; }
  if(reason==RecoveryReason::Allocation)network_.reportAllocationFailures();
  ota_.cancel();
  storage_.enterRecovery(runtime_, reason);
  delay(100);
  ESP.restart();
}
void Application::serviceConfigurationSafety() {
  if (runtime_.recovery) return;
  const auto reason = runtime_.memoryGuard.sample(millis(), heapFreeBytes(), heapMaxBlockBytes(),
      runtimeMetrics.allocationFailures.load(), runtime_.hardwareTrial);
  if (reason != RecoveryReason::None) { failConfiguration(reason); return; }
  if (!runtime_.hardwareTrial && runtime_.memoryGuard.ready(millis()) &&
      !storage_.finishHardwareBoot(runtime_,uint32_t(millis()-runtime_.memoryGuard.started)))
    failConfiguration(RecoveryReason::Storage);
}

void Application::controllerSetup() {
  beginRuntimeMetrics(&recoveryLatch, RecoveryCookie);
  diagnostics_.readBootReason();
  diagnostics_.addResetHistoryEntry();

  runtime_.setupStorageOk=storage_.loadSetup(runtime_);
  runtime_.controller.setupRequired=runtime_.setup.required;
  storage_.loadHud(runtime_, false);
  const auto reset = esp_reset_reason();
  const bool unexpected = reset == ESP_RST_PANIC || reset == ESP_RST_INT_WDT ||
      reset == ESP_RST_TASK_WDT || reset == ESP_RST_WDT || reset == ESP_RST_BROWNOUT;
  storage_.loadHardware(runtime_, unexpected || (reset != ESP_RST_POWERON &&
      __atomic_load_n(&recoveryLatch, __ATOMIC_RELAXED) == RecoveryCookie));
  const bool modbusReadable=storage_.loadModbus(runtime_);
  __atomic_store_n(&recoveryLatch, 0u, __ATOMIC_RELAXED);
  if(!runtime_.setupStorageOk || !modbusReadable || (!runtime_.recovery &&
      runtime_.modbus.wiring(runtime_.controller.hardware,runtime_.hud.config.uart))) {
    storage_.enterRecovery(runtime_,RecoveryReason::Invalid);
    storage_.loadHardware(runtime_,true);
    // Invalid protocol storage stays safe even if recording recovery fails.
    runtime_.recovery=true;runtime_.recoveryReason=RecoveryReason::Invalid;
    runtime_.controller.configurationLocked=true;
  }
  runtime_.hardwareToken = esp_random();
  if (!runtime_.hardwareToken) runtime_.hardwareToken = 1;
  if (runtime_.recovery) {
    runtime_.controller.remote = nullptr;
    runtime_.hud.config.role = DeviceRole::Controller;
    runtime_.hud.config.wifi = HudWifi::Host;
    runtime_.hud.config.uart = false;
  } else {
    recoveryReserve_ = heap_caps_malloc(16384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!recoveryReserve_) { failConfiguration(RecoveryReason::LowMemory); return; }
  }
  runtime_.memoryGuard.begin(millis(), runtimeMetrics.allocationFailures.load());
  if (!runtime_.recovery && runtimeMetrics.allocationFailures.load()) {
    failConfiguration(RecoveryReason::Allocation); return;
  }
  if (!runtime_.recovery) {
    storage_.loadHud(runtime_, true);
    if (runtimeMetrics.allocationFailures.load() != runtime_.memoryGuard.failures) {
      failConfiguration(RecoveryReason::Allocation); return;
    }
  }
  network_.beginHud();
  ota_.begin([](void *context) {
    auto &self=*static_cast<Application *>(context);
    self.hardware_.cancelJoystickCalibration();
    self.runtime_.controller.stopNow(-1,millis());self.hardware_.applyOutputs();
    if(self.runtime_.hud.isHud())self.network_.serviceHud();
    self.runtime_.controller.configurationLocked=true;
  },this);
  if (!runtime_.recovery) hardware_.beginDirections();
  if (!runtime_.hud.isHud())
    hardware_.allStop();
  hardware_.rgbSet(0, 0, 0);
#if FORK_CLASSIC_ESP32
  // UART0 belongs to GS-232. Keep IDF radio diagnostics off that byte stream.
  esp_log_level_set("*", ESP_LOG_NONE);
#endif
  if (!network_.beginSerial() && !runtime_.recovery) { failConfiguration(RecoveryReason::Radio); return; }
  if(runtime_.modbus.pc)esp_log_level_set("*",ESP_LOG_NONE);
  diagnostics_.beginNativeUsb();
  // Reserve the essential console before Wi-Fi can consume memory or fail.
  // Ordinary web builds allocate command sessions on first use instead.
  if (runtime_.recovery || !WEB_INTERFACE_ENABLED) network_.beginCommandApi();
  if (!runtime_.recovery) hardware_.beginAdc();

  if (!runtime_.recovery) display_.initLocalDisplay();
  if (runtime_.recovery) storage_.loadPrefs(runtime_, calibration_, network_.credentials(), true);
  else { storage_.loadForkSettings(runtime_); loadPrefs(); }
  if(runtime_.setup.fresh && runtime_.setup.generation==1 && !runtime_.setup.checked) {
    runtime_.protocol.manualFollow={true,false,true,true};runtime_.controller.restoreMode(Mode::Manual);
    if(!storage_.saveForkSettings(runtime_)){failConfiguration(RecoveryReason::Storage);return;}
  }
  diagnostics_.dbgPrintln(String("[WIFI NVS] load=") +
                         (storage_.wifiCredentialsReadOk() ? "OK" : "namespace unavailable") +
                         ", saved SSID=" + (network_.credentials().wifiSsid.length() ? "present" : "empty"));
  if (!runtime_.recovery && !runtime_.hud.isHud())
    runtime_.controller.setRange(calibration_.activeAzRange(), calibration_.state().gsAzMode, millis());
  if (!runtime_.recovery) {
    loadProfilePresets(); hardware_.beginInputs(); display_.loadTouchCalibration();
    display_.startAfterCalibrationLoad(); diagnostics_.initRssiHistory();
  }

  diagnostics_.dbgPrintln("");
  diagnostics_.dbgPrintln("######## G5500 CONTROLLER FORK ########");
  diagnostics_.dbgPrint("[BUILD] ");
  diagnostics_.dbgPrint(board::NAME);
  diagnostics_.dbgPrint(" | display=");
  diagnostics_.dbgPrint(DISPLAY_KIND == DISPLAY_ST7796 ? "ST7796" : DISPLAY_KIND == DISPLAY_GC9A01 ? "GC9A01" : DISPLAY_KIND == DISPLAY_CROWPANEL_21 ? "CROWPANEL_21" : DISPLAY_KIND == DISPLAY_ILI9341 ? "ILI9341" : DISPLAY_KIND == DISPLAY_LCD1602 ? "LCD1602" : "NONE");
  diagnostics_.dbgPrint(" | drive=");
  diagnostics_.dbgPrintln(runtime_.hud.isHud() ? "HUD remote drive"
                          : runtime_.controller.hardware.simulation ? "SIMULATED 13.4 deg/s"
                          : !runtime_.controller.hardware.hasDirections(0) ? "NONE"
                          : runtime_.controller.hardware.drive == DRIVE_YAESU   ? "YAESU"
                          : runtime_.controller.hardware.drive == DRIVE_BTS7960 ? "BTS7960"
                                                             : runtime_.controller.hardware.drive == DRIVE_DIR_PWM ? "DIR_PWM" : "SEPARATE_PWM");
#if FORK_CLASSIC_ESP32
  diagnostics_.dbgPrintln("[DIAG] /diaglog: fixed 2048-byte log; UART0 H4: RAM and Wi-Fi status");
#else
  diagnostics_.dbgPrintln("[DIAG] Web console: /diaglog, RAM ring buffer 160 lines");
#endif
  diagnostics_.dbgPrint("[RESET REASON] ");
  diagnostics_.dbgPrintln(diagnostics_.bootReasonText());
  diagnostics_.dbgPrint("[RESET CODE] ");
  diagnostics_.dbgPrintln(String((int)diagnostics_.bootReasonCode()));
  diagnostics_.dbgPrintln("[V1559] USB debug alive");
  diagnostics_.dbgPrint("Bluetooth backend: ");
  diagnostics_.dbgPrintln(runtime_.controller.hardware.backendName());
  if (!network_.startWifi()) {
    if (!runtime_.recovery) failConfiguration(RecoveryReason::Radio);
    return;
  }
  if (runtime_.recovery) return;
  if (heapFreeBytes() < MemoryGuard::HealthyFree || heapMaxBlockBytes() < MemoryGuard::HealthyBlock ||
      runtimeMetrics.allocationFailures.load() != runtime_.memoryGuard.failures) {
    failConfiguration(RecoveryReason::LowMemory); return;
  }
  network_.setupLoggerUdp();

  bool timeOk = diagnostics_.syncClockFromNtp();
  diagnostics_.updateResetHistoryTime();
  diagnostics_.dbgPrint("[TIME SYNC] ");
  diagnostics_.dbgPrintln(timeOk ? "NTP OK" : "NO NTP - history entry has no exact time");

  diagnostics_.dbgPrintln("");
#if TFT_DISPLAY_ENABLED
  diagnostics_.dbgPrintln("=== G5500 " FORK_CHIP_NAME " v1.6.23 TFT TOUCH CAL ===");
#else
  diagnostics_.dbgPrintln("=== G5500 " FORK_CHIP_NAME " v1.6.23 WEB ===");
#endif
  diagnostics_.dbgPrintln("Copyright (c) UA1CFM");
  for(unsigned i=0;i<2;++i) {
    const auto &port=i?runtime_.modbus.uart1:runtime_.modbus.uart0;
    diagnostics_.dbgPrintln(String("UART")+i+": "+port.baud+" 8"+
        (port.parity==1?"E":port.parity==2?"O":"N")+port.stopBits);
  }
#if FORK_NATIVE_USB
  diagnostics_.dbgPrintln("Native USB " FORK_USB_NAME
                          ": diagnostics + programming (COM number assigned by Windows)");
#else
  diagnostics_.dbgPrintln("UART0 bridge: GS-232 + programming; diagnostics: /diaglog");
#endif
  diagnostics_.dbgPrintln("TCP 4533: Look4Sat/Hamlib + GS-232B full command set");
  diagnostics_.dbgPrintln("R4UAB DDE Client TCP: decimal comma accepted in Hamlib P az el command");
  diagnostics_.dbgPrintln(
      "BUGFIX: Serial/Web manual or target commands cancel active GS-232 timed sequence");
  diagnostics_.dbgPrintln(
      "AP password: configurable from Web UI; empty = open, 8..63 chars = WPA2");
  diagnostics_.dbgPrintln("RSSI history: 1h/1d/1w/1m/1y, year saved daily");
  diagnostics_.dbgPrintln(
      "Reset history: last 20 boots stored in NVS with NTP UTC time when available");
  diagnostics_.dbgPrintln(
      "STOP preserves the mode; preset discards incoming movement while active");
  diagnostics_.dbgPrintln(
      "Fork modes: numerical remote targets wait in manual; jog commands always execute");
  diagnostics_.dbgPrintln(
      "Calibration: GS232 2-point + multi-point; separate AZ P36/P45/custom tables, shared EL");
  diagnostics_.dbgPrintln("ADC: 25 measurements, median of 5 samples at 20ms; one-pass arrival zone 5deg");
  diagnostics_.dbgPrintln(
      "Manual axis behavior: AZ manual cancels AZ track only; EL manual cancels EL track only");
  diagnostics_.dbgPrintln("Boot outputs: LOW preloaded before pinMode(OUTPUT)");
  diagnostics_.dbgPrintln("Web calibration: explicit GS232 2-POINT / MULTI-POINT mode selector");
  diagnostics_.dbgPrintln("Web calibration menu: collapsible, default closed");
  diagnostics_.dbgPrintln("Calibration buttons: saved points highlighted in green");
  diagnostics_.dbgPrintln(
      "Calibration data stored separately; separate reset for each mode and axis");
#if TFT_DISPLAY_ENABLED
  const auto &pins=runtime_.controller.hardware.pins;
  diagnostics_.dbgPrintln(String("TFT SPI: MOSI=")+pins.tftMosi+", SCK="+pins.tftSck+", MISO="+pins.tftMiso+
      ", CS="+pins.tftCs+", DC="+pins.tftDc+", RST="+pins.tftRst);
  diagnostics_.dbgPrintln(String("XPT2046: CS=")+pins.touchCs+", IRQ="+pins.touchIrq+"; shared SPI bus");
#endif
  diagnostics_.dbgPrintln("Fork control: calibrated target movement, configurable servo reverse pause");
  diagnostics_.dbgPrintln("Web main cards: current / target shown inline for AZ and EL");
#if TFT_DISPLAY_ENABLED
  diagnostics_.dbgPrintln("TFT: ILI9341 320x240 dashboard + XPT2046 touch calibration UI");
  diagnostics_.dbgPrintln("TFT calibration: 2-POINT / MULTI-POINT, P36/P45/custom, AZ/EL capture + reset");
#elif DISPLAY_KIND == DISPLAY_ST7796
  diagnostics_.dbgPrintln("ESP32-3248S035C: ST7796 480x320, GT911 touch, network HUD");
#elif DISPLAY_KIND == DISPLAY_GC9A01
  diagnostics_.dbgPrintln("ESP32-2424S012C: GC9A01 240x240, CST816 touch, network HUD");
#elif DISPLAY_KIND == DISPLAY_LCD1602
  diagnostics_.dbgPrintln("LCD1602: current/target, mode/wait and PWM output bars; touch disabled");
#elif DISPLAY_KIND == DISPLAY_CROWPANEL_21
  diagnostics_.dbgPrintln("CrowPanel 2.1: RGB dashboard, encoder 20 deg, touch mode/STOP");
#else
  diagnostics_.dbgPrintln("Display and touch: disabled; Web control and calibration available");
#endif
  diagnostics_.dbgPrint("[WiFi AP]  ");
  diagnostics_.dbgPrintln((WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP().toString() : "off (STA preferred)");
  if (WiFi.status() == WL_CONNECTED) {
    diagnostics_.dbgPrint("[WiFi LAN] ");
    diagnostics_.dbgPrintln(WiFi.localIP().toString());
  } else {
    diagnostics_.dbgPrintln("[WiFi LAN] not connected");
  }
  if (ETHERNET_SUPPORTED) {
    diagnostics_.dbgPrint("[Ethernet] ");
    diagnostics_.dbgPrintln(ethernetConnected() ? ethernetIP().toString() : "waiting for cable / DHCP");
  }
  diagnostics_.dbgPrintln("==============================");

  hardware_.startHeartbeat();
  network_.captureSetupHeap();
}
void Application::controllerLoop() {
  const uint32_t startedUs = micros();
  ota_.service();
  network_.serviceCommandReplies();
  if(restart_.consume(millis()) || ota_.readyToRestart()) {
    if (!storage_.serviceRuntimeSpeeds(runtime_,millis(),true))
      diagnostics_.dbgPrintln("[Speed NVS] save before restart failed");
    if(storage_.finishHardwareBoot(runtime_))ESP.restart();
  }
  if (runtime_.recovery) {
    network_.handlePstRotatorSerial();network_.serviceCommandUdp();
    network_.serviceCommandReplies();network_.serviceHeapDiagnostics();
    web_server_.service();delay(5);return;
  }
  device_api_.serviceSetup(millis());
  serviceConfigurationSafety();
  // Once an update starts, keep outputs stopped even after an upload error.
  // Only reboot can resume input processing, so buffered commands cannot replay.
  if (ota_.locked()) {
    network_.handlePstRotatorSerial();network_.serviceCommandUdp();network_.handleRotctldTcp();
    network_.serviceCommandReplies();
    web_server_.service();
    network_.serviceWifiReconnect();
    hardware_.updateRgb();
    delay(5);
    return;
  }
  // Refresh held input after rendering before HUD checks its local input lease.
  display_.serviceTouch();
  network_.serviceHud();
  updatePosition();
  runtime_.controller.tick(millis()); // expire the hour before accepting any external command
  hardware_.serviceLocalInputs();
  if (runtime_.controller.hardware.buttonGpio >= 0 && confirmButton.sample(hardware_.buttonReleased(), millis())) {
    runtime_.controller.execute(millis());
  }
  network_.serviceCommandUdp();
  network_.serviceLoggerUdp();
  network_.handlePstRotatorSerial(); // UART0 USB / PSTROTATOR / GS-232
  network_.handleBluetoothSerial(); // selected Bluetooth transport / GS-232B
  network_.handleRotctldTcp();       // Wi-Fi TCP 4533 / Look4Sat / Hamlib rotctld
  web_server_.serviceLiveCommands();
  if (runtime_.controller.controlPeriodMs() < 20) updatePosition();
  hardware_.controlRotator();
  display_.serviceTouch();
  network_.serviceHud();
  if (runtime_.controller.controlPeriodMs() < 20) updatePosition();
  hardware_.controlRotator();
  web_server_.publishLive();
  network_.serviceCommandReplies();
  network_.serviceHeapDiagnostics();
  web_server_.service();
  if (ota_.locked()) return;
  if (!storage_.serviceRuntimeSpeeds(runtime_,millis()))
    diagnostics_.dbgPrintln("[Speed NVS] deferred save failed; retry in 40 s");
  network_.serviceWifiReconnect();
  diagnostics_.serviceRssiHistory();
  if (runtime_.hud.settingsDirty) {
    runtime_.hud.settingsDirty = false;
    storage_.saveForkSettings(runtime_);
  }
  if (runtime_.hud.selectionDirty) {
    runtime_.hud.selectionDirty = false;
    if (!storage_.saveHudConfig(runtime_.hud.config)) runtime_.hud.catalogError = true;
  }
  if (runtime_.hud.presetsDirty) {
    runtime_.hud.presetsDirty = false;
    storage_.saveProfilePresets(runtime_);
  }
  if (runtime_.controller.controlPeriodMs() < 20) updatePosition();
  hardware_.controlRotator();
  display_.serviceTouch();
  display_.updateLocalDisplay();
  hardware_.updateRgb();
  runtimeMetrics.recordLoop(uint32_t(micros() - startedUs));
  delay(runtime_.controller.controlPeriodMs() < 20 ? 1 : 5);
}
} // namespace forkrot
namespace {
#if SELECTED_BOARD == BOARD_S2_LEGACY || SELECTED_BOARD == BOARD_S2_MINI
// S2's static DRAM region overlaps IRAM. Its internal byte-addressable heap
// includes additional regions; allocate once before any application GPIO setup.
forkrot::Application *application=nullptr;
#else
forkrot::Application applicationStorage;
forkrot::Application *const application=&applicationStorage;
#endif
}
void controllerSetup() {
#if SELECTED_BOARD == BOARD_S2_LEGACY || SELECTED_BOARD == BOARD_S2_MINI
  void *memory=heap_caps_malloc(sizeof(forkrot::Application),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!memory) {
    __atomic_store_n(&forkrot::recoveryLatch,forkrot::RecoveryCookie,__ATOMIC_RELAXED);
    delay(100); ESP.restart(); return;
  }
  application=new(memory) forkrot::Application;
#endif
  application->controllerSetup();
}
void controllerLoop() {
  if(application) application->controllerLoop();
}
