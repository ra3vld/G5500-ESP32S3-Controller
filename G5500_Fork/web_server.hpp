// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "bounded_web_server.hpp"
#include "runtime.hpp"
#include "calibration.hpp"
#include "hardware.hpp"
#include "diagnostics.hpp"
#include "network.hpp"
#include "storage.hpp"
#include "device_actions.hpp"
#include "live_server.hpp"
namespace forkrot {
class WebInterface {
public:
  WebInterface(Runtime &runtime, DeviceActions &actions, Calibration &calibration,
               Hardware &hardware, Diagnostics &diagnostics, Network &network, Storage &storage)
      : runtime_(runtime), actions_(actions), calibration_(calibration), hardware_(hardware),
        diagnostics_(diagnostics), network_(network), storage_(storage) {
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
    if (restart_.consume(millis())) ESP.restart();
  }
  void setupWeb();
  void serviceLiveCommands() { live_.commands(millis()); }
  void publishLive() { live_.publish(millis()); }

private:
  void sendHtml(const char *raw, size_t rawSize, const unsigned char *gzip, size_t gzipSize);
  void sendJson(const JsonWriter &j) {
    if (!j.good()) { web_.send(503, "application/json", "{\"error\":\"response_too_large\"}"); return; }
    web_.sendBuffer(200, "application/json", j.data(), j.size());
  }
  void setupForkWeb();
  void setupHudWeb();
  Response callApi(std::string_view path) {
    if (path == "/api/settings") {
      const unsigned n =
          web_.hasArg("n1mmPort") ? web_.arg("n1mmPort").toInt() : runtime_.loggerSettings.n1mmPort;
      const unsigned p =
          web_.hasArg("pstPort") ? web_.arg("pstPort").toInt() : runtime_.loggerSettings.pstPort;
      const unsigned r =
          web_.hasArg("reportPort") ? web_.arg("reportPort").toInt() : runtime_.loggerSettings.reportPort;
      if (!network_.hudPortValid(runtime_.hud.config.port, n, p, r))
        return {400, "{\"ok\":false,\"result\":\"hud_port_conflict\"}"};
    }
    std::array<FormArgument, HTTP_MAX_ARGUMENTS> arguments;
    const size_t count = web_.args();
    if (count > arguments.size()) return {400, "{\"error\":\"too_many_arguments\"}"};
    for (size_t i = 0; i < count; ++i) arguments[i] = {web_.argumentName(i), web_.argumentValue(i)};
    return apiView(runtime_.controller, runtime_.protocol, runtime_.loggerSettings, path,
                   {arguments.data(), count}, millis());
  }
  Runtime &runtime_;
  DeviceActions &actions_;
  Calibration &calibration_;
  Hardware &hardware_;
  Diagnostics &diagnostics_;
  Network &network_;
  Storage &storage_;
  LiveServer live_{runtime_};
  BoundedWebServer web_{80};
  RestartDelay restart_;
  char statusBuffer_[STATUS_JSON_CAPACITY];
};
} // namespace forkrot
