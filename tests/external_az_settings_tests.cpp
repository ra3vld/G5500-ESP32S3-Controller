#include "motion_fixture.hpp"
#include "api.hpp"
#include "storage.hpp"
#include <cassert>
#include <iostream>
using namespace forkrot;

int main() {
  Preferences::database.clear();
  Storage storage;
  MotionTestRuntime fresh;
  storage.loadForkSettings(fresh);
  assert(fresh.protocol.externalAzMode == ExternalAzMode::Shortest);
  assert(stateJson(fresh.controller, fresh.protocol, fresh.loggerSettings, 0).find(
             "\"externalAzMode\":\"shortest\"") != std::string::npos);
  for (bool hud : {false, true}) {
    for (const char *mode : {"mechanical", "shortest"}) {
      MotionTestRuntime r;
      if (hud) {
        r.hud.config.role = DeviceRole::Hud;
        r.hud.begin(1, 2);
      }
      r.controller.feedback(350, 30, true, true);
      assert(api(r.controller, r.protocol, r.loggerSettings, "/api/settings",
                 {{"externalAzMode", mode}}, 1).status == 200);
      const auto expected = std::string(mode) == "mechanical" ? ExternalAzMode::Mechanical : ExternalAzMode::Shortest;
      assert(r.protocol.externalAzMode == expected);
      const auto before = stateJson(r.controller, r.protocol, r.loggerSettings, 1);
      for (const char *invalid : {"", "auto", "true", "MECHANICAL"}) {
        assert(api(r.controller, r.protocol, r.loggerSettings, "/api/settings",
                   {{"externalAzMode", invalid}, {"rotorName", "Changed"}}, 1).status == 400);
        assert(stateJson(r.controller, r.protocol, r.loggerSettings, 1) == before);
      }
      assert(api(r.controller, r.protocol, r.loggerSettings, "/api/settings",
                 {{"externalAzMode", "shortest"}, {"pstManualFollow", "bad"}}, 1).status == 400);
      assert(stateJson(r.controller, r.protocol, r.loggerSettings, 1) == before);
      storage.saveForkSettings(r);
      MotionTestRuntime loaded;
      if (hud) {
        loaded.hud.config.role = DeviceRole::Hud;
        loaded.hud.begin(1, 3);
      }
      storage.loadForkSettings(loaded);
      assert(loaded.protocol.externalAzMode == expected);
      assert(!loaded.controller.moving() && !loaded.controller.pendingValid);
    }
  }
  MotionTestRuntime r; r.controller.feedback(350, 30, true, true);
  assert(r.protocol.datagram("<PST><AZIMUTH>10</AZIMUTH></PST>", 1).result == Result::Pending);
  assert(r.controller.pending.az == 370);
  assert(api(r.controller, r.protocol, r.loggerSettings, "/api/settings",
             {{"externalAzMode", "mechanical"}}, 2).status == 200);
  assert(r.controller.pending.az == 370 && !r.controller.moving());
  // Changing the interpretation cannot replay the previous PST base via LP.
  assert(r.protocol.datagram("<PST><LP>1</LP></PST>", 3).result == Result::Ok);
  assert(r.controller.pending.az == 370);
  assert(r.protocol.line("M10", 4).result == Result::Pending);
  assert(r.controller.pending.az == 10);
  assert(api(r.controller, r.protocol, r.loggerSettings, "/api/settings",
             {{"externalAzMode", "shortest"}}, 5).status == 200);
  assert(r.controller.pending.az == 10 && !r.controller.moving());
  assert(r.protocol.line("M10", 6).result == Result::Pending);
  assert(r.controller.pending.az == 370);
  // Web targets and stored trajectories always use mechanical coordinates.
  assert(api(r.controller, r.protocol, r.loggerSettings, "/api/target", {{"az", "10"}}, 7).status == 200);
  assert(r.controller.axes[0].target == 10);
  r.controller.stop();
  assert(r.protocol.line("M1 10 370", 8).result == Result::Pending);
  assert(r.controller.pending.az == 10);
  std::cout << "PASS: external AZ mode default, atomic API, controller/HUD NVS, safe mode changes and mechanical web/sequence targets\n";
}
