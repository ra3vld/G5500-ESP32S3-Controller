// G5500 Fork; GPL-3.0.
#include "web_server.hpp"
#include "hardware_form.hpp"
#include "hardware_page.hpp"
#include "runtime_metrics.hpp"
namespace forkrot {
void WebInterface::writeHardwareStatus() {
  JsonWriter j(statusBuffer_, sizeof statusBuffer_);
  auto config = [&](const HardwareConfig &c) {
    j << "{\"driveInterface\":" << c.drive << ",\"simulation\":" << unsigned(c.simulation)
      << ",\"bluetoothEnabled\":" << unsigned(c.bluetooth) << ",\"bluetoothMode\":" << c.bluetoothMode
      << ",\"forceApSta\":" << unsigned(c.forceAp) << ",\"buttonGpio\":" << int(c.buttonGpio)
      << ",\"buttonActiveLow\":" << unsigned(c.buttonLow) << ",\"buttonPullup\":" << unsigned(c.buttonPullup)
      << ",\"pwmInverted\":" << unsigned(c.pwmInverted) << '}';
  };
  const auto &r = runtime_.hardwareRecord;
  const auto elapsed = uint32_t(millis()-runtime_.memoryGuard.started);
  const auto &configured = r.phase == ConfigPhase::Testing || r.phase == ConfigPhase::Pending || runtime_.recovery ? r.candidate : r.good;
  const bool ready = runtime_.memoryGuard.ready(millis()) && heapFreeBytes() >= MemoryGuard::HealthyFree &&
      heapMaxBlockBytes() >= MemoryGuard::HealthyBlock &&
      runtimeMetrics.allocationFailures.load() == runtime_.memoryGuard.failures;
  j << "{\"token\":" << runtime_.hardwareToken << ",\"generation\":" << r.generation
    << ",\"hasConfirmed\":" << (r.hasConfirmed ? "true" : "false")
    << ",\"trial\":" << (runtime_.hardwareTrial ? "true" : "false")
    << ",\"recovery\":" << (runtime_.recovery ? "true" : "false")
    << ",\"reason\":" << jsonQuoted(recoveryReasonName(runtime_.recoveryReason))
    << ",\"ready\":" << (ready ? "true" : "false")
    << ",\"remainingMs\":" << (runtime_.hardwareTrial && elapsed < MemoryGuard::TrialMs ? MemoryGuard::TrialMs-elapsed : 0u)
    << ",\"heapFree\":" << heapFreeBytes() << ",\"heapMaxBlock\":" << heapMaxBlockBytes()
    << ",\"backend\":" << jsonQuoted(runtime_.controller.hardware.backendName())
    << ",\"sppSupported\":" << (SELECTED_BOARD == BOARD_ESP32 ? "true" : "false")
    << ",\"classicEsp32\":" << (SELECTED_BOARD == BOARD_ESP32 ? "true" : "false")
    << ",\"crowpanel\":" << (SELECTED_BOARD == BOARD_CROWPANEL_21 ? "true" : "false")
    << ",\"crowpanelLocalDrive\":" << (CROWPANEL_LOCAL_DRIVE ? "true" : "false")
    << ",\"separateDrive\":" << (SELECTED_BOARD != BOARD_CROWPANEL_21 && board::output(board::PIN_EL_SPEED) ? "true" : "false")
    << ",\"btsDrive\":" << (CROWPANEL_LOCAL_DRIVE || (SELECTED_BOARD != BOARD_CROWPANEL_21 && board::output(board::PIN_EL_SPEED)) ? "true" : "false")
    << ",\"independentDrive\":" << (SELECTED_BOARD != BOARD_CROWPANEL_21 && board::output(board::PIN_EL_SPEED) ? "true" : "false")
    << ",\"configured\":"; config(configured);
  j << ",\"effective\":"; config(runtime_.controller.hardware);
  j << ",\"confirmed\":"; config(r.good); j << '}';
  web_.sendFixed(j.good() ? 200 : 503, "application/json", j.data(), j.size());
}
bool WebInterface::handleHardwareRequest(std::string_view request) {
  const auto space = request.find(' '), end = request.find(' ',space+1);
  if (space == request.npos || end == request.npos) return false;
  const auto method = request.substr(0,space), path = request.substr(space+1,end-space-1);
  const bool hardwarePath = path == "/api/hardware" || path == "/api/hardware/confirm" ||
      path == "/api/hardware/rollback" || path == "/api/hardware/recovery";
  auto answer = [&](int code, const char *error) {
    const int count = std::snprintf(statusBuffer_,sizeof statusBuffer_,"{\"ok\":%s,\"error\":\"%s\"}",
        code == 200 ? "true" : "false", error);
    web_.sendFixed(code,"application/json",statusBuffer_,size_t(count));
  };
  if (method == "GET" && (path == "/hardware" || path == "/recovery" || (runtime_.recovery && path == "/"))) {
    web_.sendFixed(200,"text/html; charset=utf-8",HARDWARE_PAGE,sizeof HARDWARE_PAGE-1); return true;
  }
  if (method == "GET" && path == "/api/hardware") { writeHardwareStatus(); return true; }
  if (hardwarePath && method == "POST") {
    if (ota_.locked()) { answer(409,"ota_in_progress"); return true; }
    const auto bodyAt = request.find("\r\n\r\n");
    HardwareForm form;
    if (bodyAt == request.npos || !parseHardwareForm(request.substr(bodyAt+4),form,path == "/api/hardware")) {
      answer(400,"invalid_form"); return true;
    }
    if (form.token != runtime_.hardwareToken || form.generation != runtime_.hardwareRecord.generation) {
      answer(409,"stale_configuration"); return true;
    }
    bool saved = false;
    if (path == "/api/hardware") {
      if (const char *error = board::validateHardware(form.config,runtime_.hardwareHudUart)) {
        answer(400,error); return true;
      }
      saved = storage_.stageHardware(runtime_,form.config);
    } else if (path == "/api/hardware/confirm") {
      if (runtime_.recovery || !runtime_.hardwareTrial || !runtime_.hardwareProbe ||
          !runtime_.memoryGuard.ready(millis()) || heapFreeBytes() < MemoryGuard::HealthyFree ||
          heapMaxBlockBytes() < MemoryGuard::HealthyBlock ||
          runtimeMetrics.allocationFailures.load() != runtime_.memoryGuard.failures) {
        answer(409,"not_ready"); return true;
      }
      saved = storage_.confirmHardware(runtime_);
    } else if (path == "/api/hardware/rollback") {
      if (!runtime_.hardwareRecord.hasConfirmed) { answer(409,"no_confirmed_configuration"); return true; }
      saved = storage_.stageHardware(runtime_,runtime_.hardwareRecord.good);
    }
    else saved = storage_.enterRecovery(runtime_,RecoveryReason::Requested);
    if (!saved) { answer(503,"storage_failed"); return true; }
    runtime_.controller.stopNow(-1,millis()); hardware_.applyOutputs();
    runtime_.controller.configurationLocked = true;
    answer(200,""); restart_.request(millis()); return true;
  }
  if (runtime_.recovery) { answer(404,"recovery_only"); return true; }
  return false;
}
} // namespace forkrot
