#pragma once
#include <cstdint>

namespace forkrot {
// Count valid Gray-code transitions. Opposite edges cancel contact bounce;
// a skipped/invalid transition discards the incomplete step.
class QuadratureDecoder {
public:
  explicit constexpr QuadratureDecoder(unsigned edges_per_step)
      : edges_per_step_(edges_per_step) {}
  void reset(uint8_t state) {
    previous_ = state & 3;
    pending_ = 0;
  }
  int update(uint8_t state) {
    state &= 3;
    static constexpr int8_t transitions[] = {
        0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    const int step = transitions[previous_ * 4 + state];
    if (state != previous_ && !step)
      pending_ = 0;
    previous_ = state;
    pending_ += step;
    const int result = pending_ / static_cast<int>(edges_per_step_);
    pending_ -= result * static_cast<int>(edges_per_step_);
    return result;
  }

private:
  unsigned edges_per_step_;
  uint8_t previous_ = 0;
  int pending_ = 0;
};
} // namespace forkrot
