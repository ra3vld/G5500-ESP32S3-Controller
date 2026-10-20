// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include "runtime.hpp"
#include "input.hpp"
#include "device_actions.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include "calibration.hpp"
#include "diagnostics.hpp"
#include "network.hpp"
#include "display.hpp"
#include "web_server.hpp"
#include "device_api.hpp"
namespace forkrot {
class Application : public DeviceActions {
public:
  void updatePosition() override;
  void setCalibrationMode(CalibrationMode m) override;
  void setGsAzMode(uint8_t m) override;
  void captureGs232Cal(const String &cmd) override;
  void controllerSetup();
  void controllerLoop();
  void deviceRequest(std::string_view method, std::string_view path, FormView form, JsonWriter &out,
                     int &status, const char *&type, bool command = false) override {
    device_api_.execute(method,path,form,out,status,type,command);
  }
  void writeWebDiagnostics(JsonWriter &j) override { web_server_.writeDiagnostics(j); }
  bool saveCal() override;
  bool configureAzRange(uint8_t profile, float start, float span) override;
  bool editAzPoint(const char *operation, float angle) override;
  bool configureElRange(uint8_t profile, float start, float span) override;
  bool editElPoint(const char *operation, float angle) override;
  void loadPrefs() override {
    storage_.loadPrefs(runtime_, calibration_, network_.credentials());
  }
  void loadMotionSettings() override {
    storage_.loadMotionSettings(runtime_);
  }
  void loadProfilePresets() override {
    storage_.loadProfilePresets(runtime_);
  }
  void saveProfilePresets() override {
    storage_.saveProfilePresets(runtime_);
  }
  void saveForkSettings() override {
    if (!storage_.saveForkSettings(runtime_)) {
      runtime_.controller.stopNow(-1,millis());
      for(auto &m:runtime_.controller.motionSettings) m.ready=false;
      runtime_.controller.fault="motion_storage_failed";
    } else if(runtime_.controller.fault=="motion_storage_failed") runtime_.controller.fault.clear();
  }
  void setupWeb() override {
    web_server_.setupWeb();
  }
  void invalidateDisplay() override {
    display_.invalidate();
  }

private:
  void failConfiguration(RecoveryReason reason);
  void serviceConfigurationSafety();
  void *recoveryReserve_ = nullptr;
  Runtime runtime_;
  Calibration calibration_{runtime_.controller};
  Storage storage_;
  Hardware hardware_{runtime_};
  Diagnostics diagnostics_{storage_};
  Network network_{runtime_, *this, calibration_, hardware_, diagnostics_};
  Display display_{runtime_, *this, calibration_, hardware_, diagnostics_, network_, storage_};
  Ota ota_;
  RestartDelay restart_;
  DeviceApi device_api_{runtime_, *this, calibration_, hardware_, diagnostics_, network_, storage_, ota_, restart_};
  WebInterface web_server_{runtime_,     *this,    calibration_, hardware_,
                           diagnostics_, network_, storage_, ota_, restart_};
  Button confirmButton;
};
} // namespace forkrot
