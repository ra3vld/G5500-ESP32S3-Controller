// G5500 Fork; GPL-3.0.
#pragma once
#include <cstdint>
// Build-time default; the operator can select 50/100/200 Hz at runtime.
#ifndef MOTION_CONTROL_PERIOD_MS
#define MOTION_CONTROL_PERIOD_MS 20
#endif
#if MOTION_CONTROL_PERIOD_MS != 5 && MOTION_CONTROL_PERIOD_MS != 10 && MOTION_CONTROL_PERIOD_MS != 20
#error "MOTION_CONTROL_PERIOD_MS must be 5, 10 or 20"
#endif
namespace forkrot {
inline constexpr unsigned MotionControlPeriodMs = MOTION_CONTROL_PERIOD_MS;
inline constexpr unsigned MotionAdcSamples = 4;
inline constexpr bool validMotionHz(unsigned hz) { return hz==50 || hz==100 || hz==200; }
struct MotionTiming {
  uint32_t steps = 0, maxIntervalMs = 0, lateSteps = 0;
  uint32_t computeMaxUs = 0;
  uint64_t computeTotalUs = 0;
  void recordCompute(uint32_t us) {
    if(us>computeMaxUs)computeMaxUs=us;
    computeTotalUs+=us;
  }
  void record(uint32_t interval, unsigned period = MotionControlPeriodMs) {
    ++steps;
    if (interval > maxIntervalMs) maxIntervalMs = interval;
    if (interval > period) ++lateSteps;
  }
};
}
