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
namespace forkrot {
class WebInterface {
public:
  WebInterface(Runtime &runtime, DeviceActions &actions, Calibration &calibration,
               Hardware &hardware, Diagnostics &diagnostics, Network &network, Storage &storage)
      : runtime_(runtime), actions_(actions), calibration_(calibration), hardware_(hardware),
        diagnostics_(diagnostics), network_(network), storage_(storage) {
    web_.setStopHandler([](void *context, int axis) {
      auto &self = *static_cast<WebInterface *>(context);
      self.runtime_.controller.stopNow(axis, millis());
      self.hardware_.applyOutputs();
    }, this);
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

private:
  void sendHtml(const char *raw, size_t rawSize, const unsigned char *gzip, size_t gzipSize);
  void sendJson(const JsonWriter &j) {
    if (!j.good()) { web_.send(503, "application/json", "{\"error\":\"response_too_large\"}"); return; }
    web_.sendBuffer(200, "application/json", j.data(), j.size());
  }
  void setupForkWeb();
  Response callApi(std::string_view path) {
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
  BoundedWebServer web_{80};
  RestartDelay restart_;
  char statusBuffer_[STATUS_JSON_CAPACITY];
};
} // namespace forkrot
