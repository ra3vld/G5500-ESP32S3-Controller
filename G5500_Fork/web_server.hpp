// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "build_config.hpp"
#include "json_writer.hpp"
#if WEB_INTERFACE_ENABLED
#include "bounded_web_server.hpp"
#include "runtime.hpp"
#include "calibration.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "network.hpp"
#include "storage.hpp"
#include "device_actions.hpp"
#include "live_server.hpp"
#include "ota.hpp"
namespace forkrot {
class WebInterface {
public:
  WebInterface(Runtime &runtime, DeviceActions &actions, Calibration &calibration,
               Hardware &hardware, Diagnostics &diagnostics, Network &network, Storage &storage, Ota &ota, RestartDelay &restart)
      : runtime_(runtime), actions_(actions), calibration_(calibration), hardware_(hardware),
        diagnostics_(diagnostics), network_(network), storage_(storage), restart_(restart), ota_(ota) {
    web_.setStopHandler(
        [](void *context, const StopRequest &request) {
          auto &self = *static_cast<WebInterface *>(context);
          if (self.runtime_.hud.isHud() && request.scoped &&
              request.selection != self.runtime_.hud.selectionEpoch)
            return BoundedWebServer::StopResult{409, 0};
          self.runtime_.controller.stopNow(request.axis, millis());
          self.hardware_.applyOutputs();
          if (self.runtime_.hud.isHud())
            return BoundedWebServer::StopResult{self.runtime_.hud.lastResult() == Result::Deferred ? 202
                                                                                                   : 503,
                                                self.runtime_.hud.requestId()};
          return BoundedWebServer::StopResult{};
        },
        this);
    web_.setRequestBuffer(statusBuffer_, sizeof statusBuffer_);
    static const char *const lowMemoryPaths[] = {"/", "/api/status", "/diaglog", "/resetdiag", "/adc",
                                                "/api/rssi?range=hour", "/api/rssi?range=day", "/api/rssi?range=week",
                                                "/api/rssi?range=month", "/api/rssi?range=year", "/api/diaglog", "/api/resethistory"};
    web_.setLowMemoryPaths(lowMemoryPaths, sizeof lowMemoryPaths / sizeof *lowMemoryPaths);
  }
  void service() {
    web_.handleClient();
    if (web_.writeFailed() && web_.diagnostics().writeFailures == 1)
      diagnostics_.dbgPrintln("[HTTP] response failed or send deadline expired; client closed");
    network_.setHttpDiagnostics(web_.diagnostics());

  }
  bool otaLocked() const { return ota_.locked(); }
  void abortForRecovery() { ota_.cancel(); }
  void setupWeb();
  void writeDiagnostics(JsonWriter &j) { j << "\"livePort\":" << (live_.started()?81:0); live_.writeDiagnostics(j); }
  void serviceLiveCommands() { live_.commands(millis()); }
  void publishLive() { live_.publish(millis()); }

private:
  bool handleHardwareRequest(std::string_view request);
  bool handleOtaRequest(std::string_view request);
  void sendHtml(const char *raw, size_t rawSize, const unsigned char *gzip, size_t gzipSize);
  void sendJson(const JsonWriter &j) {
    if (!j.good()) { web_.send(503, "application/json", "{\"error\":\"response_too_large\"}"); return; }
    web_.sendBuffer(200, "application/json", j.data(), j.size());
  }
  void serveApi(std::string_view method, std::string_view path, FormView arguments, bool fixed = false);
  Runtime &runtime_;
  DeviceActions &actions_;
  Calibration &calibration_;
  Hardware &hardware_;
  Diagnostics &diagnostics_;
  Network &network_;
  Storage &storage_;
  LiveServer live_{runtime_};
  BoundedWebServer web_{80};
  RestartDelay &restart_;
  Ota &ota_;
  char statusBuffer_[STATUS_JSON_CAPACITY];
};
} // namespace forkrot

#else
namespace forkrot {
class WebInterface {
public:
  template<class... Args> explicit WebInterface(Args &...) {}
  void setupWeb() {}
  void service() {}
  void serviceLiveCommands() {}
  void publishLive() {}
  void abortForRecovery() {}
  void writeDiagnostics(JsonWriter &j) { j << "\"livePort\":0"; }
};
}
#endif
