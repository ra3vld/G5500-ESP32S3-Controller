#include "api.hpp"
#include "board.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;
int main() {
  Controller c; Protocol p(c); Settings settings;
  const auto before = stateJson(c, p, settings, 0);
  assert(before.find(std::string("\"bluetoothBackend\":\"") + board::BLUETOOTH_BACKEND_NAME + "\"") != std::string::npos);
  assert(before.find(BLUETOOTH_ENABLED ? "\"bluetooth\":true" : "\"bluetooth\":false") != std::string::npos);
  assert(api(c, p, settings, "/api/settings", {{"bluetoothBackend", "HC05"}}, 0).status == 400);
  assert(stateJson(c, p, settings, 0) == before);
#if SELECTED_BOARD == BOARD_ESP32
  assert(before.find("\"azAdc\":34,\"elAdc\":35") != std::string::npos);
#endif
  std::cout << "PASS: build metadata, pin map and read-only Bluetooth backend\n";
}
