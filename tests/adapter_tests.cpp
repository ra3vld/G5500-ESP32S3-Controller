#include "motion_fixture.hpp"
#include "calibration.hpp"
#include "hardware.hpp"
#include "storage.hpp"
#include <iostream>
#include <stdexcept>

using namespace forkrot;
using namespace forkrot::board;
namespace {
unsigned checks = 0;
#define CHECK(expression) do { ++checks; if (!(expression)) { throw std::runtime_error(\
  std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expression); } } while (0)

void calibrationMath() {
  const int increasing[] = {100, 600, 1600};
  const int decreasing[] = {1600, 600, 100};
  const float degrees[] = {0, 90, 450};
  CHECK(calPointsMonotonic(increasing, 3));
  CHECK(calPointsMonotonic(decreasing, 3));
  CHECK(!calPointsMonotonic(increasing, 1));
  CHECK(!calPointsMonotonic(increasing, 0));
  for (const auto table : {increasing, decreasing}) {
    CHECK(mapPiecewise(table[0], table, degrees, 3) == 0);
    CHECK(mapPiecewise(table[1], table, degrees, 3) == 90);
    CHECK(mapPiecewise(table[2], table, degrees, 3) == 450);
    CHECK(mapPiecewise((table[0] + table[1]) / 2, table, degrees, 3) == 45);
    CHECK(mapPiecewise((table[1] + table[2]) / 2, table, degrees, 3) == 270);
  }
  CHECK(mapPiecewise(0, increasing, degrees, 3) == 0);
  CHECK(mapPiecewise(2000, increasing, degrees, 3) == 450);
  CHECK(mapPiecewise(2000, decreasing, degrees, 3) == 0);
  CHECK(mapPiecewise(0, decreasing, degrees, 3) == 450);
  for (const auto invalid : {std::array<int, 3>{100, 100, 100}, {100, 90, 200}, {100, -1, 200}}) {
    CHECK(!calPointsMonotonic(invalid.data(), 3));
    CHECK(mapPiecewise(150, invalid.data(), degrees, 3) == 0);
  }
  CHECK(mapCal(600, 100, 1100, 450) == 225);
  CHECK(mapCal(600, 1100, 100, 450) == 225);
  CHECK(mapCal(0, 100, 1100, 450) == 0);
  CHECK(mapCal(2000, 100, 1100, 450) == 450);
  CHECK(mapCal(100, 100, 100, 450) == 0);

  Controller controller = forkrot::testController();
  Calibration calibration(controller);
  auto &data = calibration.state();
  data.az36LinearValid = true;
  data.az45LinearValid = false;
  data.elLinearValid = true;
  data.gsAzMode = 36;
  CHECK(calibration.selectedCalibrationValid());
  CHECK(calibration.activeAzMaxDeg() == 360);
  data.gsAzMode = 45;
  CHECK(!calibration.selectedCalibrationValid());
  CHECK(calibration.activeAzMaxDeg() == 450);
  data.az45LinearValid = true;
  data.elLinearValid = false;
  CHECK(!calibration.selectedCalibrationValid());
  controller.model = Model::G800DXA;
  CHECK(calibration.selectedCalibrationValid());
  for (unsigned i = 0; i < AZ36_CAL_N; ++i) { data.az36CalMv[i] = 100 + i * 500; }
  calibration.refreshMultiCalValidity();
  CHECK(data.az36MultiValid);
  CHECK(!data.az45MultiValid);
  data.calibrationMode = CAL_MULTI_POINT;
  data.gsAzMode = 36;
  CHECK(calibration.selectedCalibrationValid());
  data.gsAzMode = 45;
  CHECK(!calibration.selectedCalibrationValid());
}

