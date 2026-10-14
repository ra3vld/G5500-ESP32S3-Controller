// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "web_server.hpp"
#include "web_page.hpp"
#include "diagnostic_pages.hpp"
namespace forkrot {
using namespace board;
void WebInterface::setupWeb() {
  web_.on("/", HTTP_GET, [this] { web_.send_P(200, "text/html; charset=utf-8", WEB_PAGE); });
  web_.on("/diaglog", HTTP_GET,
          [this] { web_.send_P(200, "text/html; charset=utf-8", DIAG_PAGE); });
  web_.on("/api/diaglog", HTTP_GET,
          [this] { web_.send(200, "text/plain; charset=utf-8", diagnostics_.diagSnapshot()); });
  web_.on("/api/diaglog/clear", HTTP_GET, [this] {
    diagnostics_.diagClear();
    web_.send(200, "text/plain", "OK");
  });
  web_.on("/resetdiag", HTTP_GET,
          [this] { web_.send_P(200, "text/html; charset=utf-8", RESET_DIAG_PAGE); });
  web_.on("/adc", HTTP_GET,
          [this] { web_.send_P(200, "text/html; charset=utf-8", ADC_TEST_PAGE); });
  web_.on("/api/adcdiag", HTTP_GET, [this] {
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
    forkrot::Form form;
    form["d"] = web_.arg("d").c_str();
    auto r = forkrot::api(runtime_.controller, runtime_.protocol, runtime_.loggerSettings,
                          "/api/move", form, millis());
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    web_.send(r.status, "application/json", r.body.c_str());
  });
  web_.on("/api/target", HTTP_GET, [this] {
    forkrot::Form form;
    form["az"] = web_.arg("az").c_str();
    if (web_.hasArg("el")) {
      form["el"] = web_.arg("el").c_str();
    }
    actions_.updatePosition();
    auto response = forkrot::api(runtime_.controller, runtime_.protocol, runtime_.loggerSettings,
                                 "/api/target", form, millis());
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    runtime_.targetValid = true;
    web_.send(response.status, "application/json", response.body.c_str());
  });
  web_.on("/api/cal", HTTP_GET, [this] {
    auto &calibrationState = calibration_.state();
    // MULTI-POINT. AZ is written only to the currently selected P36/P45 table.
    String p = web_.arg("p");
    if (runtime_.controller.model == forkrot::Model::G800DXA && p.startsWith("el")) {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    int a = hardware_.adcAvgMv(PIN_AZ_ADC), e = hardware_.adcAvgMv(PIN_EL_ADC);
    if (p.startsWith("az")) {
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
    actions_.saveCal();
    web_.send(200, "text/plain", "MULTI SAVED");
  });

  web_.on("/api/cal2", HTTP_GET, [this] {
    String p = web_.arg("p");
    p.toUpperCase();
    if (runtime_.controller.model == forkrot::Model::G800DXA && (p == "O2" || p == "F2")) {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    if (p == "O" || p == "O2" || p == "F" || p == "F2") {
      actions_.captureGs232Cal(p);
      web_.send(200, "text/plain", "2-POINT SAVED");
    } else {
      web_.send(400, "text/plain", "BAD COMMAND");
    }
  });

  web_.on("/api/calmode", HTTP_GET, [this] {
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
    web_.send(200, "text/plain", calibration_.calibrationModeName());
  });

  web_.on("/api/azmode", HTTP_GET, [this] {
    int m = web_.arg("m").toInt();
    if (m != 36 && m != 45) {
      web_.send(400, "text/plain", "BAD AZ MODE");
      return;
    }
    actions_.setGsAzMode((uint8_t)m);
    web_.send(200, "text/plain", m == 36 ? "P36" : "P45");
  });

  web_.on("/api/calreset", HTTP_GET, [this] {
    auto &calibrationState = calibration_.state();
    String axis = web_.arg("axis");
    axis.toLowerCase();
    if (runtime_.controller.model == forkrot::Model::G800DXA && axis == "el") {
      web_.send(400, "text/plain", "NO EL AXIS");
      return;
    }
    String m = web_.arg("mode");
    m.toLowerCase();

    if (m == "multi") {
      if (axis == "az") {
        if (calibrationState.gsAzMode == 36) {
          for (uint8_t i = 0; i < AZ36_CAL_N; i++) {
            calibrationState.az36CalMv[i] = -1;
          }
          calibrationState.az36MultiValid = false;
        } else {
          for (uint8_t i = 0; i < AZ45_CAL_N; i++) {
            calibrationState.az45CalMv[i] = -1;
          }
          calibrationState.az45MultiValid = false;
        }
      } else if (axis == "el") {
        for (uint8_t i = 0; i < EL_CAL_N; i++) {
          calibrationState.elCalMv[i] = -1;
        }
        calibrationState.elMultiValid = false;
      } else {
        web_.send(400, "text/plain", "BAD AXIS");
        return;
      }
    } else if (m == "2") {
      if (axis == "az") {
        if (calibrationState.gsAzMode == 36) {
          calibrationState.cal.az36_0 = 0;
          calibrationState.cal.az36Max = 3100;
          calibrationState.az36OSet = false;
          calibrationState.az36FSet = false;
          calibrationState.az36LinearValid = false;
        } else {
          calibrationState.cal.az45_0 = 0;
          calibrationState.cal.az45Max = 3100;
          calibrationState.az45OSet = false;
          calibrationState.az45FSet = false;
          calibrationState.az45LinearValid = false;
        }
      } else if (axis == "el") {
        calibrationState.cal.el0 = 0;
        calibrationState.cal.elMax = 3100;
        calibrationState.elOSet = false;
        calibrationState.elFSet = false;
        calibrationState.elLinearValid = false;
      } else {
        web_.send(400, "text/plain", "BAD AXIS");
        return;
      }
    } else {
      web_.send(400, "text/plain", "BAD MODE");
      return;
    }

    actions_.saveCal();
    web_.send(200, "text/plain", "RESET OK");
  });
  web_.on("/api/wifi", HTTP_GET, [this] {
    storage_.saveWifi(web_.arg("ssid"), web_.arg("pass"));
    web_.send(200, "text/plain", "Saved");
    delay(500);
    ESP.restart();
  });
  web_.on("/api/ap", HTTP_GET, [this] {
    String p = web_.arg("pass");
    if (p.length() != 0 && (p.length() < 8 || p.length() > 63)) {
      web_.send(400, "text/plain", "AP password must be empty or 8..63 characters");
      return;
    }
    storage_.saveApPassword(p);
    web_.send(200, "text/plain", p.length() ? "AP password saved" : "AP set to open");
    delay(500);
    ESP.restart();
  });
  web_.on("/api/resetreason", HTTP_GET, [this] {
    String j = "{\"reason\":\"" + diagnostics_.bootReasonText() +
               "\",\"code\":" + String(diagnostics_.bootReasonCode()) + "}";
    web_.send(200, "application/json", j);
  });
  web_.on("/api/resethistory", HTTP_GET,
          [this] { web_.send(200, "application/json", storage_.resetHistoryJson()); });
  web_.on("/api/resethistory/clear", HTTP_GET, [this] {
    storage_.clearResetHistory();
    web_.send(200, "text/plain", "OK");
  });
  web_.on("/api/rssi", HTTP_GET, [this] {
    String r = web_.arg("range");
    if (r == "day") {
      web_.send(200, "application/json", diagnostics_.historyJson(RssiPeriod::Day));
    } else if (r == "week") {
      web_.send(200, "application/json", diagnostics_.historyJson(RssiPeriod::Week));
    } else if (r == "month") {
      web_.send(200, "application/json", diagnostics_.historyJson(RssiPeriod::Month));
    } else if (r == "year") {
      web_.send(200, "application/json", diagnostics_.historyJson(RssiPeriod::Year));
    } else {
      web_.send(200, "application/json", diagnostics_.historyJson(RssiPeriod::Hour));
    }
  });
  web_.on("/api/status", HTTP_GET, [this] {
    auto &calibrationState = calibration_.state();
    actions_.updatePosition();
    bool staOk = (WiFi.status() == WL_CONNECTED);
    String staIp = staOk ? WiFi.localIP().toString() : "-";
    String apIp = WiFi.softAPIP().toString();
    String ip = staOk ? staIp : apIp;
    String mode = staOk ? "AP + Wi-Fi Client" : "Access Point";
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
    bool azLin = calibration_.activeAzLinearValid();
    bool azMul = calibration_.activeAzMultiValid();
    int az450v = (azN > 5) ? azTbl[5] : -1;
    int az2z = calibration_.activeAzOSet() ? calibration_.activeAz2Zero() : -1;
    int az2f = calibration_.activeAzFSet() ? calibration_.activeAz2Full() : -1;
    calibrationState.cal.valid = calibration_.selectedCalibrationValid();

    String j =
        "{\"az\":" + String(runtime_.currentAz, 2) + ",\"el\":" + String(runtime_.currentEl, 2) +
        ",\"targetAz\":" + String(runtime_.targetAz, 2) +
        ",\"targetEl\":" + String(runtime_.targetEl, 2) +
        ",\"targetValid\":" + (runtime_.targetValid ? "true" : "false") +
        ",\"targetAgeMs\":" + String(targetAge) +
        ",\"calibrated\":" + (calibrationState.cal.valid ? "true" : "false") +
        ",\"selectedCalOk\":" + (calibration_.selectedCalibrationValid() ? "true" : "false") +
        ",\"azMode\":" + String(calibrationState.gsAzMode) +
        ",\"azLinear\":" + (azLin ? "true" : "false") +
        ",\"azLinear36\":" + (calibrationState.az36LinearValid ? "true" : "false") +
        ",\"azLinear45\":" + (calibrationState.az45LinearValid ? "true" : "false") +
        ",\"elLinear\":" + (calibrationState.elLinearValid ? "true" : "false") +
        ",\"azMulti\":" + (azMul ? "true" : "false") +
        ",\"azMulti36\":" + (calibrationState.az36MultiValid ? "true" : "false") +
        ",\"azMulti45\":" + (calibrationState.az45MultiValid ? "true" : "false") +
        ",\"elMulti\":" + (calibrationState.elMultiValid ? "true" : "false") + ",\"calMode\":\"" +
        String(calibrationState.calibrationMode == CAL_MULTI_POINT ? "multi" : "2") + "\"" +
        ",\"az2p0\":" + String(az2z) + ",\"az2pf\":" + String(az2f) +
        ",\"el2p0\":" + String(calibrationState.elOSet ? calibrationState.cal.el0 : -1) +
        ",\"el2pf\":" + String(calibrationState.elFSet ? calibrationState.cal.elMax : -1) +
        ",\"motion\":\"" + hardware_.motion() + "\"" + ",\"azp0\":" + String(azTbl[0]) +
        ",\"azp90\":" + String(azTbl[1]) + ",\"azp180\":" + String(azTbl[2]) +
        ",\"azp270\":" + String(azTbl[3]) + ",\"azp360\":" + String(azTbl[4]) +
        ",\"azp450\":" + String(az450v) + ",\"elp0\":" + String(calibrationState.elCalMv[0]) +
        ",\"elp45\":" + String(calibrationState.elCalMv[1]) +
        ",\"elp90\":" + String(calibrationState.elCalMv[2]) +
        ",\"elp135\":" + String(calibrationState.elCalMv[3]) +
        ",\"elp180\":" + String(calibrationState.elCalMv[4]) + ",\"ip\":\"" + ip +
        "\",\"apIp\":\"" + apIp + "\",\"staIp\":\"" + staIp + "\",\"wifiMode\":\"" + mode +
        "\",\"ssid\":" +
        String(forkrot::jsonQuote(network_.credentials().wifiSsid.c_str()).c_str()) +
        ",\"rssi\":" + rssiJson + ",\"signal\":\"" + signal +
        "\",\"btConnected\":" + (network_.bluetoothConnected() ? "true" : "false") +
        ",\"resetReason\":\"" + diagnostics_.bootReasonText() + "\"}";
    auto extra = forkrot::stateJson(runtime_.controller, runtime_.protocol, runtime_.loggerSettings,
                                    millis());
    j.remove(j.length() - 1);
    j += ",";
    j += extra.c_str() + 1;
    web_.send(200, "application/json", j);
  });
  setupForkWeb();
  web_.begin();
}

void WebInterface::setupForkWeb() {
  web_.on("/api/target", HTTP_POST, [this] {
    forkrot::Form f;
    for (unsigned i = 0; i < web_.args(); ++i) {
      f[web_.argName(i).c_str()] = web_.arg(i).c_str();
    }
    actions_.updatePosition();
    auto r = forkrot::api(runtime_.controller, runtime_.protocol, runtime_.loggerSettings,
                          "/api/target", f, millis());
    runtime_.controller.tick(millis());
    hardware_.applyOutputs();
    if (r.status == 200) {
      runtime_.targetValid = true;
    }
    web_.send(r.status, "application/json", r.body.c_str());
  });
  for (const char *route :
       {"/api/mode", "/api/execute", "/api/preset", "/api/preset/run", "/api/settings"}) {
    String path = route;
    web_.on(path, HTTP_POST, [this, path] {
      forkrot::Form form;
      for (unsigned i = 0; i < web_.args(); ++i) {
        form[web_.argName(i).c_str()] = web_.arg(i).c_str();
      }
      auto oldModel = runtime_.controller.model;
      auto oldSettings = runtime_.loggerSettings;
      actions_.updatePosition();
      auto response = forkrot::api(runtime_.controller, runtime_.protocol, runtime_.loggerSettings,
                                   path.c_str(), form, millis());
      if (response.status == 200) {
        if (oldModel != runtime_.controller.model) {
          // Old calibration was already saved on each calibration edit.
          actions_.loadPrefs();
          runtime_.controller.maximum = calibration_.activeAzMaxDeg();
          actions_.loadProfilePresets();
          actions_.invalidateDisplay();
          runtime_.targetValid = false;
        }
        if (path == "/api/mode" || path == "/api/settings") {
          actions_.saveForkSettings();
        }
        if (path == "/api/preset") {
          actions_.saveProfilePresets();
        }
        if (oldSettings.n1mmPort != runtime_.loggerSettings.n1mmPort ||
            oldSettings.pstPort != runtime_.loggerSettings.pstPort) {
          network_.setupLoggerUdp();
        }
        if (path == "/api/execute" || path == "/api/preset/run") {
          runtime_.targetValid = true;
        }
      }
      runtime_.controller.tick(millis());
      hardware_.applyOutputs();
      web_.send(response.status, "application/json", response.body.c_str());
    });
  }
}
} // namespace forkrot
