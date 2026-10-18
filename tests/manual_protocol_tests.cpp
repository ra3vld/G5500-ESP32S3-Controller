#include "motion_fixture.hpp"
#include "api.hpp"
#include "storage.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;

static const char *keys[] = {"n1mmManualFollow", "pstManualFollow", "gs232ManualFollow",
                             "rotctldManualFollow"};
static const char *targets[] = {
    "<N1MMRotor><rotor>*</rotor><goazi>100</goazi></N1MMRotor>",
    "<PST><AZIMUTH>100</AZIMUTH><ELEVATION>45</ELEVATION></PST>", "W100 45", "P 100 45"};
static Packet send(Protocol &p, unsigned protocol, uint32_t now = 1) {
  return protocol < 2 ? p.datagram(targets[protocol], now) : p.line(targets[protocol], now, true);
}
static void configure(Controller &c, Protocol &p, Settings &s, unsigned mask) {
  Form form;
  for (unsigned i = 0; i < 4; ++i) form[keys[i]] = mask & (1u << i) ? "true" : "false";
  assert(api(c, p, s, "/api/settings", form, 0).status == 200);
}
static void policies() {
  for (auto model : {Model::G5500, Model::G800DXA}) {
    for (auto mode : {Mode::Manual, Mode::Auto, Mode::AutoHour}) {
      for (unsigned mask = 0; mask < 16; ++mask) {
        Controller c = forkrot::testController(); c.setModel(model); c.feedback(20, 30, true, true);
        Protocol p(c); Settings settings;
        configure(c, p, settings, mask);
        c.setMode(mode, 0);
        for (unsigned i = 0; i < 4; ++i) {
          c.stop();
          // G-800DXA accepts combined commands only with EL=0.
          Packet packet;
          if (model == Model::G5500 || i == 0) packet = send(p, i);
          else if (i == 1) packet = p.datagram("<PST><AZIMUTH>100</AZIMUTH></PST>", 1);
          else packet = p.line(i == 2 ? "M100" : "P 100 0", 1, true);
          const bool immediate = mode != Mode::Manual || (mask & (1u << i));
          assert(packet.result == (immediate ? Result::Ok : Result::Pending));
          assert(c.mode == mode && c.pendingValid == !immediate);
          if (!immediate) {
            assert(!c.moving() && c.pending.az == 100);
            assert(c.execute(2) == Result::Ok);
          }
          assert(c.axes[0].target == 100 && c.moving());
          assert(c.preset(1, 3) == Result::Ok);
          assert(send(p, 0, 4).result == Result::Ignored);
          if (model == Model::G5500) assert(send(p, i, 4).result == Result::Ignored);
          assert(!c.pendingValid && c.presetActive);
        }
      }
    }
  }
}
static void changesAndSafety() {
  Controller c = forkrot::testController(); c.feedback(20, 30, true, true); Protocol p(c); Settings s;
  assert(send(p, 0).result == Result::Pending);
  configure(c, p, s, 5); // N1MM/GS-232 follow; PST/rotctld confirm.
  assert(c.pendingValid && !c.moving()); // Saving never executes an old target.
  assert(send(p, 0, 2).result == Result::Ok && !c.pendingValid);
  assert(send(p, 1, 3).result == Result::Pending && c.moving());
  assert(c.axes[0].target == 100); // A pending logger cannot interrupt the current movement.
  assert(p.line("M150", 4).result == Result::Ok && !c.pendingValid);
  assert(c.axes[0].target == 150 && c.mode == Mode::Manual);
  assert(send(p, 3, 5).result == Result::Pending);
  assert(p.datagram("<PST><STOP>1</STOP></PST>", 6).result == Result::Ok);
  c.tick(6); assert(!c.moving() && !c.pendingValid && c.mode == Mode::Manual);
  for (unsigned mask : {0u, 15u}) {
    configure(c, p, s, mask);
    assert(p.line("R", 100).result == Result::Ok && c.axes[0].wanted == 1);
    assert(p.line("C", 101).reply == "AZ=020\r\n");
    assert(p.line("p", 101, true).reply.find("20.000000") == 0);
    assert(p.line("S", 102).result == Result::Ok);
    c.tick(102); assert(!c.moving());
  }
  c.feedback(20, 30, false, true);
  for (unsigned i = 0; i < 4; ++i) {
    assert(send(p, i, 110).result == Result::NotCalibrated);
    assert(!c.moving() && !c.pendingValid);
  }
  c.feedback(20, 30, true, true);
  assert(p.line("M9999", 111).result == Result::Invalid && !c.moving());
  // Every fractional/nearby external goal updates the accepted absolute target.
  assert(p.line("M100", 112).result == Result::Ok);
  c.feedback(95, 30, true, true); c.tick(113); assert(!c.moving());
  assert(p.line("M103", 114).result == Result::Ok && c.axes[0].tracking);
  assert(c.axes[0].acceptedTarget == 103);
  assert(p.line("M106", 115).result == Result::Ok && c.axes[0].tracking);
  c.stop();
  // PST's EL, LP and PARK all obey its independent policy.
  assert(p.datagram("<PST><ELEVATION>60</ELEVATION></PST>", 116).result == Result::Ok);
  assert(c.axes[1].target == 60);
  assert(p.datagram("<PST><AZIMUTH>100</AZIMUTH></PST>", 117).result == Result::Ok);
  assert(p.datagram("<PST><LP>1</LP></PST>", 118).result == Result::Ok);
  assert(c.axes[0].target == 280);
  assert(p.datagram("<PST><PARK>1</PARK></PST>", 119).result == Result::Ok);
  c.stop();
  assert(p.line("M1 90 180", 120).result == Result::Ok);
  assert(p.line("T", 121).result == Result::Ok);
  c.tick(1121); assert(c.axes[0].target == 180 && c.mode == Mode::Manual);
  assert(p.line("S", 1122).result == Result::Ok && c.sequenceCount() == 0);
  c.tick(1122);
  configure(c, p, s, 0);
  assert(p.line("M1 90 180", 1123).result == Result::Pending);
  assert(p.line("T", 1124).result == Result::Pending && !c.moving());
  assert(c.execute(1125) == Result::Ok);
  c.tick(2125); assert(c.axes[0].target == 180);
}
static void apiAndStorage() {
  Preferences::database.clear();
  Storage storage;
  MotionTestRuntime fresh; storage.loadForkSettings(fresh);
  for (const char *key : keys)
    assert(stateJson(fresh.controller, fresh.protocol, fresh.loggerSettings, 0).find(
        std::string("\"") + key + "\":false") != std::string::npos);
  for (unsigned mask = 0; mask < 16; ++mask) {
    MotionTestRuntime r; r.controller.feedback(20, 30, true, true);
    configure(r.controller, r.protocol, r.loggerSettings, mask);
    const auto before = stateJson(r.controller, r.protocol, r.loggerSettings, 0);
    for (const char *key : keys) {
      assert(api(r.controller, r.protocol, r.loggerSettings, "/api/settings",
                 {{key, "yes"}, {"rotorName", "Changed"}}, 0).status == 400);
      assert(stateJson(r.controller, r.protocol, r.loggerSettings, 0) == before);
    }
    storage.saveForkSettings(r);
    MotionTestRuntime loaded; storage.loadForkSettings(loaded);
    for (unsigned i = 0; i < 4; ++i) {
      const bool follow = mask & (1u << i);
      assert(stateJson(loaded.controller, loaded.protocol, loaded.loggerSettings, 0).find(
          std::string("\"") + keys[i] + "\":" + (follow ? "true" : "false")) != std::string::npos);
      assert(send(r.protocol, i).result == (follow ? Result::Ok : Result::Pending));
      r.controller.stop();
    }
    assert(!loaded.controller.moving() && !loaded.controller.pendingValid);
  }
}
int main() {
  policies(); changesAndSafety(); apiAndStorage();
  std::cout << "PASS: independent manual protocol policies, modes/models, safety, GS-232 sequences, atomic API and NVS round trip\n";
}
