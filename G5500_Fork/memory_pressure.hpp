// G5500 Fork; GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
// Enter immediately; leave only after a sustained recovery of internal RAM.
struct MemoryPressure {
  bool limited = false, recovering = false;
  uint32_t healthyAt = 0;
  void sample(uint32_t now, uint32_t free, uint32_t block) {
    if (free < 32768 || block < 12288) { limited = true; recovering = false; }
    else if (limited) {
      if (!recovering) { recovering = true; healthyAt = now; }
      if (uint32_t(now - healthyAt) >= 10000) limited = recovering = false;
    }
  }
};
} // namespace forkrot