void gpioOrdering() {
  fakeHardware = {};
  MotionTestRuntime runtime;
  Hardware hardware(runtime);
  hardware.beginDirections();
  for (unsigned i=0; i<fakeHardware.events.size(); i+=2) {
    CHECK(fakeHardware.events[i].operation == "preload");
    CHECK(fakeHardware.events[i].value == LOW);
    CHECK(fakeHardware.events[i+1].operation == "mode");
    CHECK(fakeHardware.events[i+1].value == OUTPUT);
  }
  hardware.beginInputs();
  CHECK(fakeHardware.levels[PIN_SPEED] == LOW);
  for (int first : {-1, 0, 1}) {
    for (int second : {-1, 0, 1}) {
      runtime.controller.axes[0].direction = first;
      runtime.controller.axes[1].direction = second;
      fakeHardware.events.clear();
      hardware.applyOutputs();
      CHECK(fakeHardware.levels[PIN_LEFT] == (first < 0));
      CHECK(fakeHardware.levels[PIN_RIGHT] == (first > 0));
      CHECK(fakeHardware.levels[PIN_DOWN] == (second < 0));
      CHECK(fakeHardware.levels[PIN_UP] == (second > 0));
      auto levels = std::map<int, int>{{PIN_LEFT, 0}, {PIN_RIGHT, 0}, {PIN_UP, 0}, {PIN_DOWN, 0}};
      // Also replay the opposite previous direction: LOW must precede HIGH.
      levels[PIN_LEFT] = first > 0;
      levels[PIN_RIGHT] = first < 0;
      levels[PIN_UP] = second < 0;
      levels[PIN_DOWN] = second > 0;
      for (const auto &event : fakeHardware.events) {
        if (event.operation != "write") { continue; }
        levels[event.pin] = event.value;
        CHECK(!(levels[PIN_LEFT] && levels[PIN_RIGHT]));
        CHECK(!(levels[PIN_UP] && levels[PIN_DOWN]));
      }
    }
  }
  runtime.controller.model = Model::G800DXA;
  runtime.controller.speedEnabled = true;
  runtime.controller.axes[0].direction = 1;
  runtime.controller.axes[0].speedOutputPercent = 50;
  fakeHardware.events.clear();
  hardware.applyOutputs();
  bool halfDuty = false;
  for (const auto &event : fakeHardware.events) if (event.operation == "pwm" && event.pin == PIN_SPEED && event.value == 512) halfDuty = true;
  CHECK(halfDuty);
  runtime.controller.axes[0].direction = 0;
  hardware.applyOutputs();
  uint32_t lastDuty=9999;
  for (const auto &event : fakeHardware.events) if (event.operation == "pwm" && event.pin == PIN_SPEED) lastDuty=event.value;
  CHECK(lastDuty == 0);
  runtime.controller.model = Model::G5500;
  fakeHardware.events.clear();
  hardware.applyOutputs();
  bool detached = false;
  for (const auto &event : fakeHardware.events) if (event.operation == "detach" && event.pin == PIN_SPEED) detached = true;
  CHECK(detached);
  CHECK(fakeHardware.levels[PIN_SPEED] == LOW);

  MotionTestRuntime failingRuntime;
  Hardware failingHardware(failingRuntime);
  failingRuntime.controller.model = Model::G800DXA;
  failingRuntime.controller.speedEnabled = true;
  fakeHardware.attachSucceeds = false;
  failingHardware.applyOutputs();
  CHECK(failingRuntime.controller.driveFault);
  CHECK(!failingRuntime.controller.moving());
  CHECK(failingRuntime.controller.fault == "pwm_attach_failed");
}

