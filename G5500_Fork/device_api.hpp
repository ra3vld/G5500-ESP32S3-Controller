// Transport-independent device operations; shared by HTTP and command API. GPL-3.0.
#pragma once
#include "network.hpp"
#include "storage.hpp"
#include "ota.hpp"
#include "write_budget.hpp"
namespace forkrot {
class DeviceApi {
public:
  DeviceApi(Runtime &runtime, DeviceActions &actions, Calibration &calibration, Hardware &hardware,
            Diagnostics &diagnostics, Network &network, Storage &storage, Ota &ota, RestartDelay &restart)
      : runtime_(runtime), actions_(actions), calibration_(calibration), hardware_(hardware),
        diagnostics_(diagnostics), network_(network), storage_(storage), ota_(ota), restart_(restart) {}
  void execute(std::string_view method, std::string_view path, FormView arguments,
               JsonWriter &output, int &status, const char *&type, bool command = false);
  void serviceSetup(uint32_t now);
private:
  void executeHardware(std::string_view method, std::string_view path, FormView arguments,
                       JsonWriter &output, int &status, const char *&type, bool command);
  void executeGeneral(std::string_view method, std::string_view path, FormView arguments,
                      JsonWriter &output, int &status, const char *&type, bool command, uint32_t lease);
  bool setupReady(SetupStep step) const;
  void writeSetup(JsonWriter &) const;
  void invalidateSetup(SetupStep step);
  Response callApi(std::string_view path, FormView form);
  void writeHardwareStatus(JsonWriter &, bool compact = false, std::string_view view = {});
  void writeConfiguration(JsonWriter &);
  Runtime &runtime_;
  DeviceActions &actions_;
  Calibration &calibration_;
  Hardware &hardware_;
  Diagnostics &diagnostics_;
  Network &network_;
  Storage &storage_;
  Ota &ota_;
  RestartDelay &restart_;
};
}
