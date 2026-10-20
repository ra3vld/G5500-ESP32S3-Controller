// G5500 Fork; original by UA1CFM. GPL-3.0.
#include "input.hpp"
namespace forkrot {
bool Button::sample(bool released, uint32_t now) {
  if (released != rawReleased_) {
    rawReleased_ = released;
    rawChangedAt_ = now;
  }
  if (uint32_t(now - rawChangedAt_) < 40) {
    return false;
  }
  if (rawReleased_) {
    armed_ = true;
    stableReleased_ = true;
    return false;
  }
  if (stableReleased_ && armed_) {
    stableReleased_ = false;
    armed_ = false;
    return true;
  }
  stableReleased_ = false;
  return false;
}
} // namespace forkrot
