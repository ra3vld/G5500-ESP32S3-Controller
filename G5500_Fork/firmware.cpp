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
#if SELECTED_BOARD == BOARD_ESP32
#include <esp_log.h>
#endif
namespace forkrot {
using namespace board;
void Application::controllerSetup() {
  beginRuntimeMetrics();
  diagnostics_.readBootReason();

  storage_.loadHud(runtime_);
  network_.beginHud();
  hardware_.beginDirections();
  if (!runtime_.hud.isHud())
    hardware_.allStop();
  hardware_.rgbSet(0, 0, 0);
#if SELECTED_BOARD == BOARD_ESP32
  // UART0 belongs to GS-232. Keep IDF radio diagnostics off that byte stream.
  esp_log_level_set("*", ESP_LOG_NONE);
#endif
  network_.beginSerial();
  diagnostics_.beginNativeUsb();
  hardware_.beginAdc();

  display_.initLocalDisplay();
  storage_.loadForkSettings(runtime_);
  loadPrefs();
  diagnostics_.dbgPrintln(String("[WIFI NVS] load=") +
                         (storage_.wifiCredentialsReadOk() ? "OK" : "namespace unavailable") +
                         ", saved SSID=" + (network_.credentials().wifiSsid.length() ? "present" : "empty"));
  if (!runtime_.hud.isHud())
    runtime_.controller.maximum = calibration_.activeAzMaxDeg();
  loadProfilePresets();
  hardware_.beginInputs();
  display_.loadTouchCalibration();

  display_.startAfterCalibrationLoad();
  diagnostics_.initRssiHistory();

  diagnostics_.dbgPrintln("");
  diagnostics_.dbgPrintln("######## G5500 CONTROLLER FORK ########");
  diagnostics_.dbgPrint("[BUILD] ");
  diagnostics_.dbgPrint(board::NAME);
  diagnostics_.dbgPrint(" | display=");
  diagnostics_.dbgPrint(DISPLAY_KIND == DISPLAY_CROWPANEL_21 ? "CROWPANEL_21" : DISPLAY_KIND == DISPLAY_ILI9341 ? "ILI9341" : DISPLAY_KIND == DISPLAY_LCD1602 ? "LCD1602" : "NONE");
  diagnostics_.dbgPrint(" | drive=");
  diagnostics_.dbgPrintln(runtime_.hud.isHud() ? "HUD remote drive"
                          : (SELECTED_BOARD == BOARD_CROWPANEL_21 && CROWPANEL_SIMULATION) ? "SIMULATED 13.4 deg/s"
                          : SELECTED_BOARD == BOARD_CROWPANEL_21 ? "NONE"
                          : DRIVE_INTERFACE == DRIVE_YAESU   ? "YAESU"
                          : DRIVE_INTERFACE == DRIVE_BTS7960 ? "BTS7960"
                                                             : "SEPARATE_PWM");
#if SELECTED_BOARD == BOARD_ESP32
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
  diagnostics_.dbgPrintln(board::BLUETOOTH_BACKEND_NAME);
  network_.startWifi();
  network_.setupLoggerUdp();

  bool timeOk = diagnostics_.syncClockFromNtp();
  diagnostics_.addResetHistoryEntry();
  diagnostics_.dbgPrint("[TIME SYNC] ");
  diagnostics_.dbgPrintln(timeOk ? "NTP OK" : "NO NTP - history entry has no exact time");

  diagnostics_.dbgPrintln("");
#if TFT_DISPLAY_ENABLED
  diagnostics_.dbgPrintln("=== G5500 " FORK_CHIP_NAME " v1.6.23 TFT TOUCH CAL ===");
#else
  diagnostics_.dbgPrintln("=== G5500 " FORK_CHIP_NAME " v1.6.23 WEB ===");
#endif
  diagnostics_.dbgPrintln("Copyright (c) UA1CFM");
  diagnostics_.dbgPrintln(runtime_.hud.config.uart
                              ? "UART0: HUD/1, 115200 8N1; GS-232 disabled on these pins"
                              : "UART0 USB: PSTROTATOR GS-232, 9600 baud (COM number assigned by Windows)");
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
      "Calibration: GS232 2-point + multi-point; separate AZ P36/P45 tables, shared EL");
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
  diagnostics_.dbgPrintln(
      "TFT SPI: MOSI GPIO10, SCK GPIO11, MISO GPIO2, TFT_CS GPIO12, DC GPIO13, RST GPIO14");
  diagnostics_.dbgPrintln("XPT2046: TOUCH_CS GPIO8, TOUCH_IRQ GPIO9; shared SPI bus");
#endif
  diagnostics_.dbgPrintln("Fork control: calibrated target movement, 300ms reverse dead-time");
  diagnostics_.dbgPrintln("Web main cards: current / target shown inline for AZ and EL");
#if TFT_DISPLAY_ENABLED
  diagnostics_.dbgPrintln("TFT: ILI9341 320x240 dashboard + XPT2046 touch calibration UI");
  diagnostics_.dbgPrintln("TFT calibration: 2-POINT / MULTI-POINT, P36/P45, AZ/EL capture + reset");
#elif DISPLAY_KIND == DISPLAY_LCD1602
  diagnostics_.dbgPrintln("LCD1602: current/target, mode/wait and PWM output bars; touch disabled");
#elif DISPLAY_KIND == DISPLAY_CROWPANEL_21
  diagnostics_.dbgPrintln("CrowPanel 2.1: RGB dashboard, encoder 20 deg, touch mode/STOP; no motor GPIO");
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
  diagnostics_.dbgPrintln("==============================");

  hardware_.startHeartbeat();
  network_.captureSetupHeap();
}
void Application::controllerLoop() {
  const uint32_t startedUs = micros();
  network_.serviceHud();
  updatePosition();
  runtime_.controller.tick(millis()); // expire the hour before accepting any external command
  if (PIN_CONFIRM >= 0 && confirmButton.sample(hardware_.buttonReleased(), millis())) {
    runtime_.controller.execute(millis());
  }
  network_.serviceLoggerUdp();
  network_.handlePstRotatorSerial(); // UART0 USB / PSTROTATOR / GS-232
  network_.handleBluetoothSerial(); // selected Bluetooth transport / GS-232B
  network_.handleRotctldTcp();       // Wi-Fi TCP 4533 / Look4Sat / Hamlib rotctld
  web_server_.serviceLiveCommands();
  hardware_.controlRotator();
  display_.serviceTouch();
  network_.serviceHud();
  hardware_.controlRotator();
  web_server_.publishLive();
  network_.serviceHeapDiagnostics();
  web_server_.service();
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
  hardware_.controlRotator();
  display_.serviceTouch();
  display_.updateLocalDisplay();
  hardware_.updateRgb();
  runtimeMetrics.recordLoop(uint32_t(micros() - startedUs));
  delay(5);
}
} // namespace forkrot
namespace {
forkrot::Application application;
}
void controllerSetup() {
  application.controllerSetup();
}
void controllerLoop() {
  application.controllerLoop();
}
