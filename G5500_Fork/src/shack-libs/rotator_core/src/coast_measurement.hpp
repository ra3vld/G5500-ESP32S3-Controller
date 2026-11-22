// Fixed-speed, explicitly started coast experiment. GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
struct CoastMeasurement {
  enum Phase : uint8_t { Idle, Running, Coasting, Complete, Failed, Cancelled };
  Phase phase=Idle;
  unsigned axis=0;
  int direction=1;
  uint32_t id=0,started=0,cutAt=0,settledAt=0,revision=0,stopEpoch=0;
  float origin=0,cutPosition=0,travel=10;
  float degrees[2][2]{},seconds[2][2]{};
  uint8_t measured[2]{};
  const char *reason="";
  bool active() const { return phase==Running || phase==Coasting; }
};
}
