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
#include "camera.hpp"
#include "http_io_task.hpp"
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
    web_.setServiceHandler([](void *context) {
      auto &self=*static_cast<WebInterface *>(context);
      if(self.runtime_.recovery || self.ota_.locked() || self.runtime_.hud.isHud())return;
      const uint32_t now=millis();
      if(self.controlServiced_ && now==self.controlServicedAt_)return;
      if(!self.runtime_.controller.controlDue(now))return;
      self.controlServiced_=true;self.controlServicedAt_=now;
      self.actions_.updatePosition();
      self.hardware_.controlRotator();
    },this);
    web_.setRequestBuffer(statusBuffer_, sizeof statusBuffer_);
    static const char *const lowMemoryPaths[] = {"/", "/api/status", "/diaglog", "/resetdiag", "/adc",
                                                "/api/rssi?range=hour", "/api/rssi?range=day", "/api/rssi?range=week",
                                                "/api/rssi?range=month", "/api/rssi?range=year", "/api/diaglog", "/api/resethistory"};
    web_.setLowMemoryPaths(lowMemoryPaths, sizeof lowMemoryPaths / sizeof *lowMemoryPaths);
  }
  void service() {
    if(io_.active())io_.service();
    else web_.handleClient();
    const auto diagnostics=httpDiagnostics();
    if (diagnostics.writeFailures && !reportedWriteFailure_) {
      reportedWriteFailure_=true;
      diagnostics_.dbgPrintln("[HTTP] response failed or send deadline expired; client closed");
    }
    network_.setHttpDiagnostics(diagnostics);

  }
  bool otaLocked() const { return ota_.locked(); }
  void abortForRecovery() { ota_.cancel(); }
  void setupWeb();
  void startIoTask() {io_.begin(web_);}
  void wait(unsigned ms) {io_.wait(ms);}
  void writeDiagnostics(JsonWriter &j) { j << "\"livePort\":" << (live_.started()?81:0); live_.writeDiagnostics(j); camera_.writeStatus(j);io_.writeDiagnostics(j); }
  void serviceLiveCommands() { live_.commands(millis()); }
  void publishLive() { live_.publish(millis()); }
  void relieveMemory(uint32_t free, uint32_t largest) { live_.relieveMemory(free,largest); }

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
  Camera camera_;
  BoundedWebServer web_{80};
  HttpIoTask io_;
  bool reportedWriteFailure_=false;
  HttpDiagnostics httpDiagnostics() {
#if defined(ARDUINO) && defined(ARDUINO_ARCH_ESP32) && !CONFIG_FREERTOS_UNICORE && (FORK_CLASSIC_ESP32 || FORK_S3)
    if(io_.active())return io_.diagnostics();
#endif
    return web_.diagnostics();
  }
  RestartDelay &restart_;
  Ota &ota_;
  char statusBuffer_[STATUS_JSON_CAPACITY];
  uint32_t controlServicedAt_=0;
  bool controlServiced_=false;
};
} // namespace forkrot

#else
namespace forkrot {
class WebInterface {
public:
  template<class... Args> explicit WebInterface(Args &...) {}
  void setupWeb() {}
  void startIoTask() {}
  void wait(unsigned ms) {delay(ms);}
  void service() {}
  void serviceLiveCommands() {}
  void publishLive() {}
  void relieveMemory(uint32_t, uint32_t) {}
  void abortForRecovery() {}
  void writeDiagnostics(JsonWriter &j) { j << "\"livePort\":0"; }
};
}
#endif
