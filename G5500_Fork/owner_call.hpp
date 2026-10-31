// G5500 Fork; GPL-3.0.
#pragma once
#include <atomic>
namespace forkrot {
// One producer, one state owner. The producer retains its arguments until
// takeResult(); no allocations, queue growth, or shared Controller copies.
class OwnerCall {
public:
  using Function = void (*)(void *);
  bool post(Function function, void *context) {
    if (!function || state_.load(std::memory_order_acquire) != State::Idle) return false;
    function_ = function; context_ = context;
    state_.store(State::Pending, std::memory_order_release);
    return true;
  }
  bool service() {
    if (state_.load(std::memory_order_acquire) != State::Pending) return false;
    // Mark running before invoking: callbacks may service other transports.
    state_.store(State::Running, std::memory_order_relaxed);
    function_(context_);
    state_.store(State::Done, std::memory_order_release);
    return true;
  }
  bool takeResult() {
    if (state_.load(std::memory_order_acquire) != State::Done) return false;
    state_.store(State::Idle, std::memory_order_release);
    return true;
  }
private:
  enum class State { Idle, Pending, Running, Done };
  std::atomic<State> state_{State::Idle};
  Function function_ = nullptr;
  void *context_ = nullptr;
};
}
