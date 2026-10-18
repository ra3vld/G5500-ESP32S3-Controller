// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
namespace forkrot {
class PositionFilter {
  std::array<int, 5> values_{};
  unsigned count_ = 0, index_ = 0;
  uint32_t at_ = 0;
  int result_ = 0;
public:
  bool due(uint32_t now) const { return !count_ || uint32_t(now-at_) >= 20; }
  int value() const { return result_; }
  uint32_t sampledAt() const { return at_; }
  int sample(int mv, uint32_t now) {
    if (!due(now)) return result_;
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
