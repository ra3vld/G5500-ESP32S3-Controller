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
        diagnostics_(diagnostics), network_(network), storage_(storage) {}
  void service() {
    web_.handleClient();
    if (web_.writeFailed() && ++httpWriteFailures_ == 1)
      diagnostics_.dbgPrintln("[HTTP] response could not be sent within 250 ms; client closed");
    if (restart_.consume(millis())) ESP.restart();
  }
  void setupWeb();

private:
  void setupForkWeb();
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
  uint32_t httpWriteFailures_ = 0;
};
} // namespace forkrot
