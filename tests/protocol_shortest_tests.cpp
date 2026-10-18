#include "motion_fixture.hpp"
#include "protocol.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace forkrot;

static void near(float actual, float expected) {
  assert(std::abs(actual - expected) < .002f);
}

int main() {
  for (auto mode : {ExternalAzMode::Shortest, ExternalAzMode::Mechanical}) {
    for (bool automatic : {false, true}) {
      for (const char *command : {"M010", "W010 045", "P 10 45", "set_pos 10 45", "\\set_pos 10 45"}) {
        Controller c = forkrot::testController();
        c.feedback(350, 30, true, true);
        if (automatic) c.setMode(Mode::Auto, 0);
        Protocol p(c);
        p.setExternalAzMode(mode);
        const auto reply = p.line(command, 1, true);
        assert(reply.result == (automatic ? Result::Ok : Result::Pending));
        const bool shortest = mode == ExternalAzMode::Shortest;
        near(automatic ? c.axes[0].target : c.pending.az, shortest ? 370 : 10);
        if (!automatic) assert(c.execute(2) == Result::Ok);
        near(c.axes[0].target - c.axes[0].current, shortest ? 20 : -340);
        if (command[0] != 'M') near(c.axes[1].target, 45);
        c.tick(3);
        assert(c.axes[0].direction == (shortest ? 1 : -1));
      }
    }
  }
  struct Route { AzRange range; float current, requested, expected; };
  for (const auto &route : {
           Route{{0, 450}, 400, 10, 370},
           Route{{0, 450}, 350, 360, 360},
           Route{{0, 450}, 10, 350, 350}, // A negative turn would cross the lower stop.
           Route{{0, 360}, 350, 10, 10}, // No overlap: 370 is outside the physical range.
           Route{{-60, 390}, 350, 10, 370},
           Route{{-60, 390}, 10, 350, -10},
           Route{{50, 500}, 350, 10, 370}}) {
    Controller c = forkrot::testController();
    assert(c.setRange(route.range, AZ_CUSTOM));
    c.feedback(route.current, 30, true, true);
    Protocol p(c);
    assert(p.line("M" + std::to_string(route.requested), 1).result == Result::Pending);
    near(c.pending.az, route.expected);
    assert(route.range.contains(c.pending.az));
  }
  // Explicit continuous coordinates, stored trajectories and linear EL keep their meaning.
  for (const char *command : {"M450", "W450 045", "P 450 45"}) {
    Controller c = forkrot::testController(); c.feedback(350, 30, true, true); Protocol p(c);
    assert(p.line(command, 1).result == Result::Pending);
    near(c.pending.az, 450);
  }
  {
    Controller c = forkrot::testController(); assert(c.setRange({-60, 390}, AZ_CUSTOM));
    c.feedback(350, 30, true, true); Protocol p(c);
    assert(p.line("M-60", 1).result == Result::Pending); near(c.pending.az, -60);
    assert(p.line("M10 10", 2).result == Result::Pending); near(c.pending.az, 10);
  }
  {
    Controller c = forkrot::testController(); assert(c.setRange({300, 390}, AZ_CUSTOM));
    c.feedback(350, 30, true, true); Protocol p(c);
    assert(p.line("M360", 1).result == Result::Pending);
    assert(p.line("M90", 2).result == Result::Invalid); near(c.pending.az, 360);
    assert(!c.moving());
  }
  {
    Controller c = forkrot::testController(); assert(c.setElRange({-30, 420}, EL_CUSTOM));
    c.feedback(350, 350, true, true); Protocol p(c);
    assert(p.line("W010 010", 1).result == Result::Pending);
    near(c.pending.az, 370); near(c.pending.el, 10);
  }
  // Existing bearing protocols must agree with line protocols across north.
  for (const char *command : {"<PST><AZIMUTH>10</AZIMUTH></PST>",
       "<N1MMRotor><rotor>*</rotor><goazi>10</goazi></N1MMRotor>"}) {
    Controller c = forkrot::testController(); c.feedback(350, 30, true, true); Protocol p(c);
    assert(p.datagram(command, 1).result == Result::Pending); near(c.pending.az, 370);
  }
  for (const char *command : {"<PST><AZIMUTH>10</AZIMUTH></PST>",
       "<N1MMRotor><rotor>*</rotor><goazi>10</goazi></N1MMRotor>"}) {
    for (bool automatic : {false, true}) {
      Controller c = forkrot::testController(); c.feedback(350, 30, true, true); Protocol p(c);
      p.setExternalAzMode(ExternalAzMode::Mechanical);
      if (automatic) c.setMode(Mode::Auto, 0);
      assert(p.datagram(command, 1).result == (automatic ? Result::Ok : Result::Pending));
      near(automatic ? c.axes[0].target : c.pending.az, 10);
    }
  }
  // Mechanical commands retain signed and multi-turn coordinates on every protocol.
  for (const auto range : {AzRange{-60, 390}, AzRange{50, 500}}) {
    for (float angle : {-60.f, 10.f, 360.f, 370.f, 500.f}) {
      for (unsigned protocol = 0; protocol < 4; ++protocol) {
        Controller c = forkrot::testController(); assert(c.setRange(range, AZ_CUSTOM));
        c.feedback(350, 30, true, true); Protocol p(c);
        p.setExternalAzMode(ExternalAzMode::Mechanical);
        // A rejected coordinate must preserve an earlier valid pending target.
        assert(p.line("M360", 0).result == Result::Pending);
        const auto value = std::to_string(angle);
        const auto reply = protocol == 0 ? p.line("W" + value + " 45", 1)
                         : protocol == 1 ? p.line("P " + value + " 45", 1, true)
                         : protocol == 2 ? p.datagram("<PST><AZIMUTH>" + value + "</AZIMUTH></PST>", 1)
                         : p.datagram("<N1MMRotor><rotor>*</rotor><goazi>" + value + "</goazi></N1MMRotor>", 1);
        assert(reply.result == (range.contains(angle) ? Result::Pending : Result::Invalid));
        near(c.pending.az, range.contains(angle) ? angle : 360);
        assert(!c.moving());
      }
    }
  }
  // Protocol direction modifiers keep their meaning without selecting another turn.
  {
    Controller c = forkrot::testController(); c.feedback(350, 30, true, true); Protocol p(c);
    p.setExternalAzMode(ExternalAzMode::Mechanical);
    assert(p.datagram("<N1MMRotor><rotor>*</rotor><goazi>10</goazi><offset>5</offset><bidirectional>1</bidirectional></N1MMRotor>", 1).result == Result::Pending);
    near(c.pending.az, 15);
    assert(p.datagram("<PST><AZIMUTH>370</AZIMUTH></PST>", 2).result == Result::Pending);
    assert(p.datagram("<PST><LP>1</LP></PST>", 3).result == Result::Invalid);
    near(c.pending.az, 370); // LP would request 550, beyond the hard stop.
    assert(p.datagram("<PST><AZIMUTH>10</AZIMUTH><LP>1</LP></PST>", 4).result == Result::Pending);
    near(c.pending.az, 190);
  }
  std::cout << "PASS: selectable external AZ routes, manual/auto, all protocols, hard stops, explicit turns, modifiers and linear EL\n";
}
