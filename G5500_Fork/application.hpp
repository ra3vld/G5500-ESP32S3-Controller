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
namespace forkrot {
class Application : public DeviceActions {
public:
  void updatePosition() override;
  void setCalibrationMode(CalibrationMode m) override;
  void setGsAzMode(uint8_t m) override;
  void captureGs232Cal(const String &cmd) override;
  void controllerSetup();
  void controllerLoop();
  bool saveCal() override;
  bool configureAzRange(uint8_t profile, float start, float span) override;
  bool editAzPoint(const char *operation, float angle) override;
  bool configureElRange(uint8_t profile, float start, float span) override;
  bool editElPoint(const char *operation, float angle) override;
  void loadPrefs() override {
    storage_.loadPrefs(runtime_, calibration_, network_.credentials());
  }
  void loadProfilePresets() override {
    storage_.loadProfilePresets(runtime_);
  }
  void saveProfilePresets() override {
    storage_.saveProfilePresets(runtime_);
  }
  void saveForkSettings() override {
    storage_.saveForkSettings(runtime_);
  }
  void setupWeb() override {
    web_server_.setupWeb();
  }
  void invalidateDisplay() override {
    display_.invalidate();
  }

private:
  Runtime runtime_;
  Calibration calibration_{runtime_.controller};
  Storage storage_;
  Hardware hardware_{runtime_};
  Diagnostics diagnostics_{storage_};
  Network network_{runtime_, *this, calibration_, hardware_, diagnostics_};
  Display display_{runtime_, *this, calibration_, hardware_, diagnostics_, network_, storage_};
  WebInterface web_server_{runtime_,     *this,    calibration_, hardware_,
                           diagnostics_, network_, storage_};
  Button confirmButton;
};
} // namespace forkrot
