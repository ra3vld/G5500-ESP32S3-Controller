#include "motion_fixture.hpp"
#include "hud.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace forkrot;
int main() {
  Controller controller = forkrot::testController();
  Protocol protocol(controller);
  Hud hud(controller, protocol);
  hud.config.role = DeviceRole::Hud;
  hud.begin(1, 2);
  auto *peer = hud.add(3, {0x0100007f, 4534, false}, "Calibration");
  assert(peer);
  peer->received = true;
  peer->receivedAt = hud.now = 100;
  peer->state.model = Model::G5500;
  peer->state.axes[0].current = 123;
  peer->state.axes[1].current = 45;
  for (bool az : {false, true}) for (bool el : {false, true}) {
    peer->state.axes[0].calibrated = az;
    peer->state.axes[1].calibrated = el;
    char data[4096]; JsonWriter json(data, sizeof data);
    assert(hud.writePeers(json, 0, 8));
    const std::string result(json.data());
    assert(result.find(az ? "\"azCalibrated\":true" : "\"azCalibrated\":false") != std::string::npos);
    assert(result.find(el ? "\"elCalibrated\":true" : "\"elCalibrated\":false") != std::string::npos);
    assert(result.find("\"az\":123") != std::string::npos && result.find("\"el\":45") != std::string::npos);
    assert(result.find("\"hasEl\":true") != std::string::npos);
  }
  peer->state.model = Model::G800DXA;
  char data[4096]; JsonWriter json(data, sizeof data);
  assert(hud.writePeers(json, 0, 8));
  assert(std::string(json.data()).find("\"hasEl\":false") != std::string::npos);
  for (unsigned i = 0; i < 7; ++i)
    assert(hud.add(4 + i, {0x0100007f, 4534, false}, "Длинное имя поворотки AZ EL"));
  JsonWriter page(data, sizeof data);
  assert(hud.writePeers(page, 0, 8));
  std::cout << "PASS: independent HUD calibration flags, numeric API positions, G800 and eight-peer page capacity\n";
}
