// Per-controller HUD background palette. GPL-3.0.
#pragma once
#include <cstdint>
#include <cstddef>
namespace forkrot {
inline constexpr uint32_t hudBackgrounds[] = {
    0x0a111c, // Standard
    0x081e3a, // Dark blue
    0x092c20, // Dark green
    0x073035, // Dark teal
    0x241334, // Dark purple
    0x35131e, // Burgundy
    0x302113, // Dark brown
    0x181b20, // Graphite
    0x000000, // Black
};
inline constexpr size_t hudBackgroundCount = sizeof(hudBackgrounds) / sizeof(hudBackgrounds[0]);
inline constexpr bool validHudBackground(unsigned value) { return value < hudBackgroundCount; }
inline constexpr uint32_t hudBackgroundColor(unsigned value) {
  return hudBackgrounds[validHudBackground(value) ? value : 0];
}
}
