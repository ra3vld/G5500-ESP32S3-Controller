// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include "motion_timing.hpp"
namespace forkrot {
class PositionFilter {
  std::array<int, 5> values_{};
  unsigned count_ = 0, index_ = 0, period_ = MotionControlPeriodMs;
  uint32_t at_ = 0, nextAt_ = 0, maxIntervalMs_ = 0, conversions_ = 0;
  int result_ = 0;
public:
  bool due(uint32_t now, unsigned period = MotionControlPeriodMs) const {
    return !count_ || period!=period_ || int32_t(now-nextAt_)>=0;
  }
  int value() const { return result_; }
  uint32_t sampledAt() const { return at_; }
  uint32_t maxIntervalMs() const { return maxIntervalMs_; }
  uint32_t conversions() const { return conversions_; }
  int sample(int mv, uint32_t now, unsigned period = MotionControlPeriodMs) {
    if (!due(now,period)) return result_;
    nextAt_ = count_ && period==period_
        ? nextAt_ + (uint32_t(now-nextAt_)/period+1)*period
        : now+period;
    period_=period;
    ++conversions_;
    if (count_) maxIntervalMs_ = std::max(maxIntervalMs_, uint32_t(now-at_));
    values_[index_] = mv;
    index_ = (index_+1)%5;
    count_ = std::min(5u, count_+1);
    auto sorted = values_;
    for (unsigned i=1; i<count_; ++i)
      for (unsigned j=i; j>0 && sorted[j] < sorted[j-1]; --j) std::swap(sorted[j], sorted[j-1]);
    result_ = sorted[count_/2]; at_ = now;
    return result_;
  }
};
}
