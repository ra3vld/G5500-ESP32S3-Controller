// G5500 Fork; original by UA1CFM. GPL-3.0.
#pragma once
#include <cstdint>
namespace forkrot {
// Coalesce position/speed samples without delaying command results or STOP.
// The latest value replaces older samples; no telemetry backlog is retained.
inline constexpr uint32_t HudPositionIntervalMs = 100;
inline constexpr uint32_t WebPositionIntervalMs = 200;
// Bounded recovery for a queued frame; manual-input leases remain independent.
inline constexpr uint32_t LiveFrameTimeoutMs = 5000;
} // namespace forkrot
