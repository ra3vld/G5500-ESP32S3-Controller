#include "motion_fixture.hpp"
#include "storage.hpp"
#include "hardware.hpp"
#include "bluetooth.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main(){
  static_assert(FORK_CLASSIC_ESP32 && MQTT_SUPPORT && MQTT_TLS_SUPPORT && !BLUETOOTH_SPP_SUPPORT);
  static_assert(WEB_INTERFACE_ENABLED && !WEB_LIVE_SUPPORT && !ESP32_SPP_PROFILE);
  static_assert(!LOG_HISTORY_SUPPORT && !RSSI_HISTORY_SUPPORT && !OTA_SUPPORT);
  static_assert(WIFI_COMPACT_BUFFERS);
  Preferences::database.clear();fakeHardware={};
  HardwareConfig config;config.drive=DRIVE_BTS7960;config.bluetooth=false;config.bluetoothMode=BT_ESP32_SPP;
  assert(!board::validateHardware(config));
  HardwareRecord record;record.good=record.candidate=config;record.hasConfirmed=true;record.generation=49;record.phase=ConfigPhase::Confirmed;
  const auto bytes=encodeHardware(record);Preferences::database["hardware"]["config1"]=Preferences::Bytes(bytes.begin(),bytes.end());
  Storage storage;MotionTestRuntime runtime;storage.loadHardware(runtime);
  assert(!runtime.recovery&&!runtime.hardwareTrial&&runtime.hardwareRecord.generation==49);
  assert(runtime.controller.hardware==config&&runtime.controller.hardware.backend()==BT_NONE);
  assert(storage.finishHardwareBoot(runtime));
  auto spp=config;spp.bluetooth=true;assert(spp.backend()==BT_NONE);
  auto hc=spp;hc.bluetoothMode=BT_HC05;assert(!board::validateHardware(hc));
  Bluetooth radio;assert(!radio.begin(BT_ESP32_SPP));assert(radio.begin(BT_HC05,hc.pins));radio.end();
  std::cout<<"PASS: ESP32 TLS profile preserves saved SPP-OFF drive/generation, rejects internal SPP, retains HC-05, HTTP web and full controller\n";
}