void storageCompatibility() {
  Preferences::database.clear();
  Preferences::opened.clear();
  MotionTestRuntime runtime;
  Calibration calibration(runtime.controller);
  WifiCredentials wifi;
  Storage storage;
  storage.loadForkSettings(runtime);
  CHECK(runtime.controller.model == Model::G5500);
  CHECK(runtime.controller.speedPercent == 50);
  CHECK(runtime.controller.startSpeedPercent == 25);
  CHECK(runtime.controller.accelerationMs == 2000);
  storage.loadPrefs(runtime, calibration, wifi);
  CHECK(!calibration.selectedCalibrationValid());
  CHECK(calibration.state().gsAzMode == 45);
  CHECK(std::get<uint8_t>(Preferences::database["g5500"]["calfmt"]) == 4);
  CHECK(wifi.apPass.length() == 0);

  // Exact key types matter to real NVS, including the new speed settings.
  auto &settings = Preferences::database["forkcfg"];
  settings["model"] = uint8_t(1);
  settings["mode"] = uint8_t(1);
  settings["speed"] = uint32_t(77);
  settings["startspd"] = uint32_t(31);
  settings["approachspd"] = uint32_t(29);
  settings["accelms"] = uint32_t(2300);
  settings["decelms"] = uint32_t(1600);
  settings["slowdeg"] = uint32_t(13);
  settings["name"] = String("MyRotor");
  settings["nport"] = uint16_t(22040);
  storage.loadForkSettings(runtime);
  CHECK(runtime.controller.model == Model::G800DXA);
  CHECK(runtime.controller.mode == Mode::Auto);
  CHECK(runtime.controller.speedPercent == 77);
  CHECK(runtime.controller.startSpeedPercent == 31);
  CHECK(runtime.controller.approachSpeedPercent == 29);
  CHECK(runtime.controller.accelerationMs == 2300);
  CHECK(runtime.controller.decelerationMs == 1600);
  CHECK(runtime.controller.slowdownDegrees == 13);
  CHECK(runtime.protocol.rotorName == "MyRotor");
  CHECK(runtime.loggerSettings.n1mmPort == 22040);
  runtime.controller.mode = Mode::AutoHour;
  storage.saveForkSettings(runtime);
  CHECK(std::get<uint8_t>(settings["mode"]) == 0);
  CHECK(std::get<uint32_t>(settings["speed"]) == 77);
  CHECK(std::get<uint16_t>(settings["nport"]) == 22040);

  // A G800 profile uses separate calibration while Wi-Fi always lives in g5500.
  auto &g800 = Preferences::database["g800cal"];
  g800["calfmt"] = uint8_t(4);
  g800["a36z"] = int32_t(120);
  g800["a36f"] = int32_t(2600);
  g800["a36os"] = true;
  g800["a36fs"] = true;
  g800["azmode"] = uint8_t(36);
  g800["calmode"] = uint8_t(0);
  Preferences::database["g5500"]["ssid"] = String("TestNetwork");
  storage.loadPrefs(runtime, calibration, wifi);
  CHECK(wifi.wifiSsid == String("TestNetwork"));
  CHECK(calibration.selectedCalibrationValid());
  CHECK(calibration.activeAz2Zero() == 120);
  CHECK(calibration.activeAz2Full() == 2600);
  storage.saveCal(runtime, calibration);
  CHECK(std::get<int32_t>(g800["a36z"]) == 120);
  CHECK(std::get<bool>(g800["a36fs"]));
  CHECK(std::get<int32_t>(Preferences::database["g5500"]["a36z"]) == 0);

  // Older format 3 preserves AZ tables but requires recapturing ambiguous EL endpoints.
  auto &legacy = Preferences::database["g5500"];
  runtime.controller.model = Model::G5500;
  legacy.erase("azgeom1"); // Model an actual pre-migration installation.
  legacy["calfmt"] = uint8_t(3);
  legacy["elm0"] = int32_t(100);
  legacy["elm45"] = int32_t(800);
  legacy["elm90"] = int32_t(1600);
  legacy["elm135"] = int32_t(2400);
  legacy["elm180"] = int32_t(3000);
  legacy["a36m0"] = int32_t(100);
  legacy["a36m90"] = int32_t(800);
  legacy["a36m180"] = int32_t(1600);
  legacy["a36m270"] = int32_t(2400);
  legacy["a36m360"] = int32_t(3000);
  storage.loadPrefs(runtime, calibration, wifi);
  CHECK(calibration.state().az36MultiValid);
  CHECK(calibration.state().elCalMv[0] == -1);
  CHECK(calibration.state().elCalMv[4] == -1);
  CHECK(calibration.state().elCalMv[2] == 1600);
  CHECK(!calibration.state().elMultiValid);
  CHECK(!calibration.state().az36OSet);
  CHECK(std::get<uint8_t>(legacy["calfmt"]) == 4);

  // Format 2 had one AZ table: migration copies it into both ranges.
  legacy.clear();
  legacy["calfmt"] = uint8_t(2);
  const char *keys[] = {"azm0", "azm90", "azm180", "azm270", "azm360", "azm450"};
  for (unsigned i = 0; i < 6; ++i) { legacy[keys[i]] = int32_t(100 + i * 500); }
  legacy["el2oset"] = true;
  legacy["el2fset"] = true;
  storage.loadPrefs(runtime, calibration, wifi);
  CHECK(calibration.state().az36MultiValid);
  CHECK(calibration.state().az45MultiValid);
  for (unsigned i = 0; i < 5; ++i) {
    CHECK(calibration.state().az36CalMv[i] == int(100 + i * 500));
    CHECK(calibration.state().az45CalMv[i] == int(100 + i * 500));
  }
  CHECK(calibration.state().az45CalMv[5] == 2600);

  CHECK(storage.saveWifi("AnotherNetwork", "") == WifiSaveResult::Ok);
  storage.saveApPassword("local-test-only");
  CHECK(std::get<String>(legacy["ssid"]) == String("AnotherNetwork"));
  CHECK(std::get<String>(legacy["appass"]) == String("local-test-only"));
  storage.saveProfilePresets(runtime);
  runtime.controller.model = Model::G800DXA;
  runtime.controller.presets[0].label = "G800 park";
  storage.saveProfilePresets(runtime);
  CHECK(std::get<String>(Preferences::database["fork800"]["p0n"]) == String("G800 park"));
  CHECK(std::get<String>(Preferences::database["fork5500"]["p0n"]) != String("G800 park"));
  TouchCalibration touch;
  touch.touchCalValid = true;
  touch.touchX0Raw = 500;
  touch.touchX1Raw = 3500;
  storage.saveTouchCalibration(touch);
  TouchCalibration reloaded;
  storage.loadTouchCalibration(reloaded);
  CHECK(reloaded.touchCalValid);
  CHECK(reloaded.touchX0Raw == 500);
  CHECK(reloaded.touchX1Raw == 3500);
  legacy["tx1"] = int32_t(600);
  storage.loadTouchCalibration(reloaded);
  CHECK(!reloaded.touchCalValid);
}

