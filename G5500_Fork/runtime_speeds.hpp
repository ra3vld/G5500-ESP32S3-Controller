// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <array>
#include <cstdint>
namespace forkrot {
struct RuntimeSpeeds {
  // Simulation, drive, model, axis and regulator mode have independent values.
  static constexpr unsigned Count = 2 * 3 * 2 * 2 * 2;
  static constexpr uint32_t SaveDelayMs = 40000;
  std::array<float, Count> values{}; // Zero selects the regulator's working setting.
  bool dirty = false;
  uint32_t changedAt = 0;
  void set(unsigned index, float value, uint32_t now) {
    if (values[index] == value) return;
    values[index] = value;
    dirty = true;
    changedAt = now;
  }
  bool due(uint32_t now) const { return dirty && uint32_t(now - changedAt) >= SaveDelayMs; }
};
} // namespace forkrot
