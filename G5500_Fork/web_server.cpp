#include "motion_json.hpp"
// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "web_server.hpp"
#include "web_page.hpp"
#include "diagnostic_pages.hpp"
#include "web_assets.hpp"
#include "web_encoding.hpp"
#include "tcp_memory.hpp"
#include "runtime_metrics.hpp"
#include "hardware_form.hpp"
#include <esp_timer.h>
namespace forkrot {
using namespace board;
void WebInterface::sendHtml(const char *raw, size_t rawSize, const unsigned char *gzip, size_t gzipSize) {
  const String encoding = web_.header("Accept-Encoding");
  web_.sendHeader("Vary", "Accept-Encoding");
  if (acceptsGzip(encoding.c_str())) {
    web_.sendHeader("Content-Encoding", "gzip");
    web_.sendPage(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(gzip), gzipSize);
  } else {
    web_.sendPage(200, "text/html; charset=utf-8", raw, rawSize);
  }
}
void WebInterface::setupWeb() {
  web_.setRequestHandler([](void *context, std::string_view request) {
    auto &self = *static_cast<WebInterface *>(context);
    return self.handleHardwareRequest(request) || (!self.runtime_.recovery && self.handleOtaRequest(request));
  }, this);
  if (runtime_.recovery) { web_.begin(); return; }
  ota_.begin([](void *context) {
    auto &self = *static_cast<WebInterface *>(context);
    self.runtime_.controller.stopNow(-1, millis());
    self.hardware_.applyOutputs();
    // Send the HUD STOP before pausing command processing for the update.
    if (self.runtime_.hud.isHud()) self.network_.serviceHud();
  }, this);

  web_.on("/api/hardware/probe", HTTP_POST, [this] {
    uint32_t token = 0, generation = 0;
    const bool ok = hardwareUnsigned(web_.arg("token").c_str(), token) &&
        hardwareUnsigned(web_.arg("generation").c_str(), generation) &&
        token == runtime_.hardwareToken && generation == runtime_.hardwareRecord.generation &&
        runtime_.hardwareTrial && runtime_.memoryGuard.ready(millis()) &&
        storage_.probeHardwareStorage(runtime_);
    runtime_.hardwareProbe = ok;
    web_.send(ok ? 200 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"not_ready\"}");
  });
  static const char *requestHeaders[] = {"Accept-Encoding"};
  web_.collectHeaders(requestHeaders, 1);
  web_.on("/", HTTP_GET, [this] {
    web_.sendHeader("Cache-Control", "no-store");
    sendHtml(WEB_PAGE, sizeof WEB_PAGE - 1, WEB_PAGE_GZIP, sizeof WEB_PAGE_GZIP);
  });
  web_.on("/diaglog", HTTP_GET,
          [this] { sendHtml(DIAG_PAGE, sizeof DIAG_PAGE - 1, DIAG_PAGE_GZIP, sizeof DIAG_PAGE_GZIP); });
  web_.on("/api/diaglog", HTTP_GET,
          [this] {
#if SELECTED_BOARD == BOARD_ESP32
            const size_t count = diagnostics_.copyLog(statusBuffer_, sizeof statusBuffer_);
            web_.sendBuffer(200, "text/plain; charset=utf-8", statusBuffer_, count);
#else
            web_.send(200, "text/plain; charset=utf-8", diagnostics_.diagSnapshot());
#endif
          });
  web_.on("/api/diaglog/clear", HTTP_GET, [this] {
    diagnostics_.diagClear();
    web_.send(200, "text/plain", "OK");
  });
  web_.on("/resetdiag", HTTP_GET,
          [this] { sendHtml(RESET_DIAG_PAGE, sizeof RESET_DIAG_PAGE - 1, RESET_DIAG_PAGE_GZIP, sizeof RESET_DIAG_PAGE_GZIP); });
  web_.on("/adc", HTTP_GET,
          [this] { sendHtml(ADC_TEST_PAGE, sizeof ADC_TEST_PAGE - 1, ADC_TEST_PAGE_GZIP, sizeof ADC_TEST_PAGE_GZIP); });
  web_.on("/api/adcdiag", HTTP_GET, [this] {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    int azRaw = 0, azMv = 0, elRaw = 0, elMv = 0;
    hardware_.adcDiagRead(PIN_AZ_ADC, azRaw, azMv);
    if (runtime_.controller.model == forkrot::Model::G5500) {
      hardware_.adcDiagRead(PIN_EL_ADC, elRaw, elMv);
    }
    String j = String("{\"hasEl\":") +
               (runtime_.controller.model == forkrot::Model::G5500 ? "true" : "false") +
               ",\"azRaw\":" + String(azRaw) + ",\"azMv\":" + String(azMv) +
               ",\"elRaw\":" + String(elRaw) + ",\"elMv\":" + String(elMv) + "}";
    web_.send(200, "application/json", j);
  });
  web_.on("/api/move", HTTP_GET, [this] {
    auto r = callApi("/api/move");
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    web_.send(r.status, "application/json", r.body.c_str());
  });
  web_.on("/api/target", HTTP_GET, [this] {
    actions_.updatePosition();
    auto response = callApi("/api/target");
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    runtime_.targetValid = runtime_.controller.targetKnown;
    web_.send(response.status, "application/json", response.body.c_str());
  });
  web_.on("/api/azrange", HTTP_POST, [this] {
    if (runtime_.hud.isHud()) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    const String profile = web_.arg("profile");
    const uint8_t mode = profile == "p36" ? AZ_P36 : profile == "p45" ? AZ_P45 : AZ_CUSTOM;
    float start=0, span=mode == AZ_P36 ? 360.f : 450.f;
    const bool custom = profile == "custom";
    if ((!custom && profile != "p36" && profile != "p45") ||
        (custom && (!number(web_.arg("start").c_str(),start) || !number(web_.arg("span").c_str(),span))) ||
        !AzRange::tenth(span) || !validAzProfile(mode,{start,start+span})) {
      web_.send(400,"application/json","{\"error\":\"invalid_range\"}"); return;
    }
    const bool ok=actions_.configureAzRange(mode,start,span);
    web_.send(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"error\":\"calibration_save_failed\"}");
  });
  web_.on("/api/azcal", HTTP_GET, [this] {
    if (runtime_.hud.isHud()) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    JsonWriter j(statusBuffer_,sizeof statusBuffer_);
    calibration_.writeAzJson(j); sendJson(j);
  });
  web_.on("/api/azcal", HTTP_POST, [this] {
    if (runtime_.hud.isHud()) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    float angle; const String op=web_.arg("op");
    Calibration candidate(runtime_.controller); candidate.state()=calibration_.state();
    if (!number(web_.arg("angle").c_str(),angle) || !candidate.editAzPoint(op.c_str(),angle,0)) {
      web_.send(400,"application/json","{\"error\":\"invalid_point\"}"); return;
    }
    const bool ok=actions_.editAzPoint(op.c_str(),angle);
    web_.send(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"error\":\"calibration_save_failed\"}");
  });
  web_.on("/api/elrange", HTTP_POST, [this] {
    if (runtime_.hud.isHud() || runtime_.controller.model != Model::G5500) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    const String profile = web_.arg("profile");
    const uint8_t mode = profile == "standard" ? EL_STANDARD : EL_CUSTOM;
    float start=0, span=180.f;
    const bool custom = profile == "custom";
    if ((!custom && profile != "standard") ||
        (custom && (!number(web_.arg("start").c_str(),start) || !number(web_.arg("span").c_str(),span))) ||
        !AzRange::tenth(span) || !validElProfile(mode,{start,start+span})) {
      web_.send(400,"application/json","{\"error\":\"invalid_range\"}"); return;
    }
    const bool ok=actions_.configureElRange(mode,start,span);
    web_.send(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"error\":\"calibration_save_failed\"}");
  });
  web_.on("/api/elcal", HTTP_GET, [this] {
    if (runtime_.hud.isHud() || runtime_.controller.model != Model::G5500) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    JsonWriter j(statusBuffer_,sizeof statusBuffer_);
    calibration_.writeElJson(j); sendJson(j);
  });
  web_.on("/api/elcal", HTTP_POST, [this] {
    if (runtime_.hud.isHud() || runtime_.controller.model != Model::G5500) { web_.send(403,"application/json","{\"error\":\"controller_settings_only\"}"); return; }
    float angle; const String op=web_.arg("op");
    Calibration candidate(runtime_.controller); candidate.state()=calibration_.state();
    if (!number(web_.arg("angle").c_str(),angle) || !candidate.editElPoint(op.c_str(),angle,0)) {
      web_.send(400,"application/json","{\"error\":\"invalid_point\"}"); return;
    }
    const bool ok=actions_.editElPoint(op.c_str(),angle);
    web_.send(ok?200:503,"application/json",ok?"{\"ok\":true}":"{\"error\":\"calibration_save_failed\"}");
  });
  web_.on("/api/cal", HTTP_GET, [this] {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    auto &calibrationState = calibration_.state();
    // MULTI-POINT. AZ is written only to the currently selected P36/P45 table.
    String p = web_.arg("p");
    if (runtime_.controller.model == forkrot::Model::G800DXA && p.startsWith("el")) {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    int a = hardware_.adcAvgMv(PIN_AZ_ADC), e = hardware_.adcAvgMv(PIN_EL_ADC);
    if (p.startsWith("el") && calibrationState.elProfile == EL_CUSTOM) { web_.send(400,"text/plain","USE /api/elcal"); return; }
    if (p.startsWith("az")) {
      if (calibrationState.gsAzMode == AZ_CUSTOM) { web_.send(400,"text/plain","USE /api/azcal"); return; }
      int *tbl = (calibrationState.gsAzMode == 36) ? calibrationState.az36CalMv
                                                   : calibrationState.az45CalMv;
      uint8_t n = (calibrationState.gsAzMode == 36) ? AZ36_CAL_N : AZ45_CAL_N;
      int idx = -1;
      if (p == "az0") {
        idx = 0;
      } else if (p == "az90") {
        idx = 1;
      } else if (p == "az180") {
        idx = 2;
      } else if (p == "az270") {
        idx = 3;
      } else if (p == "az360") {
        idx = 4;
      } else if (p == "az450") {
        idx = 5;
      }
      if (idx < 0 || idx >= n) {
        web_.send(400, "text/plain", "POINT NOT IN ACTIVE AZ RANGE");
        return;
      }
      tbl[idx] = a;
    } else if (p == "el0") {
      calibrationState.elCalMv[0] = e;
    } else if (p == "el45") {
      calibrationState.elCalMv[1] = e;
    } else if (p == "el90") {
      calibrationState.elCalMv[2] = e;
    } else if (p == "el135") {
      calibrationState.elCalMv[3] = e;
    } else if (p == "el180") {
      calibrationState.elCalMv[4] = e;
    } else {
      web_.send(400, "text/plain", "BAD POINT");
      return;
    }
    const bool saved = actions_.saveCal();
    web_.send(saved ? 200 : 503, "text/plain", saved ? "MULTI SAVED" : "CALIBRATION SAVE FAILED");
  });

  web_.on("/api/cal2", HTTP_GET, [this] {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    String p = web_.arg("p");
    p.toUpperCase();
    if (runtime_.controller.model == forkrot::Model::G800DXA && (p == "O2" || p == "F2")) {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    if (p == "O" || p == "O2" || p == "F" || p == "F2") {
      actions_.captureGs232Cal(p);
      const bool saved = runtime_.controller.fault != "calibration_save_failed";
      web_.send(saved ? 200 : 503, "text/plain", saved ? "2-POINT SAVED" : "CALIBRATION SAVE FAILED");
    } else {
      web_.send(400, "text/plain", "BAD COMMAND");
    }
  });

  web_.on("/api/calmode", HTTP_GET, [this] {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    String m = web_.arg("m");
    m.toLowerCase();
    if (m == "2" || m == "gs232" || m == "2point") {
      actions_.setCalibrationMode(CAL_GS232_2POINT);
    } else if (m == "multi") {
      actions_.setCalibrationMode(CAL_MULTI_POINT);
    } else {
      web_.send(400, "text/plain", "BAD MODE");
      return;
    }
    calibration_.state().cal.valid = calibration_.selectedCalibrationValid();
    const bool saved = runtime_.controller.fault != "calibration_save_failed";
    web_.send(saved ? 200 : 503, "text/plain", saved ? calibration_.calibrationModeName() : "CALIBRATION SAVE FAILED");
  });

  web_.on("/api/azmode", HTTP_GET, [this] {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    int m = web_.arg("m").toInt();
    if (m != 36 && m != 45) {
      web_.send(400, "text/plain", "BAD AZ MODE");
      return;
    }
    const bool saved = actions_.configureAzRange(uint8_t(m), 0, m == 36 ? 360 : 450);
    web_.send(saved ? 200 : 503, "text/plain", saved ? (m == 36 ? "P36" : "P45") : "CALIBRATION SAVE FAILED");
  });

  web_.on("/api/calreset", HTTP_GET, [this] {
    if (runtime_.hud.isHud()) {
      web_.send(403, "application/json", "{\"error\":\"controller_settings_only\"}");
      return;
    }
    const String axis = web_.arg("axis"), mode = web_.arg("mode");
    if ((axis != "az" && axis != "el") || (mode != "2" && mode != "multi") ||
        (axis == "el" && runtime_.controller.model != Model::G5500)) {
      web_.send(400,"text/plain","BAD AXIS OR MODE"); return;
    }
    if(axis == "az") calibration_.resetAz(mode == "multi");
    else calibration_.resetEl(mode == "multi");
    const bool saved=actions_.saveCal();
    web_.send(saved?200:503,"text/plain",saved?"RESET OK":"CALIBRATION SAVE FAILED");
  });
  const auto saveWifi = [this] {
    if (runtime_.hud.config.wifi == HudWifi::Host) {
      web_.send(409, "text/plain; charset=utf-8", "Для подключения к роутеру выберите обычный режим Wi-Fi в разделе «Пульт и связь».");
      return;
    }
    const String ssid = web_.arg("ssid"), password = web_.arg("pass");
    const auto result = storage_.saveWifi(ssid, password);
    if (result != WifiSaveResult::Ok) {
      diagnostics_.dbgPrintln(String("[WIFI NVS] save failed, stage=") +
                             (result == WifiSaveResult::InvalidSsid ? "invalid_ssid" :
                              result == WifiSaveResult::InvalidPassword ? "invalid_password" :
                              result == WifiSaveResult::OpenFailed ? "open" :
                              result == WifiSaveResult::WriteFailed ? "write" : "verify"));
      const bool invalid = result == WifiSaveResult::InvalidSsid || result == WifiSaveResult::InvalidPassword;
      const char *message = result == WifiSaveResult::InvalidSsid ? "SSID должен содержать от 1 до 32 байт." :
                            result == WifiSaveResult::InvalidPassword ? "Пароль должен содержать 8–63 символа, 64 шестнадцатеричные цифры или быть пустым для открытой сети." :
                            "Не удалось сохранить настройки Wi-Fi. Подключение не изменено.";
      web_.send(invalid ? 400 : 500, "text/plain; charset=utf-8", message);
      return;
    }
    diagnostics_.dbgPrintln("[WIFI NVS] credentials saved and verified; connecting with setup AP retained");
    network_.connectWifi(ssid, password);
    web_.send(200, "text/plain; charset=utf-8", "Настройки Wi-Fi сохранены. Подключаемся к сети; полученный IP появится на этой странице.");
  };
  web_.on("/api/wifi", HTTP_POST, saveWifi);
  web_.on("/api/wifi", HTTP_GET, saveWifi);
  web_.on("/api/ap", HTTP_GET, [this] {
    String p = web_.arg("pass");
    if (p.length() != 0 && (p.length() < 8 || p.length() > 63)) {
      web_.send(400, "text/plain", "AP password must be empty or 8..63 characters");
      return;
    }
    storage_.saveApPassword(p);
    web_.send(200, "text/plain", p.length() ? "AP password saved" : "AP set to open");
    restart_.request(millis());
  });
  web_.on("/api/resetreason", HTTP_GET, [this] {
    String j = "{\"reason\":\"" + diagnostics_.bootReasonText() +
               "\",\"code\":" + String(diagnostics_.bootReasonCode()) + "}";
    web_.send(200, "application/json", j);
  });
  web_.on("/api/resethistory", HTTP_GET,
          [this] {
            JsonWriter j(statusBuffer_, sizeof statusBuffer_);
            storage_.writeResetHistoryJson(j);
            sendJson(j);
          });
  web_.on("/api/resethistory/clear", HTTP_GET, [this] {
    storage_.clearResetHistory();
    web_.send(200, "text/plain", "OK");
  });
  web_.on("/api/rssi", HTTP_GET, [this] {
    const String range = web_.arg("range");
    const auto period = range == "day" ? RssiPeriod::Day : range == "week" ? RssiPeriod::Week :
                        range == "month" ? RssiPeriod::Month : range == "year" ? RssiPeriod::Year : RssiPeriod::Hour;
    JsonWriter j(statusBuffer_, sizeof statusBuffer_);
    diagnostics_.writeHistoryJson(j, period);
    sendJson(j);
  });
  web_.on("/api/status", HTTP_GET, [this] {
    web_.sendHeader("Cache-Control", "no-store");
    auto &calibrationState = calibration_.state();
    actions_.updatePosition();
    bool staOk = (WiFi.status() == WL_CONNECTED);
    String staIp = staOk ? WiFi.localIP().toString() : "-";
    const bool apEnabled = (WiFi.getMode() & WIFI_AP) != 0;
    String apIp = apEnabled ? WiFi.softAPIP().toString() : "-";
    String ip = staOk ? staIp : apIp;
    const char *mode = staOk ? (apEnabled ? "AP + Wi-Fi Client" : "Wi-Fi Client") : apEnabled ? "Access Point" : "Disconnected";
    int rssi = staOk ? WiFi.RSSI() : 0;
    String signal = !staOk
                        ? "-"
                        : (rssi >= -55 ? "Excellent"
                                       : (rssi >= -67 ? "Good" : (rssi >= -75 ? "Fair" : "Weak")));
    String rssiJson = staOk ? String(rssi) : "null";
    uint32_t targetAge = runtime_.targetValid ? (millis() - runtime_.lastTargetRxMs) : 0;
    const int *azTbl =
        (calibrationState.gsAzMode == 36) ? calibrationState.az36CalMv : calibrationState.az45CalMv;
    int azN = (calibrationState.gsAzMode == 36) ? AZ36_CAL_N : AZ45_CAL_N;
    if (runtime_.hud.isHud())
      calibrationState.gsAzMode = runtime_.controller.azProfile;
    bool azLin =
        runtime_.hud.isHud() ? runtime_.controller.axes[0].calibrated : calibration_.activeAzLinearValid();
    bool azMul = calibration_.activeAzMultiValid();
    int az450v = (azN > 5) ? azTbl[5] : -1;
    int az2z = calibration_.activeAzOSet() ? calibration_.activeAz2Zero() : -1;
    int az2f = calibration_.activeAzFSet() ? calibration_.activeAz2Full() : -1;
    calibrationState.cal.valid = calibration_.selectedCalibrationValid();

    char btMac[18];
    network_.bluetoothMac(btMac);
    JsonWriter j(statusBuffer_, sizeof statusBuffer_);
    j << "{\"livePort\":" << (live_.started() ? 81 : 0);
    live_.writeDiagnostics(j);
    j << ",\"az\":" << runtime_.currentAz << ",\"el\":" << runtime_.currentEl
      << ",\"targetAz\":" << runtime_.targetAz << ",\"targetEl\":" << runtime_.targetEl
      << ",\"targetValid\":" << (runtime_.targetValid ? "true" : "false") << ",\"targetAgeMs\":" << targetAge
      << ",\"calibrated\":" << (calibrationState.cal.valid ? "true" : "false") << ",\"selectedCalOk\":"
      << ((runtime_.hud.isHud()
               ? runtime_.controller.axes[0].calibrated &&
                     (runtime_.controller.model == Model::G800DXA || runtime_.controller.axes[1].calibrated)
               : calibration_.selectedCalibrationValid())
              ? "true"
              : "false")
      << ",\"azMode\":" << calibrationState.gsAzMode << ",\"azLinear\":" << (azLin ? "true" : "false")
      << ",\"azLinear36\":" << (calibrationState.az36LinearValid ? "true" : "false")
      << ",\"azLinear45\":" << (calibrationState.az45LinearValid ? "true" : "false")
      << ",\"elLinear\":" << (calibration_.activeElLinearValid() ? "true" : "false")
      << ",\"azMulti\":" << (azMul ? "true" : "false")
      << ",\"azMulti36\":" << (calibrationState.az36MultiValid ? "true" : "false")
      << ",\"azMulti45\":" << (calibrationState.az45MultiValid ? "true" : "false")
      << ",\"elMulti\":" << (calibration_.activeElMultiValid() ? "true" : "false")
      << ",\"calMode\":" << jsonQuoted(calibrationState.calibrationMode == CAL_MULTI_POINT ? "multi" : "2")
      << ",\"az2p0\":" << az2z << ",\"az2pf\":" << az2f
      << ",\"el2p0\":" << (calibration_.activeElOSet() ? calibration_.activeEl2Zero() : -1)
      << ",\"el2pf\":" << (calibration_.activeElFSet() ? calibration_.activeEl2Full() : -1)
      << ",\"motion\":" << jsonQuoted(hardware_.motion().c_str()) << ",\"azp0\":" << azTbl[0]
      << ",\"azp90\":" << azTbl[1] << ",\"azp180\":" << azTbl[2] << ",\"azp270\":" << azTbl[3]
      << ",\"azp360\":" << azTbl[4] << ",\"azp450\":" << az450v << ",\"elp0\":" << calibrationState.elCalMv[0]
      << ",\"elp45\":" << calibrationState.elCalMv[1] << ",\"elp90\":" << calibrationState.elCalMv[2]
      << ",\"elp135\":" << calibrationState.elCalMv[3] << ",\"elp180\":" << calibrationState.elCalMv[4]
      << ",\"ip\":" << jsonQuoted(ip.c_str()) << ",\"apIp\":" << jsonQuoted(apIp.c_str())
      << ",\"staIp\":" << jsonQuoted(staIp.c_str()) << ",\"wifiMode\":" << jsonQuoted(mode)
      << ",\"connectedSsid\":" << jsonQuoted(staOk ? WiFi.SSID().c_str() : "")
      << ",\"wifiConnecting\":" << (network_.wifiConnecting() ? "true" : "false")
      << ",\"ssid\":" << jsonQuoted(network_.credentials().wifiSsid.c_str())
      << ",\"rssi\":" << rssiJson.c_str() << ",\"signal\":" << jsonQuoted(signal.c_str())
      << ",\"btConnected\":" << (network_.bluetoothConnected() ? "true" : "false")
      << ",\"btLinkConnected\":" << (network_.bluetoothLinkConnected() ? "true" : "false")
      << ",\"btMac\":" << jsonQuoted(btMac)
      << ",\"resetReason\":" << jsonQuoted(diagnostics_.bootReasonText().c_str())
      << ",\"uptimeSeconds\":" << uint32_t(esp_timer_get_time() / 1000000)
      << ",\"heapFree\":" << heapFreeBytes() << ",\"heapMinFree\":" << heapMinBytes()
      << ",\"heapMaxBlock\":" << heapMaxBlockBytes()
      << ",\"httpWriteFailures\":" << web_.diagnostics().writeFailures;
    j << ",\"httpReadFailures\":" << web_.readFailures() << ",\"httpRejected\":" << web_.rejectedRequests()
      << ",\"httpAccepted\":" << web_.acceptedRequests() << ",\"httpRequests\":" << web_.diagnostics().requests;
    const auto tcp = tcpMemory();
    j << ",\"tcpStatsAvailable\":" << (tcp.available ? "true" : "false")
      << ",\"tcpActive\":" << tcp.active << ",\"tcpClosing\":" << tcp.closing << ",\"tcpTimeWait\":" << tcp.timewait;
    j << ",\"allocFailures\":" << runtimeMetrics.allocationFailures.load()
      << ",\"maxFailedAllocation\":" << runtimeMetrics.largestFailedAllocation.load()
      << ",\"loopMaxUs\":" << runtimeMetrics.loopMaxUs << ",\"loopsOver20ms\":" << runtimeMetrics.loopsOver20ms;
    runtime_.hud.writeStatus(j);
    if (!writeStateJson(j, runtime_.controller, runtime_.protocol, runtime_.loggerSettings, millis(), true)) {
      web_.send(503, "application/json", "{\"error\":\"status_too_large\"}");
      return;
    }
    web_.sendBuffer(200, "application/json", j.data(), j.size());
  });
  setupForkWeb();
  setupHudWeb();
  web_.begin();
  if (!live_.begin()) diagnostics_.dbgPrintln("[LIVE] WebSocket unavailable; HTTP fallback");
}

void WebInterface::setupForkWeb() {
  web_.on("/api/move", HTTP_POST, [this] {
    auto r = callApi("/api/move");
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    web_.send(r.status, "application/json", r.body.c_str());
  });
  web_.on("/api/target", HTTP_POST, [this] {
    actions_.updatePosition();
    auto r = callApi("/api/target");
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    if (r.status == 200) {
      runtime_.targetValid = runtime_.controller.targetKnown;
    }
    web_.send(r.status, "application/json", r.body.c_str());
  });
  web_.on("/api/motion", HTTP_GET, [this] {
    if(runtime_.hud.isHud()) { web_.send(409,"application/json","{\"ok\":false,\"result\":\"controller_settings_only\"}"); return; }
    JsonWriter j(statusBuffer_,sizeof statusBuffer_);
    writeMotionConfiguration(j,runtime_.controller); sendJson(j);
  });
  web_.on("/api/motion/autotune", HTTP_GET, [this] {
    if(runtime_.hud.isHud()) { web_.send(409,"application/json","{\"ok\":false,\"result\":\"controller_settings_only\"}"); return; }
    JsonWriter j(statusBuffer_,sizeof statusBuffer_);writeAutoTune(j,runtime_.controller,true);sendJson(j);
  });
  for (const char *route :
       {"/api/mode", "/api/execute", "/api/preset", "/api/preset/run", "/api/settings", "/api/motion/reset", "/api/motion/autotune"}) {
    String path = route;
    web_.on(path, HTTP_POST, [this, path] {
      auto oldModel = runtime_.controller.model;
      auto oldSettings = runtime_.loggerSettings;
      actions_.updatePosition();
      auto response = callApi(path.c_str());
      if (response.status == 200) {
        if (oldModel != runtime_.controller.model) {
          // Old calibration was already saved on each calibration edit.
          actions_.loadPrefs();
          actions_.loadMotionSettings();
          runtime_.controller.setRange(calibration_.activeAzRange(), calibration_.state().gsAzMode, millis());
          actions_.loadProfilePresets();
          actions_.invalidateDisplay();
          runtime_.targetValid = false;
          // Reapply the explicit simple/settings fields to the newly loaded
          // profile. Combined model + PID changes are rejected by the API.
          if(path == "/api/settings") response = callApi(path.c_str());
        }
        if (path == "/api/mode" || path == "/api/settings" || (path=="/api/motion/autotune" && web_.arg("action")=="apply")) {
          actions_.saveForkSettings();
          if(runtime_.controller.fault=="motion_storage_failed") {
            if(path=="/api/motion/autotune") {runtime_.controller.autoTune.phase=AutoTunePhase::Failed;runtime_.controller.autoTune.reason="motion_storage_failed";}
            response={503,"{\"ok\":false,\"result\":\"motion_storage_failed\"}"};
          }
        }
        if (path == "/api/preset") {
          actions_.saveProfilePresets();
        }
        if (oldSettings.n1mmPort != runtime_.loggerSettings.n1mmPort ||
            oldSettings.pstPort != runtime_.loggerSettings.pstPort) {
          network_.setupLoggerUdp();
        }
        if (path == "/api/execute" || path == "/api/preset/run") {
          runtime_.targetValid = runtime_.controller.targetKnown;
        }
      }
      runtime_.controller.tick(millis());
      hardware_.applyOutputs();
      web_.send(response.status, "application/json", response.body.c_str());
    });
  }
}
} // namespace forkrot