void wifiPersistenceFailures() {
  Preferences::database.clear();
  Preferences::opened.clear();
  Storage storage;
  CHECK(storage.saveWifi("", "test-password") == WifiSaveResult::InvalidSsid);
  CHECK(storage.saveWifi(String(std::string(33, 's')), "test-password") == WifiSaveResult::InvalidSsid);
  CHECK(storage.saveWifi("Test network", "short") == WifiSaveResult::InvalidPassword);
  CHECK(storage.saveWifi("Test network", String(std::string(64, 'z'))) == WifiSaveResult::InvalidPassword);
  CHECK(Preferences::opened.empty());

  Preferences::beginSucceeds = false;
  CHECK(storage.saveWifi("Test network", "test-password") == WifiSaveResult::OpenFailed);
  CHECK(Preferences::database.empty());
  Preferences::beginSucceeds = true;
  CHECK(storage.saveWifi("Test network", "test-password") == WifiSaveResult::Ok);

  // New Storage object and full boot load exercise persistence, not RAM state.
  MotionTestRuntime rebootRuntime;
  Calibration rebootCalibration(rebootRuntime.controller);
  WifiCredentials reloaded;
  Storage rebootStorage;
  rebootStorage.loadPrefs(rebootRuntime, rebootCalibration, reloaded);
  CHECK(rebootStorage.wifiCredentialsReadOk());
  CHECK(reloaded.wifiSsid == String("Test network"));
  CHECK(reloaded.wifiPass == String("test-password"));

  Preferences::failedStringKey = "ssid";
  CHECK(storage.saveWifi("Changed network", "test-password") == WifiSaveResult::WriteFailed);
  Preferences::failedStringKey = "pass";
  CHECK(storage.saveWifi("Test network", "another-password") == WifiSaveResult::WriteFailed);
  // Empty passwords return zero both for success and failure. Readback must
  // catch a failed write instead of treating zero as proof of success.
  CHECK(storage.saveWifi("Test network", "") == WifiSaveResult::VerifyFailed);
  Preferences::failedStringKey.clear();
  CHECK(storage.saveWifi("Open network", "") == WifiSaveResult::Ok);
  rebootStorage.loadPrefs(rebootRuntime, rebootCalibration, reloaded);
  CHECK(reloaded.wifiSsid == String("Open network"));
  CHECK(reloaded.wifiPass.length() == 0);

  Preferences::readOnlyBeginSucceeds = false;
  CHECK(storage.saveWifi("Test network", "test-password") == WifiSaveResult::VerifyFailed);
  Preferences::readOnlyBeginSucceeds = true;
  Preferences::corruptStringKey = "pass";
  CHECK(storage.saveWifi("Test network", "test-password") == WifiSaveResult::VerifyFailed);
  Preferences::corruptStringKey.clear();
  CHECK(storage.saveWifi("Hex PSK network", String(std::string(64, 'a'))) == WifiSaveResult::Ok);
}
}

int main() {
  try {
    calibrationMath();
    gpioOrdering();
    storageCompatibility();
    wifiPersistenceFailures();
    std::cout << "PASS: " << checks << " calibration, GPIO ordering and typed NVS checks\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
