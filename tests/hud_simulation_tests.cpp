#include "motion_fixture.hpp"
#include "hud_input.hpp"
#include "api.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace forkrot;
struct Transport : HudTransport {
  unsigned sent = 0;
  bool sendHud(const HudEndpoint &e, const uint8_t *, size_t) override {
    // Built-in simulation must never escape through a network/UART transport.
    assert(e.address || e.uart);
    ++sent;
    return false;
  }
};
int main() {
  Controller mirror;
  const auto hardware = mirror.hardware;
  Protocol protocol(mirror);
  protocol.rotorName = "HUD identity";
  Hud hud(mirror, protocol);
  Transport transport;
  hud.setTransport(&transport);
  hud.config.role = DeviceRole::Hud;
  hud.begin(1, 2);
  assert(!hud.config.simulationEnabled && !hud.count() && !hud.select(HUD_SIMULATION_ID));
  assert(hud.setSimulationEnabled(true));
  assert(hud.count() == 1 && !hud.config.selected);
  auto *sim = hud.peer(HUD_SIMULATION_ID);
  assert(sim && sim->simulated() && sim->saved && sim->manual);
  assert(!hud.remove(sim->id) && !hud.add(sim->id, {3, 4534, false}, "Override"));
  assert(hud.select(sim->id) && hud.available());
  assert(mirror.remote == &hud && mirror.hardware == hardware);
  assert(mirror.axes[0].calibrated && mirror.axes[1].calibrated);
  assert(mirror.model == Model::G5500 && mirror.manualAvailable());
  assert(protocol.rotorName == "HUD identity");
  char json[4096];
  JsonWriter status(json, sizeof json); hud.writeStatus(status);
  assert(std::string(json).find("\"hudTransport\":\"simulation\"") != std::string::npos);
  assert(std::string(json).find("\"remoteDrive\":\"SIMULATED\"") != std::string::npos);
  assert(std::string(json).find("hudHost") == std::string::npos);
  uint32_t now = 100;
  auto tick = [&](unsigned elapsed = 20) { now += elapsed; hud.service(now); mirror.tick(now); };
  auto advance = [&](unsigned ms) { for (unsigned i = 0; i < ms; i += 20) tick(); };
  auto result = [&] {
    const auto id = hud.requestId(); tick(); return hud.result(id).result;
  };
  assert(mirror.target({9999, 0, true, false}, false, now) == Result::Invalid);
  // Local commands use the same deferred results, per-axis ordering and STOP slot.
  assert(mirror.setMode(Mode::Auto, now) == Result::Deferred && result() == Result::Ok);
  assert(mirror.setRuntimeSpeed(0, 70, now) == Result::Deferred && result() == Result::Ok);
  assert(mirror.setRuntimeSpeed(1, 35, now) == Result::Deferred && result() == Result::Ok);
  assert(mirror.runtimeSpeed(0) == 70 && mirror.runtimeSpeed(1) == 35);
  assert(mirror.target({90, 40, true, true}, false, now) == Result::Deferred);
  const auto cancelled = hud.requestId();
  assert(mirror.target({110, 0, true, false}, false, now) == Result::Deferred);
  assert(hud.result(cancelled).result == Result::Cancelled && result() == Result::Ok);
  assert(mirror.axes[0].target == 110 && mirror.axes[1].target == 40);
  advance(1500);
  assert(mirror.axes[0].current > 0 && mirror.axes[1].current > 0);
  mirror.stopNow(-1, now); assert(result() == Result::Ok);
  const float stopped = mirror.axes[0].current;
  advance(1000); assert(mirror.axes[0].current == stopped && !mirror.moving());
  assert(mirror.target({200, 0, true, false}, false, now) == Result::Deferred);
  const auto beforeStop = hud.requestId(); mirror.stopNow(-1, now);
  assert(hud.result(beforeStop).result == Result::Cancelled && result() == Result::Ok);
  assert(!mirror.moving());
  assert(mirror.savePreset(0, "Test preset", {150, 30, true, true}) == Result::Deferred);
  assert(result() == Result::Ok && mirror.presets[0].label == "Test preset");
  assert(mirror.preset(0, now) == Result::Deferred && result() == Result::Ok);
  assert(mirror.axes[0].target == 150 && mirror.axes[1].target == 30);
  mirror.stopNow(-1, now); result();
  // External targets in MANUAL remain pending until explicitly executed.
  assert(mirror.setMode(Mode::Manual, now) == Result::Deferred && result() == Result::Ok);
  assert(protocol.line("M180", now).result == Result::Deferred && result() == Result::Pending);
  assert(mirror.pendingValid && !mirror.moving());
  assert(mirror.execute(now) == Result::Deferred && result() == Result::Ok);
  advance(1000); assert(mirror.moving());
  mirror.stopNow(-1, now); result();
  // Both touch and web manual streams retain lease, release and stale-gesture protection.
  ControlCommand manual; manual.op = ControlOp::ManualDrive;
  manual.gesture = 10; manual.axesMask = 3; manual.manual[0] = .8f; manual.manual[1] = .4f;
  manual.absoluteSpeed = true;
  const float beforeManual = mirror.axes[0].current;
  for (unsigned i = 0; i < 25; ++i) {
    assert(hud.submitAt(manual, now) == Result::Deferred); tick(40);
  }
  assert(mirror.axes[0].current > beforeManual && mirror.moving());
  advance(300); assert(!mirror.moving());
  assert(hud.submitAt(manual, now) == Result::Cancelled);
  manual.gesture = 11; manual.inputLeaseMs = 500;
  assert(hud.submitAt(manual, now) == Result::Deferred); advance(200);
  // A panel flush pumps input/leases without entering the trajectory solver.
  const float renderedPosition=mirror.axes[0].current;
  now+=20;hud.service(now,false);mirror.tick(now);
  assert(mirror.axes[0].current==renderedPosition);
  manual.release = true;
  now+=20;
  assert(hud.submitAt(manual, now) == Result::Deferred);
  assert(mirror.axes[0].current==renderedPosition && !mirror.moving());
  tick();
  assert(!mirror.moving());
  assert(!hud.settingsDirty && !hud.presetsDirty && !transport.sent);
  // With one real peer plus simulation, the existing two-entry gesture switches directly.
  auto *real = hud.add(3, {3, 4534, false}, "Real"); assert(real);
  real->received = true; real->receivedAt = now; real->boot = 99;
  real->state.axes[0].current = 42; real->state.axes[0].calibrated = true;
  HudKnob knob(hud, mirror);
  const auto epoch = hud.selectionEpoch;
  knob.open(); assert(!knob.menu && hud.config.selected == real->id && hud.selectionEpoch == epoch + 1);
  assert(mirror.axes[0].current == 42);
  knob.open(); assert(hud.config.selected == sim->id && hud.available());
  assert(mirror.presets[0].label == "Test preset" && mirror.runtimeSpeed(0) == 70);
  // Switching discards an unsent numeric command, but an accepted numeric move continues.
  assert(mirror.target({250, 50, true, true}, false, now) == Result::Deferred);
  const auto unsent = hud.requestId();
  assert(hud.select(real->id) && hud.result(unsent).result == Result::Cancelled);
  assert(hud.select(sim->id));
  assert(mirror.target({300, 90, true, true}, false, now) == Result::Deferred && result() == Result::Ok);
  advance(1000); const float background = mirror.axes[0].current;
  assert(hud.select(real->id)); advance(1000);
  assert(hud.select(sim->id));
  assert(mirror.axes[0].current > background && mirror.axes[0].target == 300);
  assert(real->state.axes[0].current == 42 && !real->state.axes[0].tracking);
  mirror.stopNow(-1, now); result();
  // Changing selection releases a locally owned jog and a manual stream.
  assert(mirror.jog(0, 1, false, now) == Result::Deferred && result() == Result::Ok);
  advance(1000); assert(mirror.moving());
  assert(hud.select(real->id)); advance(100);
  assert(hud.select(sim->id) && !mirror.moving());
  manual.gesture = 12; manual.release = false;
  assert(hud.submitAt(manual, now) == Result::Deferred); advance(60);
  assert(hud.select(real->id)); advance(100);
  assert(hud.select(sim->id) && !mirror.moving());
  assert(mirror.hardware == hardware && mirror.remote == &hud && protocol.rotorName == "HUD identity");
  // Disabling removes simulation from every selector, cancels queued motion and clears its selection.
  assert(mirror.target({400, 100, true, true}, false, now) == Result::Deferred);
  const auto disabledJob = hud.requestId();
  const auto beforeDisable = hud.selectionEpoch;
  assert(hud.setSimulationEnabled(false));
  assert(!hud.config.simulationEnabled && !hud.peer(HUD_SIMULATION_ID) && hud.count() == 1);
  assert(!hud.config.selected && hud.selectionEpoch == beforeDisable + 1 && !hud.available());
  assert(hud.result(disabledJob).result == Result::Cancelled && !hud.select(HUD_SIMULATION_ID));
  assert(hud.setSimulationEnabled(true) && hud.select(HUD_SIMULATION_ID));
  assert(mirror.axes[0].current == 0 && mirror.axes[1].current == 0 && !mirror.moving());
  assert(hud.select(real->id));
  const auto realEpoch = hud.selectionEpoch;
  assert(hud.setSimulationEnabled(false) && hud.config.selected == real->id && hud.selectionEpoch == realEpoch);
  // A restored selection starts with fresh virtual state and no network requirement.
  Controller fresh; Protocol freshProtocol(fresh); Hud reboot(fresh, freshProtocol);
  reboot.config.role = DeviceRole::Hud; reboot.config.simulationEnabled = true; reboot.config.selected = HUD_SIMULATION_ID;
  reboot.begin(1, 3);
  assert(reboot.available() && fresh.axes[0].current == 0 && fresh.axes[1].current == 0);
  reboot.service(UINT32_MAX - 50); reboot.service(100);
  assert(reboot.available() && !reboot.timeouts);
  std::cout << "PASS: built-in HUD simulation, AZ/EL, speed, presets, pending/STOP, leases, selection, independent state, restart and no local transport\n";
}
