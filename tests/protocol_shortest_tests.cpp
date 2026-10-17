#include "protocol.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace forkrot;

static void near(float actual, float expected) {
  assert(std::abs(actual - expected) < .002f);
}

int main() {
  for (bool automatic : {false, true}) {
    for (const char *command : {"M010", "W010 045", "P 10 45", "set_pos 10 45", "\\set_pos 10 45"}) {
      Controller c;
      c.feedback(350, 30, true, true);
      if (automatic) c.setMode(Mode::Auto, 0);
      Protocol p(c);
      const auto reply = p.line(command, 1, true);
      assert(reply.result == (automatic ? Result::Ok : Result::Pending));
      near(automatic ? c.axes[0].target : c.pending.az, 370);
      if (!automatic) assert(c.execute(2) == Result::Ok);
      near(c.axes[0].target - c.axes[0].current, 20);
      if (command[0] != 'M') near(c.axes[1].target, 45);
      c.tick(3);
      assert(c.axes[0].direction == 1);
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
    Controller c;
    assert(c.setRange(route.range, AZ_CUSTOM));
    c.feedback(route.current, 30, true, true);
    Protocol p(c);
    assert(p.line("M" + std::to_string(route.requested), 1).result == Result::Pending);
    near(c.pending.az, route.expected);
    assert(route.range.contains(c.pending.az));
  }
  // Explicit continuous coordinates, stored trajectories and linear EL keep their meaning.
  for (const char *command : {"M450", "W450 045", "P 450 45"}) {
    Controller c; c.feedback(350, 30, true, true); Protocol p(c);
    assert(p.line(command, 1).result == Result::Pending);
    near(c.pending.az, 450);
  }
  {
    Controller c; assert(c.setRange({-60, 390}, AZ_CUSTOM));
    c.feedback(350, 30, true, true); Protocol p(c);
    assert(p.line("M-60", 1).result == Result::Pending); near(c.pending.az, -60);
    assert(p.line("M10 10", 2).result == Result::Pending); near(c.pending.az, 10);
  }
  {
    Controller c; assert(c.setRange({300, 390}, AZ_CUSTOM));
    c.feedback(350, 30, true, true); Protocol p(c);
    assert(p.line("M360", 1).result == Result::Pending);
    assert(p.line("M90", 2).result == Result::Invalid); near(c.pending.az, 360);
    assert(!c.moving());
  }
  {
    Controller c; assert(c.setElRange({-30, 420}, EL_CUSTOM));
    c.feedback(350, 350, true, true); Protocol p(c);
    assert(p.line("W010 010", 1).result == Result::Pending);
    near(c.pending.az, 370); near(c.pending.el, 10);
  }
  // Existing bearing protocols must agree with line protocols across north.
  for (const char *command : {"<PST><AZIMUTH>10</AZIMUTH></PST>",
       "<N1MMRotor><rotor>*</rotor><goazi>10</goazi></N1MMRotor>"}) {
    Controller c; c.feedback(350, 30, true, true); Protocol p(c);
    assert(p.datagram(command, 1).result == Result::Pending); near(c.pending.az, 370);
  }
  std::cout << "PASS: shortest external AZ targets, manual/auto, hard stops, explicit turns and linear EL\n";
}
