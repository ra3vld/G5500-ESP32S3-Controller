// Transport-independent device operations; shared by HTTP and command API. GPL-3.0.
#pragma once
#include "network.hpp"
#include "storage.hpp"
#include "ota.hpp"
#include "write_budget.hpp"
#include "mqtt_service.hpp"
namespace forkrot {
class DeviceApi {
public:
  DeviceApi(Runtime &runtime, DeviceActions &actions, Calibration &calibration, Hardware &hardware,
            Diagnostics &diagnostics, Network &network, Storage &storage, Ota &ota, RestartDelay &restart, MqttService *mqtt = nullptr)
      : runtime_(runtime), actions_(actions), calibration_(calibration), hardware_(hardware),
        diagnostics_(diagnostics), network_(network), storage_(storage), ota_(ota), restart_(restart), mqtt_(mqtt) {}
  void execute(std::string_view method, std::string_view path, FormView arguments,
               JsonWriter &output, int &status, const char *&type, bool command = false);
  void serviceSetup(uint32_t now);
private:
  void executeHardware(std::string_view method, std::string_view path, FormView arguments,
                       JsonWriter &output, int &status, const char *&type, bool command, bool setupBatch);
  void executeGeneral(std::string_view method, std::string_view path, FormView arguments,
                      JsonWriter &output, int &status, const char *&type, bool command, uint32_t lease, bool setupBatch);
  bool setupReady(SetupStep step) const;
  bool setupMotionAllowed() const;
  void writeSetup(JsonWriter &) const;
  void writeMqttStatus(JsonWriter &) const;
  void invalidateSetup(SetupStep step);
  void configurationRestart(bool setupBatch);
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
  MqttService *mqtt_;
};
}
